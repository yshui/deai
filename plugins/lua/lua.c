/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, 2020 Yuxuan Shui <yshuiv7@gmail.com> */

// Q: how to prevent the user script from creating reference cycles?
// A: this needs to be investigated, but here are a few points
//    * lua state cannot hold strong references to listen handles. otherwise there will be
//      cycles:
//        lua state -> listen handle -> handler (assuming handler is a lua function) ->
//        lua state
//    * (more of a thing the deai core should do), objects cannot hold strong references
//      to their listen handles. otherwise:
//        lua state -> object -> listen handle -> handler -> lua state
//
//    with the above 2 points, things should be mostly fine. lua scripts can create 2
//    kinds of strong references:
//    * lua state -> di_object(A), by assigning di_object to lua variable
//    * di_object(B) -> lua state, by creating a lua table and return that to deai
//
//    as long as the script doesn't create reference chains from A to B, things should be
//    fine. here are some possbilities how such references can be created:
//    * register a lua table as module, then get that module.
//    * emit a signal with a lua table as argument, then handle that signal and store the
//      object
//
//    a completely solution would probably be using mark-and-sweep GC.

// To prevent reference cycles, lua doesn't hold strong reference to listen handles. But
// that's OK. The deai core uses implicit listener deregisteration, listeners are stopped
// when the listen handle is dropped. but the lua scripts will use a different semantic.
// In lua scripts, the listen handles are automatically add as roots and kept alive.
// They have to be explicitly stopped.
#include <assert.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdio.h>
#include <string.h>

#include <deai/builtins/log.h>
#include <deai/deai.h>
#include <deai/error.h>
#include <deai/helper.h>
#include <deai/type.h>

#include "common.h"
#include "compat.h"

#define tmalloc(type, nmem) (type *)calloc(nmem, sizeof(type))
#define auto __auto_type

#define DI_LUA_REGISTRY_STATE_OBJECT_KEY "__deai.di_lua.state_object"
#define DEAI_LUA_REGISTRY_OBJECT_CACHE_KEY "__deai.di_lua.object_cache"

#define di_lua_get_state(L, s)                                                           \
	do {                                                                                 \
		lua_pushliteral((L), DI_LUA_REGISTRY_STATE_OBJECT_KEY);                          \
		lua_rawget((L), LUA_REGISTRYINDEX);                                              \
		(s) = lua_touserdata((L), -1);                                                   \
		lua_pop(L, 1);                                                                   \
	} while (0)

/// A singleton for lua_State
typedef struct di_lua_state {
	// Beware of cycles, could happen if one lua object is registered as module
	// and access in lua again as module.
	//
	// lua_state -> di_module/di_lua_ref -> lua_script -> lua_state
	di_object;
	lua_State *L;

	// Object tracking in di_lua:
	//
	// We track all objects that lives in a lua state, in order to make mark-and-sweep
	// work, as well as deduplicate the object proxies.
	//
	// Object tracking is done in 3 parts:
	//    1) Weak references to the proxies in the lua registry. This is used so when
	//       the same object are pushed multiple times, we can use a reference to the
	//       same proxy.
	//    2) The ___lua_userdata_to_object_<pointer> members of the di_lua_state
	//       object, that maps lua userdata pointer to di_object, this also make
	//       makr-and-sweep work.
	//    3) The ___di_object_to_ref_<pointer> member of the di_lua_state object,
	//       which maps di_object pointers to the index of the lua proxies in the
	//       registry.
	//
	// For most part object <-> proxy is a 1-to-1 map, but because of a quirk of Lua
	// it might transiently stops being one. The weak reference in the lua registry
	// could die before __gc for the proxy is called. In that scenario,
	// di_lua_pushproxy has to create a new proxy, which means 2 proxies could exist
	// (although one of them is going to be GC'd soon).
} di_lua_state;

struct di_lua_ref {
	di_object;
	int tref;
};

static int di_lua_pushvariant(lua_State *L, di_string name, struct di_variant var);
static int di_lua_meta_index(lua_State *L);
static int di_lua_meta_index_for_weak_object(lua_State *L);
static int di_lua_meta_newindex(lua_State *L);
static int di_lua_meta_pairs(lua_State *L);

static int di_lua_type_to_di(lua_State *L, int i, di_type type_hint, di_type *t, di_value *ret);
static void di_lua_pushobject(lua_State *L, di_string name, di_object *obj);
static void **
di_lua_pushproxy(lua_State *L, di_string name, void *o, const luaL_Reg *reg, bool callable);

static const luaL_Reg di_lua_weak_object_methods[];

static int di_lua_errfunc(lua_State *L) {
	/* Convert error to string, to prevent a follow-up error with lua_concat. */
	di_type err_type;
	di_value err = {0};
	di_object *err_obj = NULL;
	di_lua_type_to_di(L, -1, DI_TYPE_ANY, &err_type, &err);
	if (err_type != DI_TYPE_OBJECT) {
		scopedp(char) *err_str = di_value_to_string(err_type, &err);
		lua_Debug ar;
		lua_getstack(L, 1, &ar);
		const char *path = NULL;
		if (lua_isstring(L, lua_upvalueindex(1))) {
			path = lua_tolstring(L, lua_upvalueindex(1), NULL);
		} else if (ar.source != NULL && ar.source[0] == '@') {
			path = ar.source + 1;
		} else if (*ar.short_src) {
			path = ar.short_src;
		}
		err_obj = di_new_error2(path, ar.currentline, ar.name, err_str);
		di_free_value(err_type, &err);
	} else {
		err_obj = err.object;
	}

	// Get debug.traceback
	lua_getglobal(L, "debug");
	lua_pushstring(L, "traceback");
	lua_gettable(L, -2);

	// Push arguments
	lua_pushnil(L);
	lua_pushinteger(L, 3);

	// Call debug.traceback(error_prompt, 3), this should leave the error message we
	// want on the top of the stack.
	if (lua_pcall(L, 2, 1, 0) != 0) {
		// If we failed to get a stack trace, we have to generate a generic error
		// message
		auto err2 = di_new_error(luaL_tolstring(L, -1, NULL));
		di_add_member_move(err2, di_string_borrow_literal("source"), &err_type, &err_obj);
		err_obj = err2;
	} else {
		auto traceback = lua_tolstring(L, -1, NULL);
		di_string original_detail = DI_STRING_INIT;
		if (di_rawget_borrowed(err_obj, "detail", original_detail) == 0) {
			di_string new_detail = di_string_printf(
			    "%.*s\n%s", (int)original_detail.length, original_detail.data, traceback);
			di_delete_member_raw(err_obj, di_string_borrow_literal("detail"));
			di_member(err_obj, "detail", new_detail);
		} else {
			di_member_clone(err_obj, "detail", di_string_borrow(traceback));
		}
	}

	di_lua_pushobject(L, DI_STRING_INIT, err_obj);
	return 1;
}

static bool di_lua_isproxy(lua_State *L, int index) {
	bool ret = false;
	do {
		if (!lua_isuserdata(L, index)) {
			break;
		}
		if (!lua_getmetatable(L, index)) {
			break;
		}

		lua_pushliteral(L, "__is_deai_proxy");
		lua_rawget(L, -2);
		if (lua_isnil(L, -1)) {
			break;
		}

		ret = true;
	} while (0);
	// Pops 1 boolean (__is_deai_proxy) and 1 metatable
	lua_pop(L, 2);
	return ret;
}

static void **di_lua_checkproxy(lua_State *L, int index) {
	if (di_lua_isproxy(L, index)) {
		return lua_touserdata(L, index);
	}
	luaL_argerror(L, index, "not a di_object");
	unreachable();
}

static void lua_ref_dtor(di_object *obj) {
	auto t = (struct di_lua_ref *)obj;
	scoped_di_object *state_obj = NULL;
	// The state object might already be finalized if we are part of
	// a reference cycle.
	if (di_get(t, "___di_lua_state", state_obj) != 0) {
		return;
	}

	auto state = (struct di_lua_state *)state_obj;
	if (state->L != NULL) {
		luaL_unref(state->L, LUA_REGISTRYINDEX, t->tref);
	}
}

