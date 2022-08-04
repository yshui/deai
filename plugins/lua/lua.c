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
#include <deai/helper.h>

#include "compat.h"
#include "config.h"
#include "list.h"
#include "uthash.h"
#include "utils.h"

#define tmalloc(type, nmem) (type *)calloc(nmem, sizeof(type))
#define auto __auto_type

#define DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY "__deai.di_lua.script_object"
#define DI_LUA_REGISTRY_STATE_OBJECT_KEY "__deai.di_lua.state_object"

#define di_lua_get_state(L, s)                                                           \
	do {                                                                             \
		lua_pushliteral((L), DI_LUA_REGISTRY_STATE_OBJECT_KEY);                  \
		lua_rawget((L), LUA_REGISTRYINDEX);                                      \
		(s) = lua_touserdata((L), -1);                                           \
		lua_pop(L, 1);                                                           \
	} while (0)

#define di_lua_get_env(L, s)                                                             \
	do {                                                                             \
		lua_pushliteral((L), DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);                 \
		lua_rawget((L), LUA_REGISTRYINDEX);                                      \
		(s) = lua_touserdata((L), -1);                                           \
		lua_pop(L, 1);                                                           \
	} while (0)

#define di_lua_xchg_env(L, s)                                                            \
	do {                                                                             \
		void *tmp;                                                               \
		di_lua_get_env(L, tmp);                                                  \
		lua_pushliteral((L), DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);                 \
		lua_pushlightuserdata((L), (s));                                         \
		lua_rawset((L), LUA_REGISTRYINDEX);                                      \
		s = tmp;                                                                 \
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

typedef struct di_lua_script {
	di_object;
	char *path;
} di_lua_script;

static int di_lua_pushvariant(lua_State *L, const char *name, struct di_variant var);
static int di_lua_meta_index(lua_State *L);
static int di_lua_meta_index_for_weak_object(lua_State *L);
static int di_lua_meta_newindex(lua_State *L);

static int di_lua_errfunc(lua_State *L) {
	/* Convert error to string, to prevent a follow-up error with lua_concat. */
	auto err = luaL_tolstring(L, -1, NULL);

	lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
	lua_rawget(L, LUA_REGISTRYINDEX);
	struct di_lua_script *o = lua_touserdata(L, -1);

	char *error_prompt = NULL;
	int error_prompt_len;
	error_prompt_len =
	    asprintf(&error_prompt, "Failed to run lua script %s: %s", o->path, err);

	// Get debug.traceback
	lua_getglobal(L, "debug");
	lua_pushstring(L, "traceback");
	lua_gettable(L, -2);

	// Push arguments
	lua_pushlstring(L, error_prompt, error_prompt_len);
	lua_pushinteger(L, 3);
	free(error_prompt);

	// Call debug.traceback(error_prompt, 3), this should leave the error message we
	// want on the top of the stack.
	if (lua_pcall(L, 2, 1, 0) != 0) {
		// If we failed to get a stack trace, we have to generate a generic error
		// message
		auto err2 = luaL_tolstring(L, -1, NULL);
		error_prompt_len = asprintf(&error_prompt,
		                            "Failed to run lua script %s: %s\nstack "
		                            "traceback:\n\t(Failed to generate stack "
		                            "trace: %s)",
		                            o->path, err, err2);
		lua_pushlstring(L, error_prompt, error_prompt_len);
		free(error_prompt);
	}

	return 1;
}

static void di_lua_free_script(struct di_lua_script *s) {
	free(s->path);
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

static void lua_ref_dtor(struct di_lua_ref *t) {
	scoped_di_object *script_obj = NULL;
	scoped_di_object *state_obj = NULL;
	DI_CHECK_OK(di_get(t, "___di_lua_script", script_obj));
	if (di_get(script_obj, "___di_lua_state", state_obj) == 0) {
		// The script object might already be finalized if we are part of
		// a reference cycle.
		auto state = (struct di_lua_state *)state_obj;
		luaL_unref(state->L, LUA_REGISTRYINDEX, t->tref);
	}
}

static int di_lua_type_to_di(lua_State *L, int i, di_type *t, di_value *ret);

static inline int di_lua_type_to_di_variant(lua_State *L, int i, struct di_variant *var) {
	int rc = di_lua_type_to_di(L, i, &var->type, NULL);
	if (rc != 0) {
		return rc;
	}

	var->value = malloc(di_sizeof_type(var->type));
	di_lua_type_to_di(L, i, &var->type, var->value);
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

	scoped_di_object *script_obj = NULL;
	scoped_di_object *state_obj = NULL;
	DI_CHECK_OK(di_get(t, "___di_lua_script", script_obj));
	DI_CHECK_OK(di_get(script_obj, "___di_lua_state", state_obj));

	auto script = (struct di_lua_script *)script_obj;
	auto state = (struct di_lua_state *)state_obj;
	lua_State *L = state->L;
	di_lua_xchg_env(L, script);

	lua_rawgeti(L, LUA_REGISTRYINDEX, t->tref);

	if (vars[1].type == DI_TYPE_STRING) {
		lua_pushlstring(L, vars[1].value->string.data, vars[1].value->string.length);
	} else {
		const char *key = vars[1].value->string_literal;
		lua_pushstring(L, key);
	}
	lua_gettable(L, -2);

	DI_OK_OR_RET(di_lua_type_to_di(L, -1, rt, ret));

	if (*rt == DI_TYPE_NIL) {
		// nil in Lua means non-existent.
		*rt = DI_LAST_TYPE;
	}

	di_lua_xchg_env(L, script);
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

	scoped_di_object *script_obj = NULL;
	scoped_di_object *state_obj = NULL;
	DI_CHECK_OK(di_get(t, "___di_lua_script", script_obj));
	DI_CHECK_OK(di_get(script_obj, "___di_lua_state", state_obj));

	auto script = (struct di_lua_script *)script_obj;
	auto state = (struct di_lua_state *)state_obj;
	lua_State *L = state->L;
	di_lua_xchg_env(L, script);

	lua_rawgeti(L, LUA_REGISTRYINDEX, t->tref);

	if (vars[1].type == DI_TYPE_STRING) {
		lua_pushlstring(L, vars[1].value->string.data, vars[1].value->string.length);
	} else {
		const char *key = vars[1].value->string_literal;
		lua_pushstring(L, key);
	}

	if (di_lua_pushvariant(L, NULL, vars[2]) != 1) {
		lua_pop(L, 2);        // key and table
		return -EINVAL;
	}
	lua_settable(L, -3);
	lua_pop(L, 1);        // table

	di_lua_xchg_env(L, script);
	*rt = DI_TYPE_NIL;
	return 0;
}

static struct di_lua_ref *lua_type_to_di_object(lua_State *L, int i, void *call) {
	// TODO(yshui): probably a good idea to make sure that same lua object get same
	// di object?
	struct di_lua_script *s;

	// retrive the script object from lua registry
	lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
	lua_rawget(L, LUA_REGISTRYINDEX);
	s = lua_touserdata(L, -1);
	lua_pop(L, 1);        // pop the script object

	auto o = di_new_object_with_type(struct di_lua_ref);
	di_set_type((di_object *)o, "deai.plugin.lua:LuaRef");
	o->tref = luaL_ref(L, LUA_REGISTRYINDEX);        // this pops the table from
	                                                 // stack, we need to put it back
	di_member_clone(o, "___di_lua_script", (di_object *)s);

	// Restore the value onto the stack
	lua_pushinteger(L, o->tref);
	lua_rawget(L, LUA_REGISTRYINDEX);

	auto getter = di_new_object_with_type(di_object);
	di_set_object_call((void *)getter, di_lua_di_getter);
	di_add_member_move((void *)o, di_string_borrow("__get"),
	                   (di_type[]){DI_TYPE_OBJECT}, (void **)&getter);
	auto setter = di_new_object_with_type(di_object);
	di_set_object_call((void *)setter, di_lua_di_setter);
	di_add_member_move((void *)o, di_string_borrow("__set"),
	                   (di_type[]){DI_TYPE_OBJECT}, (void **)&setter);
	di_set_object_dtor((void *)o, (void *)lua_ref_dtor);
	di_set_object_call((void *)o, call);

	// Need to return
	return o;
}

static int di_lua_method_handler_impl(lua_State *L, const char *name, di_object *m) {
	if (!di_is_object_callable(m)) {
		return luaL_error(L, "Object %s is not callable\n", name);
	}

	int nargs = lua_gettop(L);

	scoped_di_tuple t;
	t.elements = tmalloc(struct di_variant, nargs - 1);
	t.length = nargs - 1;
	// Translate lua arguments
	for (int i = 2; i <= nargs; i++) {
		if (di_lua_type_to_di_variant(L, i, &t.elements[i - 2]) != 0) {
			return luaL_argerror(L, i, "Unhandled lua type");
		}
	}

	di_value ret;
	di_type rtype;
	int rc = di_call_objectt(m, &rtype, &ret, t);

	if (rc == 0) {
		int nret = di_lua_pushvariant(L, NULL, (struct di_variant){&ret, rtype});
		di_free_value(rtype, &ret);
		return nret;
	}

	return luaL_error(L, "Failed to call function \"%s\": %s", name, strerror(-rc));
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
	char *buf = NULL;
	asprintf(&buf, "___lua_userdata_to_object_%p", optr);
	DI_CHECK_OK(di_remove_member_raw((void *)s, di_string_borrow(buf)));
	free(buf);

	// Check if ___di_object_<object> is still pointing to this proxy. If that's the
	// case, remove this entry. Otherwise it means the weak ref has died before gc and
	// di_lua_pushobject created a new one (see :ref:`lua quirk`).
	int64_t lua_ref;
	asprintf(&buf, "___di_object_to_ref_%p", o);
	if (di_get(s, buf, lua_ref) != 0) {
		// This means the newer proxy got GC'd before us, the older one.
		free(buf);
		return 0;
	}

	// Check the userdata pointer store in the registry. If it has already died (i.e.
	// weakref_get returning false), we know it's ourself; otherwise load the one in
	// the registry.
	void **current_optr = optr;
	if (luaL_weakref_get(L, LUA_REGISTRYINDEX, lua_ref)) {
		current_optr = di_lua_checkproxy(L, -1);
	}
	if (current_optr == optr) {
		DI_CHECK_OK(di_remove_member_raw((void *)s, di_string_borrow(buf)));
	}
	free(buf);
	return 0;
}

static int di_lua_gc_for_weak_object(lua_State *L) {
	struct di_weak_object *weak = *di_lua_checkproxy(L, 1);
	di_drop_weak_ref(&weak);
	return 0;
}

static const luaL_Reg di_lua_object_methods[] = {
    {"__index", di_lua_meta_index},
    {"__newindex", di_lua_meta_newindex},
    {"__gc", di_lua_gc},
    {0, 0},
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
di_lua_pushproxy(lua_State *L, const char *name, void *o, const luaL_Reg *reg, bool callable) {
	// struct di_lua_script *s;
	void **ptr;
	ptr = lua_newuserdata(L, sizeof(void *));
	*ptr = o;

	if (callable) {
		lua_pushlightuserdata(L, o);
		if (name) {
			lua_pushstring(L, name);
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
static void di_lua_pushobject(lua_State *L, const char *name, di_object *obj) {
	// TODO: if the object comes from a lua object, push the original lua object.
	struct di_lua_state *s;
	di_lua_get_state(L, s);

	scopedp(char) * buf1;
	int64_t lua_ref;

	asprintf(&buf1, "___di_object_to_ref_%p", obj);
	int rc = di_get(s, buf1, lua_ref);

	if (rc == 0) {
		if (luaL_weakref_get(L, LUA_REGISTRYINDEX, lua_ref)) {
			// We have already pushed this object before, return the same proxy
			di_unref_object(obj);
			return;
		}
		// .. _lua quirk:
		// The weak reference to this proxy died before __gc is called for it.
	}

	// Push the proxy, and weakly reference it from the lua registry
	void **userdata = di_lua_pushproxy(L, name, obj, di_lua_object_methods, true);
	// Copy the proxy, as we are going to consume it when we put it into the registry
	lua_pushvalue(L, -1);
	lua_ref = luaL_weakref(L, LUA_REGISTRYINDEX);

	// Store or update the object to lua ref map
	DI_CHECK_OK(di_setx((void *)s, di_string_borrow(buf1), DI_TYPE_INT, &lua_ref));

	// Update userdata -> object map
	scopedp(char) * buf2;
	asprintf(&buf2, "___lua_userdata_to_object_%p", userdata);
	DI_CHECK_OK(di_add_member_move((void *)s, di_string_borrow(buf2),
	                               (di_type[]){DI_TYPE_OBJECT}, &obj));
}

const char *allowed_os[] = {"time", "difftime", "clock", "tmpname", "date", NULL};

// the "di" global variable doesn't care about __gc
const luaL_Reg di_lua_di_methods[] = {
    {"__index", di_lua_meta_index},
    {"__newindex", di_lua_meta_newindex},
    {0, 0},
};

static void lua_state_dtor(struct di_lua_state *obj) {
	lua_close(obj->L);
}

static di_lua_state *lua_new_state(struct di_module *m) {
	auto L = di_new_object_with_type(struct di_lua_state);
	di_set_type((di_object *)L, "deai.plugin.lua:LuaState");
	L->L = luaL_newstate();
	di_set_object_dtor((void *)L, (void *)lua_state_dtor);
	luaL_openlibs(L->L);

	di_object *di = (void *)di_module_get_deai(m);
	di_lua_pushproxy(L->L, "di", di, di_lua_di_methods, false);
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

	return L;
}

define_object_cleanup(di_lua_script);
define_object_cleanup(di_lua_state);

/// Load a lua script
///
/// EXPORT: lua.load_script(path: :string): deai.plugin.lua:LuaScript
///
/// Arguments:
///
/// - path path to the script
///
/// Load and execute a lua script. Returns a handle to the script.
static di_object *di_lua_load_script(di_object *obj, di_string path_) {
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
		return di_new_error("Path is null");
	}

	char *path = di_string_to_chars_alloc(path_);
	scopedp(di_lua_script) *s = di_new_object_with_type(struct di_lua_script);
	di_set_type((di_object *)s, "deai.plugin.lua:LuaScript");
	di_set_object_dtor((void *)s, (void *)di_lua_free_script);

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
			di_remove_member_raw((di_object *)m,
			                     di_string_borrow("__lua_state"));
			L = lua_new_state(m);
		}
		DI_CHECK(L != NULL);
	}

	di_mgetm(m, log, di_new_error("Can't find log module"));
	lua_pushcfunction(L->L, di_lua_errfunc);

	if (luaL_loadfile(L->L, path)) {
		const char *err = lua_tostring(L->L, -1);
		di_log_va(logm, DI_LOG_ERROR, "Failed to load lua script %s: %s\n", path, err);
		lua_pop(L->L, 2);
		// Create the error object before freeing the script object. Because after
		// that the error string might be freed.
		auto errobj = di_new_error("Failed to load lua script %s: %s\n", path, err);
		free(path);
		return errobj;
	}

	s->path = path;
	di_member_clone(s, "___di_lua_state", (di_object *)L);

	int ret;
	// load_script might be called by lua script,
	// so preserve the current set script object.
	di_lua_xchg_env(L->L, s);
	ret = lua_pcall(L->L, 0, 0, -2);
	di_lua_xchg_env(L->L, s);

	// Remove the errfunc
	lua_remove(L->L, 1);

	if (ret != 0) {
		// Right now there's no way to revert what this script
		// have done. (e.g. add listeners). So there's not much
		// we can do here except unref and return an error object

		// Pop the error, and converted error string
		auto err = luaL_tolstring(L->L, -1, NULL);
		lua_pop(L->L, 2);
		return di_new_error("%s", err);
	}
	return di_ref_object((di_object *)s);
}

static int
di_lua_table_to_array(lua_State *L, int index, int nelem, di_type elemt, di_array *ret) {
	ret->elem_type = elemt;

	size_t sz = di_sizeof_type(elemt);
	assert(sz != 0 || nelem == 0);
	ret->arr = calloc(nelem, sz);

	for (int i = 1; i <= nelem; i++) {
		di_type t;
		lua_rawgeti(L, index, i);

		di_value retd;
		di_lua_type_to_di(L, -1, &t, &retd);
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
	di_lua_type_to_di(L, -1, elemt, &ret);
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
		di_lua_type_to_di(L, -1, &t, &ret);
		di_free_value(t, &ret);
		// pop 2 value (lua_next and lua_rawgeti)
		lua_pop(L, 2);

		if (t != *elemt) {
			if (t == DI_TYPE_FLOAT && *elemt == DI_TYPE_INT) {
				*elemt = DI_TYPE_FLOAT;
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

static int call_lua_function(struct di_lua_ref *ref, di_type *rt, di_value *ret, di_tuple t) {
	scoped_di_object *script_obj = NULL;
	DI_CHECK_OK(di_get(ref, "___di_lua_script", script_obj));

	struct di_variant *vars = t.elements;

	auto script = (struct di_lua_script *)script_obj;
	scoped_di_object *state_obj = NULL;
	DI_CHECK_OK(di_get(script, "___di_lua_state", state_obj));

	auto state = (struct di_lua_state *)state_obj;
	lua_State *L = state->L;

	lua_pushcfunction(L, di_lua_errfunc);

	di_lua_xchg_env(L, script);

	// Get the function
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref->tref);
	// Push arguments
	for (unsigned int i = 0; i < t.length; i++) {
		di_lua_pushvariant(L, NULL, vars[i]);
	}

	if (lua_pcall(L, t.length, 1, -(int)t.length - 2) != 0) {
		auto err = luaL_tolstring(L, -1, NULL);
		lua_pop(L, 1);        // Pop the converted error string
		ret->object = di_new_error("%s", err);
		*rt = DI_TYPE_OBJECT;
	} else {
		di_lua_type_to_di(L, -1, rt, ret);
	}

	lua_pop(L, 2);        // Pop (error or result) + errfunc

	di_lua_xchg_env(L, script);

	return 0;
}

/// Convert lua value at index `i` to a deai value.
/// The value is not popped. If `ret` is NULL, the value is not returned, but the type
/// will always be returned
static int di_lua_type_to_di(lua_State *L, int i, di_type *t, di_value *ret) {
#define ret_arg(i, field, gfn)                                                           \
	do {                                                                             \
		*t = di_typeof(ret->field);                                              \
		if (ret != NULL) {                                                       \
			ret->field = gfn(L, i);                                          \
		}                                                                        \
		return 0;                                                                \
	} while (0)
#define tostringdup(L, i) strdup(lua_tostring(L, i))
#define todiobj(L, i) (di_object *)lua_type_to_di_object(L, i, call_lua_function)
#define toobjref(L, i)                                                                   \
	({                                                                               \
		di_object *x = *(void **)lua_touserdata(L, i);                           \
		di_ref_object(x);                                                        \
		x;                                                                       \
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
		// Non-array tables, and tables with metatable shoudl become an di_object
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
			// Empty table should be pushed as unit, because empty tables can
			// either be interpreted as an empty di_array or an empty
			// di_object.
			// We pass unit for it because di_typed_trampoline knows how to
			// convert unit to either array or object as required.
			if (!nelem) {
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
	di_object *o = *di_lua_checkproxy(L, 1);

	if (lua_gettop(L) != 3) {
		return luaL_error(L, "'on' takes 3 arguments");
	}

	di_string signame;
	signame.data = luaL_checklstring(L, 2, &signame.length);
	if (lua_type(L, -1) != LUA_TFUNCTION) {
		return luaL_argerror(L, 3, "not a function");
	}

	auto handler = (di_object *)lua_type_to_di_object(L, -1, call_lua_function);

	if (once) {
		auto wrapped_handler =
		    di_new_object_with_type2(struct di_signal_handler_wrapper,
		                             "deai.plugin.lua:OnceSignalHandler");
		di_set_object_call((di_object *)wrapped_handler, call_lua_signal_handler_once);
		di_member(wrapped_handler, "wrapped", handler);
		handler = (di_object *)wrapped_handler;
	}

	auto listen_handle = di_listen_to(o, signame, (void *)handler);
	if (once) {
		di_member_clone(handler, "listen_handle", listen_handle);
	}
	di_unref_object((di_object *)handler);

	if (di_check_type(listen_handle, "deai:Error")) {
		scoped_di_string errmsg;
		DI_CHECK_OK(di_get(listen_handle, "errmsg", errmsg));
		return luaL_error(L, "failed to add listener %.*s", errmsg.length, errmsg.data);
	}
	di_lua_pushobject(L, NULL, listen_handle);
	return 1;
}

/// Push a variant value onto the lua stack. Since lua is a dynamically typed language,
/// this variant is "unpacked" into the actual value, instead of pushed as a proxy object.
/// var.value is not freed by this function, it is cloned when needed, so it's safe to
/// free it after this call.
static int di_lua_pushvariant(lua_State *L, const char *name, struct di_variant var) {
	// Check for nil
	if (var.type == DI_TYPE_OBJECT || var.type == DI_TYPE_STRING ||
	    var.type == DI_TYPE_POINTER) {
		// TODO(yshui) objects and strings cannot be NULL
		void *ptr = var.value->pointer;
		if (ptr == NULL) {
			lua_pushnil(L);
			return 1;
		}
	}

	if (var.type == DI_TYPE_ARRAY) {
		if (var.value->array.elem_type == DI_TYPE_ANY) {
			lua_pushnil(L);
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
			di_lua_pushvariant(
			    L, NULL, (struct di_variant){arr->arr + step * i, arr->elem_type});
			lua_rawseti(L, -2, i + 1);
		}
		return 1;
	case DI_TYPE_TUPLE:
		tuple = &var.value->tuple;
		lua_createtable(L, tuple->length, 0);
		for (int i = 0; i < tuple->length; i++) {
			di_lua_pushvariant(L, NULL, tuple->elements[i]);
			lua_rawseti(L, -2, i + 1);
		}
		return 1;
	case DI_TYPE_VARIANT:
		return di_lua_pushvariant(L, NULL, var.value->variant);
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
	di_object *o = *di_lua_checkproxy(L, 1);
	const char *signame = luaL_checkstring(L, 2);
	int top = lua_gettop(L);
	int rc = 0;

	di_tuple t;
	t.elements = tmalloc(struct di_variant, top - 2);
	t.length = top - 2;

	for (int i = 3; i <= top; i++) {
		rc = di_lua_type_to_di_variant(L, i, &t.elements[i - 3]);
		if (rc != 0) {
			goto err;
		}
	}

	di_ref_object(o);
	rc = di_emitn(o, di_string_borrow(signame), t);
	di_unref_object(o);

err:
	di_free_tuple(t);

	if (rc != 0) {
		return luaL_error(L, "Failed to emit signal %s", signame);
	}
	return 0;
}

static int di_lua_upgrade_weak_ref(lua_State *L) {
	struct di_weak_object *weak = *di_lua_checkproxy(L, 1);
	di_object *strong = di_upgrade_weak_ref(weak);
	if (strong == NULL) {
		return 0;
	}

	di_lua_pushobject(L, NULL, strong);
	return 1;
}

// Stack: [ object ]
static int di_lua_weak_ref(lua_State *L) {
	di_object *strong = *di_lua_checkproxy(L, 1);
	di_value weak = {.weak_object = di_weakly_ref_object(strong)};
	int nret = di_lua_pushvariant(
	    L, NULL, (struct di_variant){.type = DI_TYPE_WEAK_OBJECT, .value = &weak});
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
	return di_call_objectt(handler, rt, ret, t);
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

	/* This is __index for lua di_object proxies. This function
	 * will first try to lookup method with the requested name.
	 * If such methods are not found, this function will then
	 * try to call the __get_<name> method of the target di_object
	 * and return the result
	 */

	if (lua_gettop(L) != 2) {
		return luaL_error(L, "wrong number of arguments to __index");
	}

	const char *key = luaL_checklstring(L, 2, NULL);
	di_object *ud = *di_lua_checkproxy(L, 1);

	// Handle the special methods
	if (strcmp(key, "on") == 0) {
		lua_pushboolean(L, false);
		lua_pushcclosure(L, di_lua_add_listener, 1);
		return 1;
	}
	if (strcmp(key, "once") == 0) {
		lua_pushboolean(L, true);
		lua_pushcclosure(L, di_lua_add_listener, 1);
		return 1;
	}
	if (strcmp(key, "emit") == 0) {
		lua_pushcclosure(L, di_lua_emit_signal, 0);
		return 1;
	}
	if (strcmp(key, "weakref") == 0) {
		lua_pushcclosure(L, di_lua_weak_ref, 0);
		return 1;
	}

	di_type rt;
	di_value ret;
	int rc = di_getx(ud, di_string_borrow(key), &rt, &ret);
	if (rc != 0) {
		lua_pushnil(L);
		return 1;
	}
	rc = di_lua_pushvariant(L, key, (struct di_variant){&ret, rt});
	di_free_value(rt, &ret);
	return rc;
}

static int di_lua_meta_newindex(lua_State *L) {

	/* This is the __newindex function for lua di_object proxies,
	 * this translate calls to corresponding __set_<name>
	 * functions in the target di_object
	 */

	if (lua_gettop(L) != 3) {
		return luaL_error(L, "wrong number of arguments to __newindex");
	}

	di_object *ud = *di_lua_checkproxy(L, 1);
	di_string key;
	key.data = luaL_checklstring(L, 2, &key.length);
	di_type vt;

	di_value val;
	int rc = di_lua_type_to_di(L, 3, &vt, &val);
	if (rc != 0) {
		return luaL_error(L, "unhandled lua type");
	}

	int ret;
	if (vt == DI_TYPE_NIL) {
		ret = di_remove_member(ud, key);
	} else {
		ret = di_setx(ud, key, vt, &val);
		di_free_value(vt, &val);
	}

	if (ret != 0) {
		if (ret == -EINVAL) {
			return luaL_error(L, "property %s type mismatch", key);
		}
		if (ret == -ENOENT) {
			return luaL_error(L, "property %s doesn't exist", key);
		}
	}
	return 0;
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
static struct di_module *di_new_lua(struct deai *di) {
	auto m = di_new_module(di);

	di_method(m, "load_script", di_lua_load_script, di_string);

	// Load the builtin lua script. The returned object could safely die. The builtin
	// script should register modules which should keep it alive.
	scoped_di_object *ret = di_lua_load_script(
	    (void *)m,
	    di_string_borrow_literal(DI_PLUGIN_INSTALL_DIR "/lua/builtin.lua"));

	scoped_di_string errmsg = DI_STRING_INIT;
	if (di_get(ret, "errmsg", errmsg) == 0) {
		di_log_va(log_module, DI_LOG_ERROR, "Failed to load builtin lua script: %.*s",
		          (int)errmsg.length, errmsg.data);
		// The builtin script is not critical.
	}
	return m;
}
DEAI_PLUGIN_ENTRY_POINT(di) {
	auto m = di_new_lua(di);
	di_register_module(di, di_string_borrow("lua"), &m);
	return 0;
}
