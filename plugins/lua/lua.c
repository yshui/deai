/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <assert.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdio.h>
#include <string.h>

#include <deai/builtin/log.h>
#include <deai/deai.h>
#include <deai/helper.h>

#include "compat.h"
#include "list.h"
#include "utils.h"

#define tmalloc(type, nmem) (type *)calloc(nmem, sizeof(type))
#define auto __auto_type

#define DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY "__deai.di_lua.script_object"

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

struct di_lua_state {
	// Beware of cycles, could happen if one lua object is registered as module
	// and access in lua again as module.
	//
	// lua_state -> di_module/di_lua_ref -> lua_script -> lua_state
	struct di_object;
	lua_State *L;
	struct di_module *m;
};

struct di_lua_ref {
	struct di_object;
	int tref;
	struct di_lua_script *s;
	struct di_listener *attached_listener;
};

struct di_lua_script {
	struct di_object;
	char *path;
	// NULL means the lua module has been freed
	struct di_lua_state *L;
	/*
	 * Keep track of all listeners and objects so we can free them when script is
	 * freed
	 */
	struct list_head sibling;
};

static int di_lua_pushvariant(lua_State *L, const char *name, struct di_variant var);
static int di_lua_meta_index(lua_State *L);
static int di_lua_meta_newindex_for_weak_object(lua_State *L);
static int di_lua_meta_newindex(lua_State *L);

static int di_lua_errfunc(lua_State *L) {
	/* Convert error to string, to prevent a follow-up error with lua_concat. */
	auto err = luaL_tolstring(L, -1, NULL);

	lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
	lua_rawget(L, LUA_REGISTRYINDEX);
	struct di_lua_script *o = lua_touserdata(L, -1);
	di_mgetm(o->L->m, log, rc);

	if (!luaL_dostring(L, "return debug.traceback(\"error while running "
	                      "function!\", 3)")) {
		auto trace = lua_tostring(L, -1);
		di_log_va(logm, DI_LOG_ERROR, "Failed to run lua script %s: %s\n%s\n",
		          o->path, err, trace);
	} else {
		auto err2 = luaL_tolstring(L, -1, NULL);
		di_log_va(logm, DI_LOG_ERROR, "Failed to run lua script %s: %s\n", o->path, err);
		di_log_va(logm, DI_LOG_ERROR, "Failed to generate stack trace %s\n", err2);
	}
	return 1;
}