static inline int di_lua_type_to_di_variant(lua_State *L, int i, struct di_variant *var) {
	int rc = di_lua_type_to_di(L, i, DI_TYPE_ANY, &var->type, NULL);
	if (rc != 0) {
		return rc;
	}

	var->value = malloc(di_sizeof_type(var->type));
	di_lua_type_to_di(L, i, DI_TYPE_ANY, &var->type, var->value);
	return 0;
}

static int di_lua_di_getter(di_object *m, di_type *rt, di_value *ret, di_tuple tu) {
	if (tu.length != 2) {
		return -EINVAL;
	}

	struct di_variant *vars = tu.elements;
	if (vars[0].type != DI_TYPE_OBJECT) {
		DI_ASSERT(false, "first argument to getter is not an object");
		return -EINVAL;
	}

	auto t = (struct di_lua_ref *)vars[0].value->object;
	if (vars[1].type != DI_TYPE_STRING && vars[1].type != DI_TYPE_STRING_LITERAL) {
		return -EINVAL;
	}

	scoped_di_object *state_obj = NULL;
	DI_CHECK_OK(di_get(t, "___di_lua_state", state_obj));

	auto state = (struct di_lua_state *)state_obj;
	lua_State *L = state->L;

	assert(lua_gettop(L) == 0);
	lua_rawgeti(L, LUA_REGISTRYINDEX, t->tref);        // Stack: [ table ]

	if (vars[1].type == DI_TYPE_STRING) {
		lua_pushlstring(L, vars[1].value->string.data, vars[1].value->string.length);
	} else {
		const char *key = vars[1].value->string_literal;
		lua_pushstring(L, key);
	}
	// Stack: [ table key ]
	lua_gettable(L, -2);        // Stack: [ table value ]

	DI_OK_OR_RET(di_lua_type_to_di(L, -1, DI_TYPE_ANY, rt, ret));

	if (*rt == DI_TYPE_NIL) {
		// nil in Lua means non-existent.
		*rt = DI_LAST_TYPE;
	}
	lua_pop(L, 2);        // Pop the value and the table
	assert(lua_gettop(L) == 0);
	return 0;
}

static int di_lua_di_setter(di_object *m, di_type *rt, di_value *ret, di_tuple tu) {
	if (tu.length != 3) {
		return -EINVAL;
	}

	struct di_variant *vars = tu.elements;
	if (vars[0].type != DI_TYPE_OBJECT) {
		DI_ASSERT(false, "first argument to getter is not an object");
		return -EINVAL;
	}

	auto t = (struct di_lua_ref *)vars[0].value->object;
	if (vars[1].type != DI_TYPE_STRING && vars[1].type != DI_TYPE_STRING_LITERAL) {
		return -EINVAL;
	}

	scoped_di_object *state_obj = NULL;
	DI_CHECK_OK(di_get(t, "___di_lua_state", state_obj));

	auto state = (struct di_lua_state *)state_obj;
	lua_State *L = state->L;

	assert(lua_gettop(L) == 0);
	lua_rawgeti(L, LUA_REGISTRYINDEX, t->tref);

	if (vars[1].type == DI_TYPE_STRING) {
		lua_pushlstring(L, vars[1].value->string.data, vars[1].value->string.length);
	} else {
		const char *key = vars[1].value->string_literal;
		lua_pushstring(L, key);
	}

	if (di_lua_pushvariant(L, DI_STRING_INIT, vars[2]) != 1) {
		lua_pop(L, 2);        // key and table
		assert(lua_gettop(L) == 0);
		return -EINVAL;
	}
	lua_settable(L, -3);
	lua_pop(L, 1);        // table

	*rt = DI_TYPE_NIL;
	assert(lua_gettop(L) == 0);
	return 0;
}

static di_tuple di_lua_di_next(di_object *lua_ref, di_string name) {
	struct di_lua_ref *t = (struct di_lua_ref *)lua_ref;
	scoped_di_object *state_obj = NULL;
	DI_CHECK_OK(di_get(t, "___di_lua_state", state_obj));

	auto state = (struct di_lua_state *)state_obj;
	lua_State *L = state->L;
	assert(lua_gettop(L) == 0);

	lua_rawgeti(L, LUA_REGISTRYINDEX, t->tref);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		assert(lua_gettop(L) == 0);
		return DI_TUPLE_INIT;
	}

	if (name.data) {
		lua_pushlstring(L, name.data, name.length);
	} else {
		lua_pushnil(L);
	}

	di_string key;
	while (true) {
		if (lua_next(L, -2) == 0) {
			lua_pop(L, 1);
			assert(lua_gettop(L) == 0);
			return DI_TUPLE_INIT;
		}
		// Ignore non-string keys
		if (!lua_isstring(L, -2)) {
			lua_pop(L, 1);
			continue;
		}
		key.data = lua_tolstring(L, -2, &key.length);
		if (!di_string_starts_with(key, "__")) {
			break;
		}
	}

	di_tuple ret;
	ret.length = 2;
	ret.elements = tmalloc(struct di_variant, 2);
	ret.elements[0] = di_alloc_variant(di_clone_string(key));
	di_lua_type_to_di_variant(L, -1, &ret.elements[1]);
	lua_pop(L, 3);
	assert(lua_gettop(L) == 0);
	return ret;
}
static di_string di_lua_table_to_string(di_object *lua_ref) {
	struct di_lua_ref *t = (struct di_lua_ref *)lua_ref;
	scoped_di_object *state_obj = NULL;
	DI_CHECK_OK(di_get(t, "___di_lua_state", state_obj));

	auto state = (struct di_lua_state *)state_obj;
	lua_State *L = state->L;
	assert(lua_gettop(L) == 0);

	lua_rawgeti(L, LUA_REGISTRYINDEX, t->tref);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		assert(lua_gettop(L) == 0);
		return di_string_printf("<dead lua object %p>", t);
	}

	di_string ret = DI_STRING_INIT;
	ret.data = luaL_tolstring(L, -1, &ret.length);
	if (ret.data == NULL) {
		di_throw(di_new_error("Lua __tostring metamethod didn't return a string"));
	}
	ret = di_clone_string(ret);
	lua_pop(L, 2);        // Pop the table and the string
	assert(lua_gettop(L) == 0);
	return ret;
}
static const char lua_proxy_type[] = "deai.plugin.lua:LuaProxy";
static struct di_lua_ref *lua_type_to_di_object(lua_State *L, int i, void *call) {
	// TODO(yshui): probably a good idea to make sure that same lua object get same
	// di object?
	if (i < 0) {
		i = lua_gettop(L) + i + 1;
	}

	DI_CHECK(lua_istable(L, i) || lua_isfunction(L, i));

	di_object *state;
	di_lua_get_state(L, state);

	// Check the cache first
	lua_pushliteral(L, DEAI_LUA_REGISTRY_OBJECT_CACHE_KEY);
	lua_rawget(L, LUA_REGISTRYINDEX);        // Stack: [... object_cache]
	lua_pushvalue(L, i);                     // Stack: [... object_cache input]
	lua_rawget(L, -2);        // Get object_cache[table] | Stack [ ... object_cache cached_proxy ]
	if (!lua_isnil(L, -1)) {
		assert(di_lua_isproxy(L, -1));
		auto weak = *(di_weak_object **)lua_touserdata(L, -1);
		lua_pop(L, 1);        // Stack: [... object_cache]
		auto obj = di_upgrade_weak_ref(weak);
		if (obj != NULL) {
			lua_pop(L, 1);        // Stack: [...]
			return (struct di_lua_ref *)obj;
		}
	} else {
		lua_pop(L, 1);        // Stack: [... object_cache]
	}

