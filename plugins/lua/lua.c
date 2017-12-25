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

#include "list.h"
#include "utils.h"

#define tmalloc(type, nmem) (type *)calloc(nmem, sizeof(type))
#define auto __auto_type

#define DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY "__deai.di_lua.script_object"

#define di_lua_get_env(L, s)                                                        \
	do {                                                                        \
		lua_pushliteral((L), DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);            \
		lua_rawget((L), LUA_REGISTRYINDEX);                                 \
		(s) = lua_touserdata((L), -1);                                      \
		lua_pop(L, 1);                                                      \
	} while (0)

#define di_lua_xchg_env(L, s)                                                       \
	do {                                                                        \
		void *tmp;                                                          \
		di_lua_get_env(L, tmp);                                             \
		lua_pushliteral((L), DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);            \
		lua_pushlightuserdata((L), (s));                                    \
		lua_rawset((L), LUA_REGISTRYINDEX);                                 \
		s = tmp;                                                            \
	} while (0)

struct di_lua_module {
	struct di_module;

	// Allow lua state to be freed when no lua object is ref'd by other part of
	// the system. So lua state can in turn free all di_object it holds
	struct di_lua_state *L;

	// struct list_head scripts;
};

struct di_lua_state {
	// Beware of cycles, could happen if one lua object is registered as module
	// and access in lua again as module.
	//
	// lua_state -> di_module/di_lua_ref -> lua_script -> lua_state
	// Need to use di_apoptosis
	struct di_object;
	lua_State *L;
	struct di_lua_module *m;
	struct di_listener *d;
};

struct di_lua_ref {
	struct di_object;
	int tref;
	struct di_lua_script *s;
	struct di_listener *d;
};

struct di_lua_handler {
	struct di_lua_ref;

	struct di_lua_listener *ll;
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

static int di_lua_pushany(lua_State *L, const char *name, di_type_t t, const void *d);
static int di_lua_getter(lua_State *L);
static int di_lua_setter(lua_State *L);

static int di_lua_errfunc(lua_State *L) {
	/* Convert error to string, to prevent a follow-up error with lua_concat. */
	auto err = luaL_tolstring(L, -1, NULL);

	lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
	lua_rawget(L, LUA_REGISTRYINDEX);
	struct di_lua_script *o = lua_touserdata(L, -1);
	di_getm(o->L->m->di, log, rc);

	if (!luaL_dostring(L, "return debug.traceback(\"error while running "
	                      "function!\", 3)")) {
		auto trace = lua_tostring(L, -1);
		di_log_va(logm, DI_LOG_ERROR,
		          "Failed to run lua script %s: %s\n%s\n", o->path, err,
		          trace);
	} else {
		auto err2 = luaL_tolstring(L, -1, NULL);
		di_log_va(logm, DI_LOG_ERROR, "Failed to run lua script %s: %s\n",
		          o->path, err);
		di_log_va(logm, DI_LOG_ERROR, "Failed to generate stack trace %s\n",
		          err2);
	}
	return 1;
}

static void di_lua_free_script(struct di_lua_script *s) {
	free(s->path);
	di_unref_object((void *)s->L);
	s->L = NULL;
}

static bool di_lua_isobject(lua_State *L, int index) {
	bool ret = false;
	do {
		if (!lua_isuserdata(L, index))
			break;
		if (!lua_getmetatable(L, index))
			break;

		lua_pushliteral(L, "__deai");
		lua_rawget(L, -2);
		if (lua_isnil(L, -1))
			break;

		ret = true;
	} while (0);
	// Pops 1 boolean (__deai) and 1 metatable
	lua_pop(L, 2);
	return ret;
}

static struct di_object *di_lua_checkobject(lua_State *L, int index) {
	if (di_lua_isobject(L, index))
		return *(struct di_object **)lua_touserdata(L, index);
	luaL_argerror(L, index, "not a di_object");
	__builtin_unreachable();
}

struct lua_table_getter {
	struct di_object;