static void di_lua_free_script(struct di_lua_script *s) {
	free(s->path);
	di_unref_object((void *)s->L);
	s->L = NULL;
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

static void *di_lua_checkproxy(lua_State *L, int index) {
	if (di_lua_isproxy(L, index)) {
		return *(struct di_object **)lua_touserdata(L, index);
	}
	luaL_argerror(L, index, "not a di_object");
	unreachable();
}

static void lua_ref_dtor(struct di_lua_ref *t) {
	luaL_unref(t->s->L->L, LUA_REGISTRYINDEX, t->tref);
	di_unref_object((void *)t->s);
	if (t->attached_listener) {
		di_stop_listener(t->attached_listener);
		t->attached_listener = NULL;
	}
	t->s = NULL;
}

static int di_lua_type_to_di(lua_State *L, int i, di_type_t *t, union di_value *ret);

static inline int di_lua_type_to_di_variant(lua_State *L, int i, struct di_variant *var) {
	int rc = di_lua_type_to_di(L, i, &var->type, NULL);
	if (rc != 0) {
		return rc;
	}

	var->value = malloc(di_sizeof_type(var->type));
	di_lua_type_to_di(L, i, &var->type, var->value);
	return 0;
}

static int
di_lua_di_getter(struct di_object *m, di_type_t *rt, union di_value *ret, struct di_tuple tu) {
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

	const char *key = vars[1].value->string_literal;

	struct di_lua_script *s = t->s;
	lua_State *L = t->s->L->L;
	di_lua_xchg_env(L, s);

	lua_rawgeti(L, LUA_REGISTRYINDEX, t->tref);
	lua_pushstring(L, key);
	lua_gettable(L, -2);

	DI_OK_OR_RET(di_lua_type_to_di(L, -1, rt, ret));

	di_lua_xchg_env(L, s);
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
	o->tref = luaL_ref(L, LUA_REGISTRYINDEX);        // this pops the table from
	                                                 // stack, we need to put it back
	o->s = s;
	di_ref_object((void *)s);

	// Restore the value onto the stack
	lua_pushinteger(L, o->tref);
	lua_rawget(L, LUA_REGISTRYINDEX);

	auto getter = di_new_object_with_type(struct di_object);
	di_set_object_call((void *)getter, di_lua_di_getter);
	di_add_member_move((void *)o, "__get", (di_type_t[]){DI_TYPE_OBJECT}, (void **)&getter);
	di_set_object_dtor((void *)o, (void *)lua_ref_dtor);
	di_set_object_call((void *)o, call);

	// Need to return
	return o;
}

static int _di_lua_method_handler(lua_State *L, const char *name, struct di_object *m) {
	if (!di_is_object_callable(m)) {
		return luaL_error(L, "Object %s is not callable\n", name);
	}

	int nargs = lua_gettop(L);

	struct di_tuple t;
	t.elements = tmalloc(struct di_variant, nargs - 1);
	t.length = nargs - 1;
	int error_argi = 0;
	// Translate lua arguments
	for (int i = 2; i <= nargs; i++) {
		if (di_lua_type_to_di_variant(L, i, &t.elements[i - 2]) != 0) {
			error_argi = i;
			goto err;
		}
	}

	union di_value ret;
	di_type_t rtype;
	int rc = di_call_objectt(m, &rtype, &ret, t);
	int nret;

	if (rc == 0) {
		nret = di_lua_pushvariant(L, NULL, (struct di_variant){&ret, rtype});
		di_free_value(rtype, &ret);
	}

err:
	di_free_tuple(t);
	if (error_argi > 0) {
		return luaL_argerror(L, error_argi, "Unhandled lua type");
	}
	if (rc != 0) {
		return luaL_error(L, "Failed to call function \"%s\": %s", name, strerror(-rc));
	}
	return nret;
}

static int di_lua_method_handler(lua_State *L) {
	struct di_object *m = lua_touserdata(L, lua_upvalueindex(1));
	const char *name = lua_tostring(L, lua_upvalueindex(2));
	return _di_lua_method_handler(L, name, m);
}

static int di_lua_gc(lua_State *L) {
	struct di_object *o = di_lua_checkproxy(L, 1);
	// fprintf(stderr, "lua gc %p\n", lo);
	di_unref_object(o);
	return 0;
}

static int di_lua_gc_for_weak_object(lua_State *L) {
	struct di_weak_object *weak = di_lua_checkproxy(L, 1);
	di_drop_weak_ref(&weak);
	return 0;
}

const luaL_Reg di_lua_object_methods[] = {
    {"__index", di_lua_meta_index},
    {"__newindex", di_lua_meta_newindex},
    {"__gc", di_lua_gc},
    {0, 0},
};

const luaL_Reg di_lua_weak_object_methods[] = {
    {"__index", di_lua_meta_newindex_for_weak_object},
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

// Push an object `o` to lua stack. Note this function doesn't increment the reference
// count of `o`.
static void
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
	di_remove_member_raw((void *)obj->m, "__lua_state");
}

static void lua_new_state(struct di_module *m) {
	auto L = di_new_object_with_type(struct di_lua_state);
	L->m = m;
	L->L = luaL_newstate();
	di_set_object_dtor((void *)L, (void *)lua_state_dtor);
	luaL_openlibs(L->L);

	struct di_object *di = (void *)di_module_get_deai(m);
	di_ref_object(di);
	di_lua_pushproxy(L->L, "di", di, di_lua_di_methods, false);
	// Make it a weak ref
	// TODO(yshui) add proper weak ref
	di_unref_object(di);
	lua_setglobal(L->L, "di");

	// We have to unref di here, otherwise there will be a ref cycle:
	// di <-> lua_module
	// di_unref_object((void *)di);

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

	auto Lo = (struct di_object *)L;
	di_member(m, "__lua_state", Lo);
}

static struct di_object *di_lua_load_script(struct di_object *obj, const char *path) {
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
	if (!path) {
		return di_new_error("Path is null");
	}

	auto s = di_new_object_with_type(struct di_lua_script);
	di_set_object_dtor((void *)s, (void *)di_lua_free_script);

	struct di_module *m = (void *)obj;
	{
		union di_value L;
		di_type_t vtype;
		int rc = di_rawgetx(obj, "__lua_state", &vtype, &L);

		// Don't hold ref. If lua module goes away first, script will become
		// defunct so that's fine.
		if (rc != 0) {
			lua_new_state(m);
			rc = di_rawgetx(obj, "__lua_state", &vtype, &L);
		}
		assert(vtype == DI_TYPE_OBJECT);

		s->L = (void *)L.object;
	}

	struct di_lua_state *L = s->L;
	di_mgetm(m, log, di_new_error("Can't find log module"));
	lua_pushcfunction(s->L->L, di_lua_errfunc);

	if (luaL_loadfile(s->L->L, path)) {
		const char *err = lua_tostring(s->L->L, -1);
		di_log_va(logm, DI_LOG_ERROR, "Failed to load lua script %s: %s\n", path, err);
		lua_pop(s->L->L, 2);
		di_unref_object((void *)s);
		return di_new_error("Failed to load lua script %s: %s\n", path, err);
	}

	s->path = strdup(path);

	int ret;
	// load_script might be called by lua script,
	// so preserve the current set script object.
	di_lua_xchg_env(L->L, s);
	ret = lua_pcall(L->L, 0, 0, -2);
	di_lua_xchg_env(L->L, s);

	if (ret != 0) {
		// Right now there's no way to revert what this script
		// have done. (e.g. add listeners). So there's not much
		// we can do here except unref and return an error object

		// Pop error handling function
		lua_pop(s->L->L, 1);
		di_unref_object((void *)s);
		return di_new_error("Failed to run the lua script");
	}
	return (void *)s;
}

static int di_lua_table_to_array(lua_State *L, int index, int nelem, di_type_t elemt,
                                 struct di_array *ret) {
	ret->elem_type = elemt;

	size_t sz = di_sizeof_type(elemt);
	assert(sz != 0 || nelem == 0);
	ret->arr = calloc(nelem, sz);

	for (int i = 1; i <= nelem; i++) {
		di_type_t t;
		lua_rawgeti(L, index, i);

		union di_value retd;
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
static bool di_lua_checkarray(lua_State *L, int index, int *nelem, di_type_t *elemt) {
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
	union di_value ret;
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
		di_type_t t;
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

static int call_lua_function(struct di_lua_ref *ref, di_type_t *rt, union di_value *ret,
                             struct di_tuple t) {
	if (!ref->s) {
		return -EBADF;
	}

	struct di_variant *vars = t.elements;

	lua_State *L = ref->s->L->L;
	struct di_lua_script *s = ref->s;
	// Prevent script object from being freed during pcall
	di_ref_object((void *)s);

	lua_pushcfunction(L, di_lua_errfunc);

	di_lua_xchg_env(L, s);

	// Get the function
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref->tref);
	// Push arguments
	for (unsigned int i = 0; i < t.length; i++) {
		di_lua_pushvariant(L, NULL, vars[i]);
	}

	lua_pcall(L, t.length, 1, -(int)t.length - 2);

	di_lua_xchg_env(L, s);

	di_unref_object((void *)s);

	di_lua_type_to_di(L, -1, rt, ret);
	return 0;
}

/// Convert lua value at index `i` to a deai value.
/// The value is not popped. If `ret` is NULL, the value is not returned, but the type
/// will always be returned
static int di_lua_type_to_di(lua_State *L, int i, di_type_t *t, union di_value *ret) {
#define ret_arg(i, field, gfn)                                                           \
	do {                                                                             \
		*t = di_typeof(ret->field);                                              \
		if (ret != NULL) {                                                       \
			ret->field = gfn(L, i);                                          \
		}                                                                        \
		return 0;                                                                \
	} while (0)
#define tostringdup(L, i) strdup(lua_tostring(L, i))
#define todiobj(L, i) (struct di_object *)lua_type_to_di_object(L, i, call_lua_function)
#define toobjref(L, i)                                                                   \
	({                                                                               \
		struct di_object *x = *(void **)lua_touserdata(L, i);                    \
		di_ref_object(x);                                                        \
		x;                                                                       \
	})
	int nelem;
	di_type_t elemt;
	switch (lua_type(L, i)) {
	case LUA_TBOOLEAN:
		ret_arg(i, bool_, lua_toboolean);
	case LUA_TNUMBER:
		if (lua_isinteger(L, i)) {
			ret_arg(i, int_, lua_tointeger);
		} else {
			ret_arg(i, float_, lua_tonumber);
		}
	case LUA_TSTRING:
		ret_arg(i, string, tostringdup);
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
				*t = DI_TYPE_NIL;
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

static int di_lua_listener_gc(lua_State *L) {
	struct di_object *o = di_lua_checkproxy(L, 1);
	// fprintf(stderr, "lua gc %p\n", lo);
	di_stop_listener((struct di_listener *)o);
	return 0;
}

const luaL_Reg di_lua_listener_methods[] = {
    {"__index", di_lua_meta_index},
    {"__newindex", di_lua_meta_newindex},
    {"__gc", di_lua_listener_gc},
    {0, 0},
};

// Stack: [ object, string, boolean (optional), lua closure ]
static int di_lua_add_listener(lua_State *L) {
	bool once = false;
	struct di_object *o = di_lua_checkproxy(L, 1);

	if (lua_gettop(L) == 4) {
		once = lua_toboolean(L, 3);
		lua_remove(L, 3);
	}
	if (lua_gettop(L) != 3) {
		return luaL_error(L, "'on' takes 3 or 4 arguments");
	}

	const char *signame = luaL_checklstring(L, 2, NULL);
	if (lua_type(L, -1) != LUA_TFUNCTION) {
		return luaL_argerror(L, 3, "not a function");
	}

	auto h = lua_type_to_di_object(L, -1, call_lua_function);

	auto l = di_listen_to_once(o, signame, (void *)h, once);
	if (IS_ERR(l)) {
		return luaL_error(L, "failed to add listener %s", strerror((int)PTR_ERR(l)));
	}
	h->attached_listener = l;
	di_unref_object((void *)h);

	di_ref_object((void *)l);
	di_lua_pushproxy(L, NULL, (void *)l, di_lua_object_methods, false);
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
	struct di_array *arr;
	struct di_tuple *tuple;
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
	case DI_TYPE_OBJECT:
		di_ref_object(var.value->object);
		di_lua_pushproxy(L, name, var.value->object, di_lua_object_methods, true);
		return 1;
	case DI_TYPE_WEAK_OBJECT:
		di_copy_value(DI_TYPE_WEAK_OBJECT, &weak, &var.value->weak_object);
		di_lua_pushproxy(L, name, weak, di_lua_weak_object_methods, false);
		return 1;
	case DI_TYPE_STRING:
		lua_pushstring(L, var.value->string);
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

pushint:
	lua_pushinteger(L, i);
	return 1;
pushnumber:
	lua_pushnumber(L, n);
	return 1;
}

// Stack: [ object, string (signal name), arguments... ]
int di_lua_emit_signal(lua_State *L) {
	struct di_object *o = di_lua_checkproxy(L, 1);
	const char *signame = luaL_checkstring(L, 2);
	int top = lua_gettop(L);
	int rc = 0;

	struct di_tuple t;
	t.elements = tmalloc(struct di_variant, top - 2);
	t.length = top - 2;

	for (int i = 3; i <= top; i++) {
		rc = di_lua_type_to_di_variant(L, i, &t.elements[i - 3]);
		if (rc != 0) {
			goto err;
		}
	}

	di_ref_object(o);
	rc = di_emitn(o, signame, t);
	di_unref_object(o);

err:
	di_free_tuple(t);

	if (rc != 0) {
		return luaL_error(L, "Failed to emit signal %s", signame);
	}
	return 0;
}

static int di_lua_upgrade_weak_ref(lua_State *L) {
	struct di_weak_object *weak = di_lua_checkproxy(L, 1);
	struct di_object *strong = di_upgrade_weak_ref(weak);
	if (strong == NULL) {
		return 0;
	}
	di_lua_pushproxy(L, NULL, strong, di_lua_object_methods, true);
	return 1;
}

// Stack: [ object ]
static int di_lua_weak_ref(lua_State *L) {
	struct di_object *strong = di_lua_checkproxy(L, 1);
	union di_value weak = {.weak_object = di_weakly_ref_object(strong)};
	int nret = di_lua_pushvariant(
	    L, NULL, (struct di_variant){.type = DI_TYPE_WEAK_OBJECT, .value = &weak});
	di_drop_weak_ref(&weak.weak_object);
	return nret;
}

static int di_lua_meta_newindex_for_weak_object(lua_State *L) {
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
	struct di_object *ud = di_lua_checkproxy(L, 1);

	// Eliminate special methods
	if (strcmp(key, "on") == 0) {
		lua_pushcclosure(L, di_lua_add_listener, 0);
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

	di_type_t rt;
	union di_value ret;
	int rc = di_getx(ud, key, &rt, &ret);
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

	struct di_object *ud = di_lua_checkproxy(L, 1);
	const char *key = luaL_checkstring(L, 2);
	di_type_t vt;

	union di_value val;
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

DEAI_PLUGIN_ENTRY_POINT(di) {
	auto m = di_new_module(di);

	di_method(m, "load_script", di_lua_load_script, char *);

	di_register_module(di, "lua", &m);
	return 0;
}