	auto o = di_new_object_with_type(struct di_lua_ref);
	di_set_type((di_object *)o, lua_proxy_type);
	lua_pushvalue(L, i);
	o->tref = luaL_ref(L, LUA_REGISTRYINDEX);
	di_member_clone(o, "___di_lua_state", (di_object *)state);

	auto getter = di_new_object_with_type(di_object);
	di_set_object_call((void *)getter, di_lua_di_getter);
	di_add_member_move((void *)o, di_string_borrow_literal("__get"),
	                   (di_type[]){DI_TYPE_OBJECT}, (void **)&getter);
	auto setter = di_new_object_with_type(di_object);
	di_set_object_call((void *)setter, di_lua_di_setter);
	di_add_member_move((void *)o, di_string_borrow_literal("__set"),
	                   (di_type[]){DI_TYPE_OBJECT}, (void **)&setter);
	di_set_object_dtor((void *)o, (void *)lua_ref_dtor);
	di_set_object_call((void *)o, call);

	di_method(o, "__next", di_lua_di_next, di_string);
	di_method(o, "__to_string", di_lua_table_to_string);

	auto weak = di_weakly_ref_object((di_object *)o);
	lua_pushvalue(L, i);
	di_lua_pushproxy(L, DI_STRING_INIT, weak, di_lua_weak_object_methods, false);
	lua_rawset(L, -3);        // object_cache[input] = weak_proxy
	lua_pop(L, 1);            // Stack: [...]

	return o;
}

static int di_lua_method_handler_impl(lua_State *L, const char *name, di_object *m) {
	// Note lua_error uses setjmp/longjmp, so we can't rely on cleanup
	// attribute here
	if (!di_is_object_callable(m)) {
		return luaL_error(L, "Object %s is not callable\n", name);
	}

	int nargs = lua_gettop(L);

	di_tuple t;
	t.elements = tmalloc(struct di_variant, nargs - 1);
	t.length = nargs - 1;
	// Translate lua arguments
	for (int i = 2; i <= nargs; i++) {
		if (di_lua_type_to_di_variant(L, i, &t.elements[i - 2]) != 0) {
			t.length = i - 2;
			di_free_tuple(t);
			return luaL_argerror(L, i, "Unhandled lua type");
		}
	}

	lua_pop(L, nargs);

	di_value ret;
	di_type rtype;
	di_object *error = NULL;
	int rc = di_call_object_catch(m, &rtype, &ret, t, &error);
	di_free_tuple(t);
	if (rc != 0) {
		return luaL_error(L, "Failed to call function \"%s\": %s", name, strerror(-rc));
	}
	if (error != NULL) {
		di_lua_pushobject(L, di_string_borrow_literal("error"), error);
		return lua_error(L);
	}
	int nret = di_lua_pushvariant(L, DI_STRING_INIT, (struct di_variant){&ret, rtype});
	di_free_value(rtype, &ret);
	return nret;
}

static int di_lua_method_handler(lua_State *L) {
	di_object *m = lua_touserdata(L, lua_upvalueindex(1));
	const char *name = lua_tostring(L, lua_upvalueindex(2));
	return di_lua_method_handler_impl(L, name, m);
}

/// Store a weak reference to the object on the top of the stack in the table at `index`,
/// pops the value.
///
/// This creates an extra weak table indirection, and stores the object in the table. We
/// cannot just use 1 weak table to store all the objects, because the luaL_ref machinary
/// doesn't work with weak tables.
static int luaL_weakref(lua_State *L, int index) {
	lua_newtable(L);        // new_table={}
	lua_newtable(L);        // metatable={}
	lua_pushliteral(L, "__mode");
	lua_pushliteral(L, "v");
	lua_rawset(L, -3);              // metatable.__mode='v'
	lua_setmetatable(L, -2);        // setmetatable(new_table,metatable)
	lua_pushvalue(L, -2);           // push the previous top of stack
	lua_rawseti(L, -2, 1);          // new_table[1]=value, pops the value

	lua_remove(L, -2);        // removes the original top of stack

	// Now new_table is on top of the stack
	return luaL_ref(L, index);
}

/// Get the weak reference `r` from the table at `index`. Leaves the value at the top of
/// the stack. Value `nil` will be at the top of the stack if the weak ref is dead.
///
/// Returns whether the weak reference is still alive
static bool luaL_weakref_get(lua_State *L, int index, int r) {
	// Get the weak table indirection
	lua_rawgeti(L, index, r);

	DI_CHECK(lua_type(L, -1) == LUA_TTABLE);
	lua_rawgeti(L, -1, 1);

	// Remove the weak table indirection from the stack
	lua_remove(L, -2);

	return !lua_isnil(L, -1);
}

static int di_lua_gc(lua_State *L) {
	void **optr = di_lua_checkproxy(L, 1);
	di_object *o = *optr;
	struct di_lua_state *s;

	di_lua_get_state(L, s);
	DI_CHECK(s != NULL);

	// Forget about this object
	scoped_di_string userdata_key = di_string_printf("___lua_userdata_to_object_%p", optr);
	DI_CHECK_OK(di_delete_member_raw((di_object *)s, userdata_key));

	// Check if ___di_object_<object> is still pointing to this proxy. If that's the
	// case, remove this entry. Otherwise it means the weak ref has died before gc and
	// di_lua_pushobject created a new one (see :ref:`lua quirk`).
	int64_t lua_ref;
	scoped_di_string ref_key = di_string_printf("___di_object_to_ref_%p", o);
	if (di_rawget_borrowed2(s, ref_key, lua_ref) != 0) {
		// This means the newer proxy got GC'd before us, the older one.
		return 0;
	}

	// Check the userdata pointer store in the registry. If it has already died (i.e.
	// weakref_get returning false), we know it's ourself; otherwise load the one in
	// the registry.
	void **current_optr = optr;
	if (luaL_weakref_get(L, LUA_REGISTRYINDEX, (int)lua_ref)) {
		current_optr = di_lua_checkproxy(L, -1);
	}
	if (current_optr == optr) {
		DI_CHECK_OK(di_delete_member_raw((void *)s, ref_key));
	}
	return 0;
}

static int di_lua_meta_to_string(lua_State *L) {
	if (!di_lua_isproxy(L, 1)) {
		return luaL_argerror(L, 1, "not a di_object");
	}
	di_object *error = NULL;
	di_object *o = di_ref_object(*(di_object **)lua_touserdata(L, 1));
	lua_pop(L, 1);

	scoped_di_string str = di_object_to_string(o, &error);
	di_unref_object(o);

	if (error != NULL) {
		di_lua_pushobject(L, DI_STRING_INIT, error);
		return lua_error(L);
	}
	lua_pushlstring(L, str.data, str.length);
	return 1;
}

static int di_lua_gc_for_weak_object(lua_State *L) {
	struct di_weak_object *weak = *di_lua_checkproxy(L, 1);
	di_drop_weak_ref(&weak);
	return 0;
}

static const luaL_Reg di_lua_object_methods[] = {
    {"__index", di_lua_meta_index},        {"__newindex", di_lua_meta_newindex},
    {"__pairs", di_lua_meta_pairs},        {"__gc", di_lua_gc},
    {"__tostring", di_lua_meta_to_string}, {0, 0},
};

static const luaL_Reg di_lua_weak_object_methods[] = {
    {"__index", di_lua_meta_index_for_weak_object},
    {"__gc", di_lua_gc_for_weak_object},
    {0, 0},
};