	struct di_lua_ref *t;
};

static void lua_ref_cleanup(struct di_lua_ref *t) {
	di_stop_unref_listenerp(&t->d);
	if (t->s) {
		luaL_unref(t->s->L->L, LUA_REGISTRYINDEX, t->tref);
		di_unref_object((void *)t->s);
		t->s = NULL;
	}
	di_apoptosis((void *)t);
}

static void lua_ref_dtor(struct di_lua_ref *t) {
	lua_ref_cleanup(t);
}

static void *di_lua_type_to_di(lua_State *L, int i, di_type_t *t);

static int di_lua_table_get(struct di_object *m, di_type_t *rt, void **ret, int nargs,
                            const di_type_t *ats, const void *const *args) {
	struct lua_table_getter *g = (void *)m;
	struct di_lua_ref *t = g->t;
	if (nargs != 1)
		return -EINVAL;

	if (ats[0] != DI_TYPE_STRING)
		return -EINVAL;

	const char *key = *(const char *const *)args[0];

	struct di_lua_script *s = t->s;
	lua_State *L = t->s->L->L;
	di_lua_xchg_env(L, s);

	lua_rawgeti(L, LUA_REGISTRYINDEX, t->tref);
	lua_pushstring(L, key);
	lua_gettable(L, -2);

	*ret = di_lua_type_to_di(L, -1, rt);

	di_lua_xchg_env(L, s);
	return 0;
}

static struct di_object *
_lua_type_to_di_object(lua_State *L, int i, void *call, size_t sz) {
	// TODO need to make sure that same lua object get same di object
	assert(sz >= sizeof(struct di_lua_ref));
	struct di_lua_script *s;

	lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
	lua_rawget(L, LUA_REGISTRYINDEX);
	s = lua_touserdata(L, -1);
	lua_pop(L, 1);

	struct di_lua_ref *o = (void *)di_new_object(sz);
	o->tref = luaL_ref(L, LUA_REGISTRYINDEX);
	o->s = s;
	di_ref_object((void *)s);

	auto getter = di_new_object_with_type(struct lua_table_getter);
	getter->call = di_lua_table_get;
	// not incrementing getter->t->ref_count to prevent cycle
	getter->t = o;
	di_add_value_member((void *)o, "__get", false, DI_TYPE_OBJECT, getter);
	di_unref_object((void *)getter);
	o->dtor = (void *)lua_ref_dtor;
	o->call = call;
	o->d =
	    di_listen_to_destroyed((void *)s->L, (void *)lua_ref_cleanup, (void *)o);

	return (void *)o;
}

static struct di_object *lua_type_to_di_object(lua_State *L, int i, void *call) {
	return _lua_type_to_di_object(L, i, call, sizeof(struct di_lua_ref));
}

static int
_di_lua_method_handler(lua_State *L, const char *name, struct di_object *m) {
	if (!m->call)
		return luaL_error(L, "Object %s is not callable\n", name);

	int nargs = lua_gettop(L);

	void **args = calloc(nargs - 1, sizeof(void *));
	di_type_t *atypes = calloc(nargs - 1, sizeof(di_type_t));
	int argi = 0;
	// Translate lua arguments
	for (int i = 2; i <= nargs; i++) {
		args[i - 2] = di_lua_type_to_di(L, i, atypes + i - 2);
		if (atypes[i - 2] >= DI_LAST_TYPE) {
			argi = i;
			goto err;
		}
	}

	void *ret;
	di_type_t rtype;
	int nret = m->call((void *)m, &rtype, &ret, nargs - 1, atypes,
	                   (const void *const *)args);

	if (nret == 0) {
		nret = di_lua_pushany(L, NULL, rtype, ret);
		di_free_value(rtype, ret);
		free(ret);
	} else
		argi = -1;

err:
	for (int i = 0; i < nargs - 1; i++) {
		di_free_value(atypes[i], args[i]);
		free(args[i]);
	}
	free(args);
	free(atypes);
	if (argi > 0)
		return luaL_argerror(L, argi, "Unhandled lua type");
	else if (argi != 0)
		return luaL_error(L, "Failed to call function \"%s\": %d %s", name,
		                  argi, strerror(-nret));
	else
		return nret;
}

static int di_lua_method_handler(lua_State *L) {
	struct di_object *m = lua_touserdata(L, lua_upvalueindex(1));
	const char *name = lua_tostring(L, lua_upvalueindex(2));
	return _di_lua_method_handler(L, name, m);
}

static int di_lua_gc(lua_State *L) {
	struct di_object *o = di_lua_checkobject(L, 1);
	// fprintf(stderr, "lua gc %p\n", lo);
	di_unref_object(o);
	return 0;
}

const luaL_Reg di_lua_methods[] = {
    {"__index", di_lua_getter},
    {"__newindex", di_lua_setter},
    {"__gc", di_lua_gc},
    {0, 0},
};

static void di_lua_create_metatable_for_object(lua_State *L, const luaL_Reg *reg) {
	lua_pushcclosure(L, di_lua_method_handler, 2);

	lua_newtable(L);
	luaL_setfuncs(L, reg, 0);
	lua_pushliteral(L, "__deai");
	lua_pushboolean(L, true);
	lua_rawset(L, -3);

	lua_insert(L, -2);
	lua_pushliteral(L, "__call");
	lua_insert(L, -2);
	lua_rawset(L, -3);
	lua_setmetatable(L, -2);
}

static void di_lua_pushobject(lua_State *L, const char *name, struct di_object *o,
                              const luaL_Reg *reg) {
	// struct di_lua_script *s;
	void **ptr;
	ptr = lua_newuserdata(L, sizeof(void *));
	di_ref_object(o);
	*ptr = o;

	lua_pushlightuserdata(L, o);
	if (name)
		lua_pushstring(L, name);
	else
		lua_pushstring(L, "(anonymous)");
	di_lua_create_metatable_for_object(L, reg);
}

const char *allowed_os[] = {"time", "difftime", "clock", "tmpname", "date", NULL};

// the "di" global variable doesn't care about __gc
const luaL_Reg di_lua_di_methods[] = {
    {"__index", di_lua_getter}, {"__newindex", di_lua_setter}, {0, 0},
};

static void lua_state_dtor(struct di_lua_state *obj) {
	di_stop_unref_listenerp(&obj->d);
	obj->m->L = NULL;
	lua_close(obj->L);
}

static void lua_new_state(struct di_lua_module *m) {
	auto L = di_new_object_with_type(struct di_lua_state);
	L->m = m;
	L->L = luaL_newstate();
	L->dtor = (void *)lua_state_dtor;
	luaL_openlibs(L->L);

	di_lua_pushobject(L->L, "di", (void *)m->di, di_lua_di_methods);
	// Make it a weak ref
	di_unref_object((void *)m->di);
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
	m->L = L;
	L->d = di_listen_to_destroyed((void *)m->di, trivial_destroyed_handler,
	                              (void *)L);
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
	if (!path)
		return di_new_error("Path is null");

	auto s = di_new_object_with_type(struct di_lua_script);
	s->dtor = (void *)di_lua_free_script;

	struct di_lua_module *m = (void *)obj;
	// Don't hold ref. If lua module goes away first, script will become
	// defunct so that's fine.
	if (!m->L)
		lua_new_state(m);
	else
		di_ref_object((void *)m->L);
	s->L = m->L;

	di_getm(m->di, log, di_new_error("Can't find log module"));
	lua_pushcfunction(m->L->L, di_lua_errfunc);

	if (luaL_loadfile(m->L->L, path)) {
		const char *err = lua_tostring(m->L->L, -1);
		di_log_va(logm, DI_LOG_ERROR, "Failed to load lua script %s: %s\n",
		          path, err);
		lua_pop(m->L->L, 2);
		di_unref_object((void *)s);
		return di_new_error("Failed to load lua script %s: %s\n", path, err);
	}

	s->path = strdup(path);

	int ret;
	// load_script might be called by lua script,
	// so preserve the current set script object.
	di_lua_xchg_env(m->L->L, s);
	ret = lua_pcall(m->L->L, 0, 0, -2);
	di_lua_xchg_env(m->L->L, s);

	if (ret != 0) {
		// Right now there's no way to revert what this script
		// have done. (e.g. add listeners). So there's not much
		// we can do here except unref and return an error object

		// Pop error handling function
		lua_pop(m->L->L, 1);
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

		void *retd = di_lua_type_to_di(L, -1, &t);
		lua_pop(L, 1);
		if (t != elemt) {
			assert(t == DI_TYPE_INT && elemt == DI_TYPE_FLOAT);
			((double *)ret->arr)[i - 1] = *(int64_t *)retd;
		} else
			memcpy(ret->arr + sz * (i - 1), retd, sz);
		free(retd);
	}
	ret->length = nelem;
	return 0;
}

static int di_lua_checkarray(lua_State *L, int index, di_type_t *elemt) {
	lua_pushnil(L);
	if (lua_next(L, index) == 0) {
		// Empty array
		*elemt = DI_TYPE_VOID;
		return 0;
	}

	int i = 1;

	// get arr[1]
	lua_rawgeti(L, index, i++);

	void *ret = di_lua_type_to_di(L, -1, elemt);
	di_free_value(*elemt, ret);
	free(ret);
	// Pop 2 value, top of stack is the key
	lua_pop(L, 2);

	if (*elemt == DI_TYPE_VOID || *elemt >= DI_LAST_TYPE ||
	    *elemt == DI_TYPE_NIL) {
		lua_pop(L, 1);
		return -1;
	}

	while (lua_next(L, index) != 0) {
		lua_rawgeti(L, index, i++);

		di_type_t t;
		ret = di_lua_type_to_di(L, -1, &t);
		di_free_value(t, ret);
		free(ret);
		// pop 2 value
		lua_pop(L, 2);
		if (t != *elemt) {
			if (t == DI_TYPE_FLOAT && *elemt == DI_TYPE_INT)
				*elemt = DI_TYPE_FLOAT;
			else if (t != DI_TYPE_INT || *elemt != DI_TYPE_FLOAT) {
				// pop 1 key
				lua_pop(L, 1);
				return -1;
			}
		}

		if (i == INT_MAX) {
			// Array too big
			lua_pop(L, 1);
			return i - 1;
		}
	}
	return i - 1;
}

static int
call_lua_function(struct di_lua_ref *ref, di_type_t *rt, void **ret, int nargs,
                  const di_type_t *atypes, const void *const *args) {
	if (!ref->s)
		return -EBADF;

	lua_State *L = ref->s->L->L;
	struct di_lua_script *s = ref->s;
	// Prevent script object from being freed during pcall
	di_ref_object((void *)s);

	lua_pushcfunction(L, di_lua_errfunc);

	di_lua_xchg_env(L, s);

	// Get the function
	lua_rawgeti(L, LUA_REGISTRYINDEX, ref->tref);
	// Push arguments
	for (unsigned int i = 0; i < nargs; i++)
		di_lua_pushany(L, NULL, atypes[i], args[i]);

	lua_pcall(L, nargs, 1, -nargs - 2);

	di_lua_xchg_env(L, s);

	di_unref_object((void *)s);

	*ret = di_lua_type_to_di(L, -1, rt);
	return 0;
}

static void *di_lua_type_to_di(lua_State *L, int i, di_type_t *t) {
#define ret_arg(i, ty, t2, gfn)                                                     \
	do {                                                                        \
		void *__ret;                                                        \
		*t = ty;                                                            \
		__ret = calloc(1, sizeof(t2));                                      \
		*(t2 *)__ret = gfn(L, i);                                           \
		return __ret;                                                       \
	} while (0)
#define tostringdup(L, i) strdup(lua_tostring(L, i))
#define todiobj(L, i) lua_type_to_di_object(L, i, call_lua_function)
#define toobjref(L, i)                                                              \
	({                                                                          \
		struct di_object *x = *(void **)lua_touserdata(L, i);               \
		di_ref_object(x);                                                   \
		x;                                                                  \
	})
	int nelem;
	void *ret;
	di_type_t elemt;
	switch (lua_type(L, i)) {
	case LUA_TBOOLEAN: ret_arg(i, DI_TYPE_BOOL, bool, lua_toboolean);
	case LUA_TNUMBER:
		if (lua_isinteger(L, i))
			ret_arg(i, DI_TYPE_INT, int64_t, lua_tointeger);
		else
			ret_arg(i, DI_TYPE_FLOAT, double, lua_tonumber);
	case LUA_TSTRING: ret_arg(i, DI_TYPE_STRING, const char *, tostringdup);
	case LUA_TUSERDATA:
		if (!di_lua_isobject(L, i))
			goto type_error;
		ret_arg(i, DI_TYPE_OBJECT, void *, toobjref);
	case LUA_TTABLE:
		// Empty table should be pushed as object
		if ((nelem = di_lua_checkarray(L, i, &elemt)) <= 0) {
			*t = DI_TYPE_OBJECT;
			ret = malloc(sizeof(struct di_object *));
			*(void **)ret = lua_type_to_di_object(L, i, NULL);
			return ret;
		}
		*t = DI_TYPE_ARRAY;
		ret = calloc(1, sizeof(struct di_array));
		di_lua_table_to_array(L, i, nelem, elemt, ret);
		return ret;
	case LUA_TFUNCTION: ret_arg(i, DI_TYPE_OBJECT, struct di_object *, todiobj);
	case LUA_TNIL:
		*t = DI_TYPE_NIL;
		return NULL;
	type_error:
	default: *t = DI_LAST_TYPE; return NULL;
	}
#undef ret_arg
#undef tostringdup
#undef todiobj
}

static int di_lua_add_listener(lua_State *L) {
	if (lua_gettop(L) != 2)
		return luaL_error(L, "'on' only takes 2 arguments");

	struct di_object *o = lua_touserdata(L, lua_upvalueindex(1));
	const char *signame = luaL_checklstring(L, 1, NULL);
	if (lua_type(L, -1) != LUA_TFUNCTION)
		return luaL_argerror(L, 2, "not a function");

	struct di_lua_handler *h =
	    (void *)lua_type_to_di_object(L, -1, call_lua_function);

	auto l = di_listen_to(o, signame, (void *)h);
	if (IS_ERR(l))
		return luaL_error(L, "failed to add listener %s",
		                  strerror(PTR_ERR(l)));
	// ABRT_IF_ERR(di_hold_object(s->m->listeners, (void *)l));
	di_unref_object((void *)h);

	di_lua_pushany(L, NULL, DI_TYPE_OBJECT, &l);
	di_unref_object((void *)l);
	return 1;
}

static int di_lua_call_method(lua_State *L) {
	struct di_object *o = lua_touserdata(L, lua_upvalueindex(1));
	const char *name = luaL_checklstring(L, 1, NULL);
	struct di_object *m;

	int rc = di_get(o, name, m);
	if (rc != 0)
		return luaL_error(L, "method %s not found", name);

	return _di_lua_method_handler(L, name, m);
}

// d is not freed by this function
static int di_lua_pushany(lua_State *L, const char *name, di_type_t t, const void *d) {
	// Check for nil
	if (t == DI_TYPE_OBJECT || t == DI_TYPE_STRING || t == DI_TYPE_POINTER) {
		void *ptr = *(void **)d;
		if (ptr == NULL) {
			lua_pushnil(L);
			return 1;
		}
	}

	if (t == DI_TYPE_ARRAY) {
		const struct di_array *tmp = d;
		if (tmp->elem_type == DI_TYPE_NIL) {
			lua_pushnil(L);
			return 1;
		}
	}

	int b;
	lua_Integer i;
	lua_Number n;
	struct di_array *arr;
	struct di_tuple *tuple;
	int step;
	switch (t) {
	case DI_TYPE_NUINT: i = *(unsigned int *)d; goto pushint;
	case DI_TYPE_UINT: i = *(uint64_t *)d; goto pushint;
	case DI_TYPE_NINT: i = *(int *)d; goto pushint;
	case DI_TYPE_INT: i = *(int64_t *)d; goto pushint;
	case DI_TYPE_FLOAT: n = *(double *)d; goto pushnumber;
	case DI_TYPE_POINTER:
		// bad idea
		lua_pushlightuserdata(L, *(void **)d);
		return 1;
	case DI_TYPE_OBJECT:
		lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
		lua_rawget(L, LUA_REGISTRYINDEX);
		lua_pop(L, 1);
		di_lua_pushobject(L, name, *(void **)d, di_lua_methods);
		return 1;
	case DI_TYPE_STRING:
	case DI_TYPE_STRING_LITERAL: lua_pushstring(L, *(const char **)d); return 1;
	case DI_TYPE_ARRAY:
		arr = (struct di_array *)d;
		step = di_sizeof_type(arr->elem_type);
		lua_createtable(L, arr->length, 0);
		for (int i = 0; i < arr->length; i++) {
			di_lua_pushany(L, NULL, arr->elem_type, arr->arr + step * i);
			lua_rawseti(L, -2, i + 1);
		}
		return 1;
	case DI_TYPE_TUPLE:
		tuple = (struct di_tuple *)d;
		lua_createtable(L, tuple->length, 0);
		for (int i = 0; i < tuple->length; i++) {
			di_lua_pushany(L, NULL, tuple->elem_type[i], tuple->tuple[i]);
			lua_rawseti(L, -2, i + 1);
		}
		return 1;
	case DI_TYPE_BOOL:
		b = *(bool *)d;
		lua_pushboolean(L, b);
		return 1;
	// shouldn't happen
	case DI_TYPE_VOID:
	default: return 0;
	}

pushint:
	lua_pushinteger(L, i);
	return 1;
pushnumber:
	lua_pushnumber(L, n);
	return 1;
}

int di_lua_emit_signal(lua_State *L) {
	struct di_object *o = lua_touserdata(L, lua_upvalueindex(1));
	const char *signame = luaL_checkstring(L, 1);
	int top = lua_gettop(L);

	const void **args = tmalloc(const void *, top);
	di_type_t *atypes = tmalloc(di_type_t, top);

	for (int i = 2; i <= top; i++) {
		void *dst = di_lua_type_to_di(L, i, &atypes[i - 1]);
		args[i - 1] = dst;
	}

	di_ref_object(o);
	args[0] = &o;
	atypes[0] = DI_TYPE_OBJECT;

	int ret = di_emitn(o, signame, top, atypes, args);

	di_unref_object(o);
	for (int i = 1; i < top; i++) {
		di_free_value(atypes[i], (void *)args[i]);
		free((void *)args[i]);
	}
	free(atypes);
	free(args);

	if (ret != 0)
		return luaL_error(L, "Failed to emit signal %s", signame);
	return 0;
}

static int di_lua_getter(lua_State *L) {

	/* This is __index for lua di_object proxies. This function
	 * will first try to lookup method with the requested name.
	 * If such methods are not found, this function will then
	 * try to call the __get_<name> method of the target di_object
	 * and return the result
	 */

	if (lua_gettop(L) != 2)
		return luaL_error(L, "wrong number of arguments to __index");

	const char *key = luaL_checklstring(L, 2, NULL);
	struct di_object *ud = di_lua_checkobject(L, 1);

	// Eliminate special methods
	if (strcmp(key, "on") == 0) {
		lua_pushlightuserdata(L, ud);
		lua_pushcclosure(L, di_lua_add_listener, 1);
		return 1;
	} else if (strcmp(key, "call") == 0) {
		lua_pushlightuserdata(L, ud);
		lua_pushcclosure(L, di_lua_call_method, 1);
		return 1;
	} else if (strcmp(key, "emit") == 0) {
		lua_pushlightuserdata(L, ud);
		lua_pushcclosure(L, di_lua_emit_signal, 1);
		return 1;
	}

	di_type_t rt;
	const void *ret;
	int rc = di_getx(ud, key, &rt, &ret);
	if (rc != 0) {
		lua_pushnil(L);
		return 1;
	}
	rc = di_lua_pushany(L, key, rt, ret);
	di_free_value(rt, (void *)ret);
	free((void *)ret);
	return rc;
}

static int di_lua_setter(lua_State *L) {

	/* This is the __newindex function for lua di_object proxies,
	 * this translate calls to corresponding __set_<name>
	 * functions in the target di_object
	 */

	if (lua_gettop(L) != 3)
		return luaL_error(L, "wrong number of arguments to __newindex");

	struct di_object *ud = di_lua_checkobject(L, 1);
	const char *key = luaL_checkstring(L, 2);
	di_type_t vt;

	void *val = di_lua_type_to_di(L, 3, &vt);
	if (!val && vt != DI_TYPE_NIL)
		return luaL_error(L, "unhandled lua type");

	int ret = di_setx(ud, key, vt, val);
	di_free_value(vt, val);
	free(val);

	if (ret != 0) {
		if (ret == -EINVAL)
			return luaL_error(L, "property %s type mismatch", key);
		if (ret == -ENOENT)
			return luaL_error(
			    L, "property %s doesn't exist or is read only", key);
	}
	return 0;
}

PUBLIC int di_plugin_init(struct deai *di) {
	auto m = di_new_module_with_type(struct di_lua_module);

	m->di = di;
	di_method(m, "load_script", di_lua_load_script, char *);

	di_register_module(di, "lua", (void *)m);
	di_unref_object((void *)m);
	return 0;
}