// Expected stack layout:
//
// | name of the object |   -- if callable == true
// | the object (light) |   -- for use with method_handler closure, if callable == true
// |  the object (full) |
static void
di_lua_create_metatable_for_object(lua_State *L, const luaL_Reg *reg, bool callable) {
	if (callable) {
		lua_pushcclosure(L, di_lua_method_handler, 2);
	}

	// Stack layout here:
	//
	// |      closure      |   -- if callable == true
	// | the object (full) |
	lua_newtable(L);
	luaL_setfuncs(L, reg, 0);
	lua_pushliteral(L, "__is_deai_proxy");
	lua_pushboolean(L, true);
	lua_rawset(L, -3);

	if (callable) {
		// Stack: [ metatable, closure, object ]
		lua_insert(L, -2);
		lua_pushliteral(L, "__call");
		lua_insert(L, -2);
		// Stack: [ closure, "__call", metatable, object ]
		lua_rawset(L, -3);
	}

	// Stack: [ metatable, object ]
	lua_setmetatable(L, -2);
}

// Push a proxy for `o` to lua stack. `o` can be a pointer to anything
static void **
di_lua_pushproxy(lua_State *L, di_string name, void *o, const luaL_Reg *reg, bool callable) {
	// struct di_lua_script *s;
	void **ptr;
	ptr = lua_newuserdata(L, sizeof(void *));
	*ptr = o;

	if (callable) {
		lua_pushlightuserdata(L, o);
		if (name.length) {
			lua_pushlstring(L, name.data, name.length);
		} else {
			lua_pushstring(L, "(anonymous)");
		}
	}
	di_lua_create_metatable_for_object(L, reg, callable);
	return ptr;
}

/// Push an object to lua stack. A wrapper of di_lua_pushproxy, which also handles
/// deduplication of objects, and keeping track of object references.
///
/// This function consume the reference to `obj`
static void di_lua_pushobject(lua_State *L, di_string name, di_object *obj) {
	// TODO: if the object comes from a lua object, push the original lua object.
	struct di_lua_state *s;
	di_lua_get_state(L, s);

	int64_t lua_ref;

	scoped_di_string ref_key = di_string_printf("___di_object_to_ref_%p", obj);
	int rc = di_get2(s, ref_key, lua_ref);

	if (rc == 0) {
		if (luaL_weakref_get(L, LUA_REGISTRYINDEX, lua_ref)) {
			// We have already pushed this object before, return the same proxy
			di_unref_object(obj);
			return;
		}
		// .. _lua quirk:
		// The weak reference to this proxy died before __gc is called for it.
		lua_pop(L, 1);        // pop the nil
	}

	// Push the proxy, and weakly reference it from the lua registry
	void **userdata = di_lua_pushproxy(L, name, obj, di_lua_object_methods, true);
	// Copy the proxy, as we are going to consume it when we put it into the registry
	lua_pushvalue(L, -1);
	lua_ref = luaL_weakref(L, LUA_REGISTRYINDEX);

	// Store or update the object to lua ref map
	DI_CHECK_OK(di_setx((void *)s, ref_key, DI_TYPE_INT, &lua_ref, NULL));

	// Update userdata -> object map
	scoped_di_string userdata_key = di_string_printf("___lua_userdata_to_object_%p", userdata);
	DI_CHECK_OK(di_add_member_move((void *)s, userdata_key, (di_type[]){DI_TYPE_OBJECT}, &obj));
}

const char *allowed_os[] = {"time", "difftime", "clock", "tmpname", "date", NULL};

// the "di" global variable doesn't care about __gc
const luaL_Reg di_lua_di_methods[] = {
    {"__index", di_lua_meta_index},
    {"__newindex", di_lua_meta_newindex},
    {0, 0},
};

static void lua_state_dtor(di_object *obj_) {
	auto obj = (struct di_lua_state *)obj_;
	lua_close(obj->L);
	obj->L = NULL;
}

static di_lua_state *lua_new_state(struct di_module *m) {
	auto L = di_new_object_with_type(struct di_lua_state);
	di_set_type((di_object *)L, "deai.plugin.lua:LuaState");
	L->L = luaL_newstate();
	di_set_object_dtor((void *)L, (void *)lua_state_dtor);
	luaL_openlibs(L->L);

	di_object *di = (void *)di_module_get_deai(m);
	di_lua_pushproxy(L->L, di_string_borrow_literal("di"), di, di_lua_di_methods, false);
	lua_setglobal(L->L, "di");

	// The reference from di_lua_state to di is actually kept by the lua_State,
	// as "di" is a global in the lua_State. However, we keep di as a member of
	// di_lua_state to take advantage of the automatic memory management.
	di_member(L, DEAI_MEMBER_NAME_RAW, di);

	// Prevent the script from using os
	lua_getglobal(L->L, "os");
	lua_createtable(L->L, 0, 0);
	for (int i = 0; allowed_os[i]; i++) {
		lua_pushstring(L->L, allowed_os[i]);
		lua_pushstring(L->L, allowed_os[i]);
		lua_rawget(L->L, -4);
		lua_rawset(L->L, -3);
	}
	lua_setglobal(L->L, "os");
	lua_pop(L->L, 1);

	auto Lo = di_weakly_ref_object((di_object *)L);
	di_member(m, "__lua_state", Lo);

	// Store the state object in the lua registry
	lua_pushliteral(L->L, DI_LUA_REGISTRY_STATE_OBJECT_KEY);
	lua_pushlightuserdata(L->L, L);
	lua_rawset(L->L, LUA_REGISTRYINDEX);

	// Create the object cache table
	lua_pushliteral(L->L, DEAI_LUA_REGISTRY_OBJECT_CACHE_KEY);
	lua_newtable(L->L);
	lua_newtable(L->L);        // Stack: [ regkey object_cache, metatable ]
	lua_pushliteral(L->L, "__mode");
	lua_pushliteral(L->L, "k");
	lua_rawset(L->L, -3);              // metatable.__mode = "k"
	lua_setmetatable(L->L, -2);        // setmetatable(object_cache, metatable)
	lua_rawset(L->L, LUA_REGISTRYINDEX);        // registry[DEAI_LUA_REGISTRY_OBJECT_CACHE_KEY] = object_cache
	assert(lua_gettop(L->L) == 0);

	return L;
}

define_object_cleanup(di_lua_state);

/// Convert N values from the lua stack to a di_tuple, starting from index `index`, ending
/// at the top of the stack. Values are _NOT_ popped from the stack.
static di_tuple di_lua_values_to_di_tuple(lua_State *L, int index) {
	di_tuple t;
	auto count = lua_gettop(L) - index + 1;
	t.length = 0;
	t.elements = tmalloc(struct di_variant, count);
	for (int i = 0; i < count; i++) {
		if (di_lua_type_to_di(L, i + index, DI_TYPE_ANY, &t.elements[t.length].type, NULL) != 0) {
			continue;
		}
		if (t.elements[t.length].type == DI_TYPE_NIL) {
			t.elements[t.length++].value = NULL;
			continue;
		}
		t.elements[t.length].value = malloc(di_sizeof_type(t.elements[t.length].type));
		if (di_lua_type_to_di(L, i + index, DI_TYPE_ANY, &t.elements[t.length].type,
		                      t.elements[t.length].value) != 0) {
			free(t.elements[t.length].value);
			continue;
		}
		t.length++;
	}
	return t;
}

/// Load a lua script
///
/// EXPORT: lua.load_script(path: :string): :tuple
///
/// Arguments:
///
/// - path path to the script
///
/// Load and execute a lua script. Returns whatever the script returns as a tuple.
static di_tuple di_lua_load_script(di_object *obj, di_string path_) {
	/**
	 * Reference count scheme for di_lua_script:
	 *
	 * 1) 1 reference is held when this function returns
	 * 2) Each listener adds 1 to reference count (i.e. script object
	 *    is kept alive by either external references or listeners).
	 * 3) If the lua module is freed, all listeners owned by
	 *    di_lua_script will be freed, then the ref counts will
	 *    reflect only external references. Also script object will
	 *    become defunct
	 */
	if (!path_.data) {
		log_error("Path is null");
		return DI_TUPLE_INIT;
	}

	scopedp(char) *path = di_string_to_chars_alloc(path_);
	struct di_module *m = (void *)obj;
	scopedp(di_lua_state) *L = NULL;
	{
		scoped_di_weak_object *weak_lua_state = NULL;

		int rc = di_get(m, "__lua_state", weak_lua_state);
		if (rc == 0) {
			L = (struct di_lua_state *)di_upgrade_weak_ref(weak_lua_state);
		} else {
			DI_CHECK(rc == -ENOENT);
		}

		if (L == NULL) {
			// __lua_state not found, or lua_state has been dropped
			di_delete_member_raw((di_object *)m, di_string_borrow_literal("__lua_state"));
			L = lua_new_state(m);
		}
		DI_CHECK(L != NULL);
	}

	lua_pushstring(L->L, path);
	lua_pushcclosure(L->L, di_lua_errfunc, 1);

	if (luaL_loadfile(L->L, path)) {
		const char *err = lua_tostring(L->L, -1);
		log_error("Failed to load lua script %s: %s\n", path, err);
		lua_pop(L->L, 2);
		assert(lua_gettop(L->L) == 0);
		return DI_TUPLE_INIT;
	}

	int ret;
	ret = lua_pcall(L->L, 0, LUA_MULTRET, -2);

	// Remove the errfunc
	lua_remove(L->L, 1);
	di_tuple func_ret = DI_TUPLE_INIT;
	if (ret == 0) {
		func_ret = di_lua_values_to_di_tuple(L->L, 1);
		lua_pop(L->L, lua_gettop(L->L));
	}

	DI_CHECK_OK(luaL_loadstring(L->L, "collectgarbage()"));
	DI_CHECK_OK(lua_pcall(L->L, 0, 0, 0));

	if (ret != 0) {
		// Right now there's no way to revert what this script
		// have done. (e.g. add listeners). So there's not much
		// we can do here except unref and return an error object

		// The error created by di_lua_errfunc must be an object
		di_type err_type;
		di_value err;
		DI_CHECK_OK(di_lua_type_to_di(L->L, -1, DI_TYPE_ANY, &err_type, &err));
		DI_CHECK(err_type == DI_TYPE_OBJECT);
		lua_pop(L->L, 1);
		assert(lua_gettop(L->L) == 0);
		di_throw(err.object);
	}
	assert(lua_gettop(L->L) == 0);
	return func_ret;
}

static int
di_lua_table_to_array(lua_State *L, int index, int nelem, di_type elemt, di_array *ret) {
	if (nelem == 0) {
		ret->length = 0;
		ret->arr = NULL;
		ret->elem_type = elemt;
		return 0;
	}
	ret->elem_type = elemt;

	size_t sz = di_sizeof_type(elemt);
	assert(sz != 0 || nelem == 0);
	ret->arr = calloc(nelem, sz);

	for (int i = 1; i <= nelem; i++) {
		di_type t;
		lua_rawgeti(L, index, i);

		di_value retd;
		di_lua_type_to_di(L, -1, elemt, &t, &retd);
		lua_pop(L, 1);
		if (t != elemt) {
			// Auto convert int to double
			assert(t == DI_TYPE_INT && elemt == DI_TYPE_FLOAT);
			((double *)ret->arr)[i - 1] = retd.int_;
		} else {
			memcpy(ret->arr + sz * (i - 1), &retd, sz);
		}
	}
	ret->length = nelem;
	return 0;
}

/* Check if a lua table is an array by lua's convention
 * i.e. Whether the only keys of the table are 1, 2, 3, ..., n; and all of the values are
 * of the same type. Exception: for empty table, this function returns true, in order to
 * distinguish it from other kind of non-array tables.
 *
 * @param[out] elemt  if the table is an array, return the element type
 * @param[out] nelemt if the table is an array, return the number of elements
 * @return Whether the table is an array
 */
static bool di_lua_checkarray(lua_State *L, int index, int *nelem, di_type *elemt) {
	if (index < 0) {
		index = lua_gettop(L) + index + 1;
	}
	lua_pushnil(L);
	if (lua_next(L, index) == 0) {
		// Empty array
		*elemt = DI_TYPE_ANY;
		*nelem = 0;
		return true;
	}

	int i = 1;

	// get arr[1]
	lua_rawgeti(L, index, i++);

	// get the type of the first element
	di_value ret;
	di_lua_type_to_di(L, -1, DI_TYPE_ANY, elemt, &ret);
	di_free_value(*elemt, &ret);
	// Pop 2 value, top of stack is the key
	lua_pop(L, 2);

	// We already ruled out empty tables
	assert(*elemt != DI_TYPE_UINT);
	if (*elemt < 0 || *elemt == DI_TYPE_NIL) {
		// Unsupported element types
		lua_pop(L, 1);
		return false;
	}

	while (lua_next(L, index) != 0) {
		lua_rawgeti(L, index, i++);

		// Get the type of the i-th element
		di_type t;
		di_lua_type_to_di(L, -1, *elemt, &t, &ret);
		di_free_value(t, &ret);
		// pop 2 value (lua_next and lua_rawgeti)
		lua_pop(L, 2);

		if (t != *elemt) {
			if (t == DI_TYPE_FLOAT && *elemt == DI_TYPE_INT) {
				*elemt = DI_TYPE_FLOAT;
			} else if ((t == DI_TYPE_ARRAY || t == DI_TYPE_TUPLE) &&
			           *elemt == DI_TYPE_EMPTY_OBJECT) {
				// Empty objects can be treated as empty arrays/tuples
				*elemt = t;
			} else if (t != DI_TYPE_INT || *elemt != DI_TYPE_FLOAT) {
				// Non-uniform element type, cannot be an array
				// pop 1 key
				lua_pop(L, 1);
				return false;
			}
		}

		if (i == INT_MAX) {
			// Array too big
			lua_pop(L, 1);
			break;
		}
	}

	*nelem = i - 1;
	return true;
}

static int call_lua_function(di_object *ref_, di_type *rt, di_value *ret, di_tuple t) {
	auto ref = (struct di_lua_ref *)ref_;
	struct di_variant *vars = t.elements;

	scoped_di_object *state_obj = NULL;
	DI_CHECK_OK(di_get(ref, "___di_lua_state", state_obj));

	auto state = (struct di_lua_state *)state_obj;
	lua_State *L = state->L;
	assert(lua_gettop(L) == 0);

	lua_pushcfunction(L, di_lua_errfunc);

	// Get the function
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref->tref);
	// Push arguments
	for (unsigned int i = 0; i < t.length; i++) {
		di_lua_pushvariant(L, DI_STRING_INIT, vars[i]);
	}

	if (lua_pcall(L, t.length, 1, -(int)t.length - 2) != 0) {
		di_type err_type;
		di_value err;
		DI_CHECK_OK(di_lua_type_to_di(L, -1, DI_TYPE_ANY, &err_type, &err));
		DI_CHECK(err_type == DI_TYPE_OBJECT);
		lua_pop(L, 2);        // Pop err and errfunc
		*rt = DI_TYPE_NIL;
		assert(lua_gettop(L) == 0);
		di_throw(err.object);
	} else {
		di_lua_type_to_di(L, -1, DI_TYPE_ANY, rt, ret);
	}

	lua_pop(L, 2);        // Pop (error or result) + errfunc

	DI_CHECK_OK(luaL_loadstring(L, "collectgarbage(\"step\", 20)"));
	DI_CHECK_OK(lua_pcall(L, 0, 0, 0));
	assert(lua_gettop(L) == 0);
	return 0;
}

/// Convert lua value at index `i` to a deai value.
/// The value is not popped. If `ret` is NULL, the value is not returned, but the type
/// will always be returned. The returned di value will have ownership of whatever
/// resources the lua value has.
static int di_lua_type_to_di(lua_State *L, int i, di_type type_hint, di_type *t, di_value *ret) {
#define ret_arg(i, field, gfn)                                                           \
	do {                                                                                 \
		*t = di_typeof(ret->field);                                                      \
		if (ret != NULL) {                                                               \
			ret->field = gfn(L, i);                                                      \
		}                                                                                \
		return 0;                                                                        \
	} while (0)
#define tostringdup(L, i) strdup(lua_tostring(L, i))
#define todiobj(L, i) (di_object *)lua_type_to_di_object(L, i, call_lua_function)
#define toobjref(L, i)                                                                   \
	({                                                                                   \
		di_object *x = *(void **)lua_touserdata(L, i);                                   \
		di_ref_object(x);                                                                \
		x;                                                                               \
	})
	int nelem;
	di_type elemt;
	switch (lua_type(L, i)) {
	case LUA_TBOOLEAN:
		ret_arg(i, bool_, lua_toboolean);
	case LUA_TNUMBER:
		if (lua_isinteger(L, i)) {
			ret_arg(i, int_, lua_tointeger);
		} else {
			ret_arg(i, float_, lua_tonumber);
		}
	case LUA_TSTRING:;
		if (ret) {
			size_t length;
			const char *tmp = lua_tolstring(L, i, &length);
			ret->string = di_string_ndup(tmp, length);
		}
		*t = DI_TYPE_STRING;
		return 0;
	case LUA_TUSERDATA:
		if (!di_lua_isproxy(L, i)) {
			goto type_error;
		}
		ret_arg(i, object, toobjref);
	case LUA_TTABLE:;
		// Non-array tables, and tables with metatable should become an di_object
		bool has_metatable = lua_getmetatable(L, i);
		if (has_metatable) {
			lua_pop(L, 1);        // pop the metatable
		}
		if (!di_lua_checkarray(L, i, &nelem, &elemt) || has_metatable) {
			*t = DI_TYPE_OBJECT;
			if (ret != NULL) {
				ret->object = (void *)lua_type_to_di_object(L, i, NULL);
			}
		} else {
			// Empty table are treated differently, because empty tables can
			// either be interpreted as an empty array or an empty table. This
			// is an inherent ambiguity in lua. And since emtpy arrays can be
			// represented in deai as empty di_arrays or di_tuples, whereas
			// an empty table should be represented as an di_object.
			// So we creates a special type DI_TYPE_EMPTY_OBJECT for this case.
			if (!nelem && type_hint != DI_TYPE_ARRAY) {
				assert(elemt == DI_TYPE_ANY);
				*t = DI_TYPE_EMPTY_OBJECT;
				if (ret) {
					ret->object = (void *)lua_type_to_di_object(L, i, NULL);
				}
			} else {
				*t = DI_TYPE_ARRAY;
				if (ret) {
					di_lua_table_to_array(L, i, nelem, elemt, &ret->array);
				}
			}
		}
		return 0;
	case LUA_TFUNCTION:
		ret_arg(i, object, todiobj);
	case LUA_TNIL:
		*t = DI_TYPE_NIL;
		return 0;
	type_error:
	default:
		*t = DI_TYPE_NIL;
		return -ENOTSUP;
	}
#undef ret_arg
#undef tostringdup
#undef todiobj
}

struct di_lua_listen_handle_proxy {
	/// The source of the event
	uint64_t root_handle_for_listen_handle;
	uint64_t root_handle_for_source;
	// Whether a once signal wrapper is using this proxy struct
	bool once_wrapper_alive : 1;
	// Whether this proxy struct is in use by the lua script
	bool lua_object_alive : 1;
};

struct di_signal_handler_wrapper {
	di_object;
	struct di_lua_listen_handle_proxy *listen_handle;
};

static int call_lua_signal_handler_once(di_object *obj, di_type *rt, di_value *ret, di_tuple t);
/// EXPORT: deai.plugin.lua:Proxy.on(signal: :string, callback): deai:ListenHandle
///
/// Listen for signals
///
/// Returns a handle. The handle can be used to stop the signal listener, by calling the
/// "stop" method.
///
/// If the handle is garbage collected, the listener will be left running forever.
///
/// EXPORT: deai.plugin.lua:Proxy.once(signal: :string, callback): deai:ListenHandle
///
/// Listen for signals only once
///
/// Same as :lua:meth:`on`, except the callback will only be called for the first time the
/// signal is received.
static int di_lua_add_listener(lua_State *L) {
	// Stack: [ object, string, lua closure ]
	bool once = lua_toboolean(L, lua_upvalueindex(1));
	if (lua_gettop(L) != 3) {
		return luaL_error(L, "'on' takes 3 arguments");
	}
	if (lua_type(L, 3) != LUA_TFUNCTION) {
		return luaL_argerror(L, 3, "not a function");
	}
	if (lua_type(L, 2) != LUA_TSTRING) {
		return luaL_argerror(L, 2, "not a string");
	}
	if (!di_lua_isproxy(L, 1)) {
		return luaL_argerror(L, 1, "not a di object");
	}

	di_object *listen_handle = NULL, *error = NULL;

	// Create a scope so things are properly freed before we touch dangerous lua_error.
	{
		scoped_di_object *o = di_ref_object(*(di_object **)lua_touserdata(L, 1));
		scoped_di_string signame;
		signame.data = lua_tolstring(L, 2, &signame.length);
		signame = di_clone_string(signame);

		scoped_di_object *handler =
		    (di_object *)lua_type_to_di_object(L, -1, call_lua_function);

		if (once) {
			auto wrapped_handler = di_new_object_with_type2(
			    struct di_signal_handler_wrapper, "deai.plugin.lua:OnceSignalHandler");
			di_set_object_call((di_object *)wrapped_handler, call_lua_signal_handler_once);
			di_member(wrapped_handler, "wrapped", handler);
			handler = (di_object *)wrapped_handler;
		}

		lua_pop(L, 3);        // Pop arguments
		listen_handle = di_listen_to(o, signame, (void *)handler, &error);
		assert((error == NULL) != (listen_handle == NULL));
		if (once && listen_handle) {
			di_member_clone(handler, "listen_handle", listen_handle);
		}
	}

	if (error != NULL) {
		di_lua_pushobject(L, di_string_borrow_literal("error"), error);
		return lua_error(L);
	}

	di_lua_pushobject(L, DI_STRING_INIT, listen_handle);
	return 1;
}

/// Push a variant value onto the lua stack. Since lua is a dynamically typed language,
/// this variant is "unpacked" into the actual value, instead of pushed as a proxy object.
/// var.value is not freed by this function, it is cloned when needed, so it's safe to
/// free it after this call.
static int di_lua_pushvariant(lua_State *L, di_string name, struct di_variant var) {
	// Check for nil
	if (var.type == DI_TYPE_OBJECT || var.type == DI_TYPE_STRING || var.type == DI_TYPE_POINTER) {
		// TODO(yshui) objects and strings cannot be NULL
		void *ptr = var.value->pointer;
		if (ptr == NULL) {
			lua_pushnil(L);
			return 1;
		}
	}

	if (var.type == DI_TYPE_ARRAY) {
		if (var.value->array.elem_type == DI_TYPE_ANY) {
			lua_createtable(L, 0, 0);
			return 1;
		}
	}

	int b;
	lua_Integer i;
	lua_Number n;
	di_array *arr;
	di_tuple *tuple;
	struct di_weak_object *weak;
	int step;
	switch (var.type) {
	case DI_TYPE_NUINT:
		i = var.value->nuint;
		goto pushint;
	case DI_TYPE_UINT:
		i = var.value->uint;
		goto pushint;
	case DI_TYPE_NINT:
		i = var.value->nint;
		goto pushint;
	case DI_TYPE_INT:
		i = var.value->int_;
		goto pushint;
	case DI_TYPE_FLOAT:
		n = var.value->float_;
		goto pushnumber;
	case DI_TYPE_POINTER:
		// bad idea
		lua_pushlightuserdata(L, var.value->pointer);
		return 1;
	case DI_TYPE_EMPTY_OBJECT:
	case DI_TYPE_OBJECT:
		di_ref_object(var.value->object);
		di_lua_pushobject(L, name, var.value->object);
		return 1;
	case DI_TYPE_WEAK_OBJECT:
		di_copy_value(DI_TYPE_WEAK_OBJECT, &weak, &var.value->weak_object);
		di_lua_pushproxy(L, name, weak, di_lua_weak_object_methods, false);
		return 1;
	case DI_TYPE_STRING:
		lua_pushlstring(L, var.value->string.data, var.value->string.length);
		return 1;
	case DI_TYPE_STRING_LITERAL:
		lua_pushstring(L, var.value->string_literal);
		return 1;
	case DI_TYPE_ARRAY:
		arr = &var.value->array;
		step = di_sizeof_type(arr->elem_type);
		lua_createtable(L, arr->length, 0);
		for (int i = 0; i < arr->length; i++) {
			di_lua_pushvariant(L, DI_STRING_INIT,
			                   (struct di_variant){arr->arr + step * i, arr->elem_type});
			lua_rawseti(L, -2, i + 1);
		}
		return 1;
	case DI_TYPE_TUPLE:
		tuple = &var.value->tuple;
		lua_createtable(L, tuple->length, 0);
		for (int i = 0; i < tuple->length; i++) {
			di_lua_pushvariant(L, DI_STRING_INIT, tuple->elements[i]);
			lua_rawseti(L, -2, i + 1);
		}
		return 1;
	case DI_TYPE_VARIANT:
		return di_lua_pushvariant(L, DI_STRING_INIT, var.value->variant);
	case DI_TYPE_BOOL:
		b = var.value->bool_;
		lua_pushboolean(L, b);
		return 1;
	case DI_TYPE_NIL:
		lua_pushnil(L);
		return 0;
	case DI_TYPE_ANY:
	case DI_LAST_TYPE:
		DI_ASSERT(false, "Value with invalid type");
		return 0;
	}
	DI_CHECK(false);

pushint:
	lua_pushinteger(L, i);
	return 1;
pushnumber:
	lua_pushnumber(L, n);
	return 1;
}

// Stack: [ object, string (signal name), arguments... ]
int di_lua_emit_signal(lua_State *L) {
	if (lua_gettop(L) < 2) {
		return luaL_error(L, "emit_signal requires at least 2 arguments");
	}
	if (!di_lua_isproxy(L, 1)) {
		return luaL_argerror(L, 1, "not a di object");
	}
	if (!lua_isstring(L, 2)) {
		return luaL_argerror(L, 2, "not a string");
	}

	int rc = 0;
	// Create a scope so things are freed before we call lua_error
	{
		scoped_di_string signame = DI_STRING_INIT;
		signame.data = lua_tolstring(L, 2, &signame.length);
		signame = di_clone_string(signame);

		scoped_di_object *o = di_ref_object(*(di_object **)lua_touserdata(L, 1));
		int top = lua_gettop(L);

		scoped_di_tuple t = {
		    .elements = tmalloc(struct di_variant, top - 2),
		};

		for (int i = 3; i <= top; i++) {
			rc = di_lua_type_to_di_variant(L, i, &t.elements[i - 3]);
			if (rc != 0) {
				break;
			}
			t.length = i - 2;
		}

		lua_pop(L, top);
		if (rc == 0) {
			rc = di_emitn(o, signame, t);
		}
		if (rc != 0) {
			lua_pushfstring(L, "Failed to emit signal %.*s", (int)signame.length, signame.data);
		}
	}

	if (rc != 0) {
		return lua_error(L);
	}
	return 0;
}

static int di_lua_upgrade_weak_ref(lua_State *L) {
	struct di_weak_object *weak = *di_lua_checkproxy(L, 1);
	di_object *strong = di_upgrade_weak_ref(weak);
	if (strong == NULL) {
		return 0;
	}

	di_lua_pushobject(L, DI_STRING_INIT, strong);
	return 1;
}

// Stack: [ object ]
static int di_lua_weak_ref(lua_State *L) {
	di_object *strong = *di_lua_checkproxy(L, 1);
	di_value weak = {.weak_object = di_weakly_ref_object(strong)};
	int nret = di_lua_pushvariant(
	    L, DI_STRING_INIT, (struct di_variant){.type = DI_TYPE_WEAK_OBJECT, .value = &weak});
	di_drop_weak_ref(&weak.weak_object);
	return nret;
}

static int di_lua_meta_index_for_weak_object(lua_State *L) {
	/* This is __index for lua di_weak_object proxies. Weak object reference proxies
	 * only have one method, `upgrade()`, to retrieve a strong object reference
	 */

	if (lua_gettop(L) != 2) {
		return luaL_error(L, "wrong number of arguments to __index");
	}

	const char *key = luaL_checklstring(L, 2, NULL);

	if (strcmp(key, "upgrade") == 0) {
		lua_pushcclosure(L, di_lua_upgrade_weak_ref, 0);
		return 1;
	}

	return 0;
}

static int
call_lua_signal_handler_once(di_object *obj, di_type *rt, di_value *ret, di_tuple t) {
	scoped_di_object *handler = NULL;
	scoped_di_object *listen_handle = NULL;
	DI_CHECK_OK(di_get(obj, "wrapped", handler));
	DI_CHECK_OK(di_get(obj, "listen_handle", listen_handle));

	// Stop the listener first, in case the signal is emitted again during the
	// handler call.
	DI_CHECK_OK(di_call(listen_handle, "stop"));
	return di_call_object(handler, rt, ret, t);
}

static di_variant di_lua_globals_getter(di_object *globals, di_string key) {
	scopedp(di_object) *m = NULL;
	di_get(globals, "___di_lua_module", m);

	scoped_di_weak_object *weak_lua_state = NULL;
	scopedp(di_lua_state) *L = NULL;
	int rc = di_get(m, "__lua_state", weak_lua_state);
	if (rc == 0) {
		L = (di_lua_state *)di_upgrade_weak_ref(weak_lua_state);
	} else {
		DI_CHECK(rc == -ENOENT);
		return DI_VARIANT_INIT;
	}
	if (L == NULL) {
		// __lua_state not found, or lua_state has been dropped
		di_delete_member_raw((di_object *)m, di_string_borrow_literal("__lua_state"));
		return DI_VARIANT_INIT;
	}

	scopedp(char) *keystr = di_string_to_chars_alloc(key);
	lua_getglobal(L->L, keystr);

	di_type t;
	di_value *ret = tmalloc(di_value, 1);
	di_lua_type_to_di(L->L, -1, DI_TYPE_ANY, &t, ret);
	lua_pop(L->L, 1);        // Top of stack is the value
	return (struct di_variant){.value = ret, .type = t};
}

/// Get the global variables from loaded lua scripts
///
/// EXPORT: lua.globals: deai.plugin.lua:Globals
///
/// This object is an accessor to all global variables in the loaded lua scripts. Each
/// global variable is accessible as a property with the same name of this object.
static di_object *di_lua_get_globals(di_object *lua) {
	auto g = di_new_object_with_type(di_object);
	di_set_type((di_object *)g, "deai.plugin.lua:Globals");
	di_member_clone(g, "___di_lua_module", lua);
	di_method(g, "__get", di_lua_globals_getter, di_string);
	return (di_object *)g;
}

/// Lua proxy of a deai object
///
/// TYPE: deai.plugin.lua:Proxy
///
/// When you create a deai object, a proxy object is created in lua. Some extra methods
/// are available from these proxies.
struct di_lua_proxy_object {
	// dummy object for documentation purposes
};

static int di_lua_meta_index(lua_State *L) {
	if (lua_gettop(L) != 2) {
		return luaL_error(L, "wrong number of arguments to __index");
	}
	if (!di_lua_isproxy(L, 1)) {
		return luaL_argerror(L, 1, "not a di object");
	}
	if (!lua_isstring(L, 2)) {
		return luaL_argerror(L, 2, "not a string");
	}
	di_object *error = NULL;
	int rc = 0;
	{
		scoped_di_string key = DI_STRING_INIT;
		key.data = lua_tolstring(L, 2, &key.length);
		key = di_clone_string(key);

		scoped_di_object *ud = di_ref_object(*(di_object **)lua_touserdata(L, 1));
		lua_pop(L, 2);        // Pop 2 arguments

		// Handle the special methods
		if (di_string_eq(key, di_string_borrow_literal("on"))) {
			lua_pushboolean(L, false);
			lua_pushcclosure(L, di_lua_add_listener, 1);
			return 1;
		}
		if (di_string_eq(key, di_string_borrow_literal("once"))) {
			lua_pushboolean(L, true);
			lua_pushcclosure(L, di_lua_add_listener, 1);
			return 1;
		}
		if (di_string_eq(key, di_string_borrow_literal("emit"))) {
			lua_pushcclosure(L, di_lua_emit_signal, 0);
			return 1;
		}
		if (di_string_eq(key, di_string_borrow_literal("weakref"))) {
			lua_pushcclosure(L, di_lua_weak_ref, 0);
			return 1;
		}

		di_type rt;
		di_value ret;
		rc = di_getx(ud, key, &rt, &ret, &error);
		if (rc != 0) {
			lua_pushnil(L);
			return 1;
		}
		if (error == NULL) {
			rc = di_lua_pushvariant(L, key, (struct di_variant){&ret, rt});
			di_free_value(rt, &ret);
		}
	}

	if (error != NULL) {
		di_lua_pushobject(L, di_string_borrow_literal("error"), error);
		// lua_error use longjmp, which doesn't work with cleanup attribute.
		return lua_error(L);
	}
	return rc;
}

static int di_lua_meta_newindex(lua_State *L) {
	if (lua_gettop(L) != 3) {
		return luaL_error(L, "wrong number of arguments to __newindex");
	}
	if (!di_lua_isproxy(L, 1)) {
		return luaL_argerror(L, 1, "not a di object");
	}
	if (!lua_isstring(L, 2)) {
		return luaL_argerror(L, 2, "not a string");
	}

	di_type vt;
	di_value val;
	int rc = di_lua_type_to_di(L, 3, DI_TYPE_ANY, &vt, &val);
	if (rc != 0) {
		return luaL_error(L, "unhandled lua type");
	}

	int ret;
	di_object *error = NULL;
	{
		scoped_di_object *ud = di_ref_object(*(di_object **)lua_touserdata(L, 1));
		scoped_di_string key = DI_STRING_INIT;
		key.data = lua_tolstring(L, 2, &key.length);
		key = di_clone_string(key);

		lua_pop(L, 3);        // Pop 3 arguments

		if (vt == DI_TYPE_NIL) {
			ret = di_delete_member(ud, key, &error);
		} else {
			ret = di_setx(ud, key, vt, &val, &error);
			di_free_value(vt, &val);
		}
		// error been thrown means the call must have succeeded
		assert(error == NULL || ret == 0);
		if (error != NULL) {
			di_lua_pushobject(L, di_string_borrow_literal("error"), error);
			ret = -1;
		} else if (ret == -EINVAL) {
			lua_pushfstring(L, "property %s type mismatch", key);
		} else if (ret == -ENOENT) {
			lua_pushfstring(L, "property %s doesn't exist", key);
		} else if (ret != 0) {
			lua_pushfstring(L, "failed to set property %s: %d", key, ret);
		}
	}

	if (ret != 0) {
		return lua_error(L);
	}
	return 0;
}

static int di_lua_meta_next(lua_State *L) {
	// stack: [ object key ]
	if (lua_gettop(L) != 2 && lua_gettop(L) != 1) {
		return luaL_error(L, "wrong number of arguments to __next");
	}
	if (!di_lua_isproxy(L, 1)) {
		return luaL_argerror(L, 1, "not a di object");
	}
	if (lua_gettop(L) == 2 && !lua_isnil(L, 2) && !lua_isstring(L, 2)) {
		return luaL_argerror(L, 2, "not a string or nil");
	}
	scoped_di_object *ud = di_ref_object(*(di_object **)lua_touserdata(L, 1));
	scoped_di_string key = DI_STRING_INIT;
	if (lua_gettop(L) == 2 && !lua_isnil(L, 2)) {
		key.data = lua_tolstring(L, 2, &key.length);
		key = di_clone_string(key);
	}
	lua_pop(L, lua_gettop(L));        // Pop all arguments
	scoped_di_tuple next = di_object_next_member(ud, key);
	if (next.length < 2) {
		return 0;
	}
	di_lua_pushvariant(L, DI_STRING_INIT, next.elements[0]);
	di_lua_pushvariant(L, DI_STRING_INIT, next.elements[1]);
	return 2;
}

static int di_lua_meta_pairs(lua_State *L) {
	if (lua_gettop(L) != 1) {
		return luaL_error(L, "wrong number of arguments to __pairs");
	}

	lua_pushcfunction(L, di_lua_meta_next);        // stack: [ object next ]
	lua_insert(L, 1);                              // stack: [ next object ]
	lua_pushnil(L);                                // stack: [ next object nil ]
	return 3;
}

/// Convert a lua table to a di_object
///
/// EXPORT: lua.as_di_object(obj: :object): :object
///
/// This is intended to be called from lua scripts. It converts a lua table to a
/// di_object. It is kind of useless when called from outside lua, since what it does is
/// just returning the object passed to it. The real magic is the inner workings of the
/// lua plugin which allows external functions to be called from lua scripts.
static di_object *di_lua_as_di_object(di_object * /*lua*/, di_object *obj) {
	// Real magic is done in di_lua_method_handler, which converts the lua table to a
	// di_object. And all we need to do is to return the object.
	return di_ref_object(obj);
}

/// Lua scripting
///
/// EXPORT: lua: deai:module
///
/// **Accessing deai modules**
///
/// All deai modules is available under the global lua table named "di", such as
/// :code:`di.lua`, etc.
///
/// **Representation of deai objects**
///
/// In lua, a :lua:mod:`~deai.plugin.lua.Proxy` is created for each deai objects, which
/// function like normal lua tables. Properties and methods are accessible as table
/// entries. Note methods should be called like a lua method, i.e.
/// :code:`obj:method(...)`.
///
/// **Receiving signals**
///
/// Use the :lua:meth:`~deai.plugin.lua.Proxy.on` methods from the proxy to register
/// listeners on signals.
///
/// **Quirks**
///
/// In lua there's only table, there's no such thing as an array. To decide if a table is
/// an arraay deai will first check if it has integer 1 as a key, then the length of the
/// array is determined to be the largest contiguous integer key following 1, then deai
/// check if all these keys map to elements with the same type. If all passes, then that
/// table becomes a deai array.
///
/// Additionally, we don't know what exactly to do with an empty table. Should it be a
/// table or an array? Right now, it's treated as a table, thus translates to an object in
/// deai. So if you want to pass an empty array as argument, you would have to pass "nil".
static struct di_module *di_new_lua(di_object *di) {
	auto m = di_new_module(di);

	di_method(m, "load_script", di_lua_load_script, di_string);
	di_method(m, "as_di_object", di_lua_as_di_object, di_object *);
	di_getter(m, globals, di_lua_get_globals);

	// Load the builtin lua script. The returned object could safely die. The builtin
	// script should register modules which should keep it alive.
	scoped_di_string resources_dir = DI_STRING_INIT;
	DI_CHECK_OK(di_get(di, "resources_dir", resources_dir));
	scoped_di_string builtin_path = di_string_printf(
	    "%.*s/lua/builtins.lua", (int)resources_dir.length, resources_dir.data);
	scoped_di_tuple ret = di_lua_load_script((void *)m, builtin_path);

	return m;
}
DEAI_PLUGIN_ENTRY_POINT(di) {
	auto m = di_new_lua(di);
	di_register_module(di, di_string_borrow("lua"), &m);
}
