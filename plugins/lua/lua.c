/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Copyright (c) 2017, Yuxuan Shui <yshuiv7@gmail.com> */

#include <builtin/log.h>
#include <deai.h>
#include <helper.h>

#include <assert.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdio.h>
#include <string.h>

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
	lua_State *L;
	struct di_listener *shutdown_listener;

	struct list_head scripts;
	struct list_head ldi;
};

struct di_lua_listener {
	struct di_listener *l;
	struct di_object *o;
	char *signame;
	int fnref;
	struct di_lua_script *s;
	struct list_head sibling;
};

struct di_lua_object {
	struct di_object *o;
	struct list_head sibling;
};

struct di_lua_table {
	struct di_object;
	int tref;
	struct di_lua_script *s;
};

struct di_lua_script {
	struct di_object;
	char *path;
	// NULL means the lua module has been freed
	struct di_lua_module *m;
	/*
	 * Keep track of all listeners and objects so we can free them when script is
	 * freed
	 */
	struct list_head listeners;
	struct list_head objects;
	struct list_head sibling;
};

static int di_lua_pushany(lua_State *L, di_type_t t, void *d);
static int di_lua_getter(lua_State *L);
static int di_lua_setter(lua_State *L);

static int di_lua_errfunc(lua_State *L) {
	/* Convert error to string, to prevent a follow-up error with lua_concat. */
	auto err = luaL_tolstring(L, -1, NULL);

	lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
	lua_rawget(L, LUA_REGISTRYINDEX);
	struct di_lua_script *o = lua_touserdata(L, -1);
	di_getm(o->m->di, log);

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

static void di_lua_clear_listener(struct di_lua_script *s) {
	// Remove all listeners
	struct di_lua_listener *ll, *nll;
	list_for_each_entry_safe(ll, nll, &s->listeners, sibling)
	    di_remove_listener(ll->o, ll->signame, ll->l);
}

static void di_lua_free_script(struct di_lua_script *s) {
	// fprintf(stderr, "free lua script %p, %s\n", s->m ,s->path);
	// di_lua_clear_listener(s);
	// assert(list_empty(&s->objects));
	struct di_lua_object *lo, *nlo;
	// int cnt = 0;
	list_for_each_entry_safe(lo, nlo, &s->objects, sibling) {
		// fprintf(stderr, "%d %p\n", ++cnt, lo);
		di_unref_object(&lo->o);
		list_del(&lo->sibling);
		lo->o = NULL;
		// don't free here, will handled by di_lua_gc
	}
	list_del(&s->sibling);
	free(s->path);
	s->m = NULL;
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
	di_dtor(s, di_lua_free_script);

	struct di_lua_module *m = (void *)obj;
	// Don't hold ref. If lua module goes away first, script will become
	// defunct so that's fine.
	s->m = m;
	list_add(&s->sibling, &m->scripts);

	di_getm(m->di, log);
	lua_pushcfunction(m->L, di_lua_errfunc);

	INIT_LIST_HEAD(&s->listeners);
	INIT_LIST_HEAD(&s->objects);

	if (luaL_loadfile(m->L, path)) {
		const char *err = lua_tostring(m->L, -1);
		di_log_va(logm, DI_LOG_ERROR, "Failed to load lua script %s: %s\n",
		          path, err);
		di_unref_object((void *)&s);
		lua_pop(m->L, 2);
		return di_new_error("Failed to load lua script %s: %s\n", path, err);
	}

	s->path = strdup(path);

	int ret;
	// load_script might be called by lua script,
	// so preserve the current set script object.
	di_lua_xchg_env(m->L, s);
	ret = lua_pcall(m->L, 0, 0, -2);
	di_lua_xchg_env(m->L, s);

	if (ret != 0) {
		// destroy the object to remove any listener that
		// might have been added
		di_destroy_object((void *)s);
		di_unref_object((void *)&s);

		// Pop error handling function
		lua_pop(m->L, 1);
		return di_new_error("Failed to run the lua script");
	}
	return (void *)s;
}

static void *di_lua_type_to_di(lua_State *L, int i, di_type_t *t);

static int di_lua_table_to_array(lua_State *L, int index, int nelem, di_type_t elemt,
                                 struct di_array *ret) {
	ret->elem_type = elemt;

	size_t sz = di_sizeof_type(elemt);
	assert(sz != 0);
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

static struct di_lua_object *di_lua_checklobject(lua_State *L, int index) {
	if (di_lua_isobject(L, index))
		return *(struct di_lua_object **)lua_touserdata(L, index);
	luaL_argerror(L, index, "not a di_object");
	__builtin_unreachable();
}

static struct di_object *di_lua_checkobject(lua_State *L, int index) {
	return di_lua_checklobject(L, index)->o;
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

static int di_lua_table_get(di_type_t *rt, void **ret, unsigned int nargs,
                            const di_type_t *ats, const void *const *args, void *ud) {
	struct di_lua_table *t = ud;
	if (nargs != 1)
		return -EINVAL;

	if (ats[0] != DI_TYPE_STRING)
		return -EINVAL;

	const char *key = *(const char *const *)args[0];

	struct di_lua_script *s = t->s;
	lua_State *L = t->s->m->L;
	di_lua_xchg_env(L, s);

	lua_rawgeti(L, LUA_REGISTRYINDEX, t->tref);
	lua_pushstring(L, key);
	lua_gettable(L, -2);

	*ret = di_lua_type_to_di(L, -1, rt);

	di_lua_xchg_env(L, s);
	return 0;
}

static void di_lua_table_dtor(struct di_lua_table *t) {
	luaL_unref(t->s->m->L, LUA_REGISTRYINDEX, t->tref);
	di_unref_object((void *)&t->s);
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
	int nelem;
	void *ret;
	di_type_t elemt;
	struct di_lua_table *o;
	struct di_lua_script *s;
	switch (lua_type(L, i)) {
	case LUA_TBOOLEAN:
		ret_arg(i, DI_TYPE_NUINT, unsigned int, lua_toboolean);
		break;
	case LUA_TNUMBER:
		if (lua_isinteger(L, i))
			ret_arg(i, DI_TYPE_INT, int64_t, lua_tointeger);
		else
			ret_arg(i, DI_TYPE_FLOAT, double, lua_tonumber);
	case LUA_TSTRING: ret_arg(i, DI_TYPE_STRING, const char *, tostringdup);
	case LUA_TUSERDATA:
		if (!di_lua_isobject(L, i))
			goto type_error;
		ret_arg(i, DI_TYPE_OBJECT, void *, *(void **)lua_touserdata);
	case LUA_TTABLE:
		if ((nelem = di_lua_checkarray(L, i, &elemt)) < 0)
			goto push_object;
		*t = DI_TYPE_ARRAY;
		ret = calloc(1, sizeof(struct di_array));
		di_lua_table_to_array(L, i, nelem, elemt, ret);
		return ret;
	push_object:
		lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
		lua_rawget(L, LUA_REGISTRYINDEX);
		s = lua_touserdata(L, -1);
		lua_pop(L, 1);

		o = di_new_object_with_type(struct di_lua_table);
		o->tref = luaL_ref(L, LUA_REGISTRYINDEX);
		o->s = s;
		di_ref_object((void *)s);
		di_register_method((void *)o, (void *)di_create_untyped_method(
		                                  di_lua_table_get, "__get", o, NULL));
		di_dtor(o, di_lua_table_dtor);
		ret = tmalloc(void *, 1);
		*t = DI_TYPE_OBJECT;
		*(void **)ret = o;
		return ret;
	case LUA_TNIL:
		*t = DI_TYPE_NIL;
		return NULL;
	type_error:
	default: *t = DI_LAST_TYPE; return NULL;
	}
#undef ret_arg
#undef tostringdup
}

static int _di_lua_method_handler(lua_State *L, struct di_method *m) {
	int nargs = lua_gettop(L);

	void **args = calloc(nargs, sizeof(void *));
	di_type_t *atypes = calloc(nargs, sizeof(di_type_t));
	int argi = 0;
	// Translate lua arguments
	for (int i = 1; i <= nargs; i++) {
		args[i - 1] = di_lua_type_to_di(L, i, atypes + i - 1);
		if (atypes[i - 1] >= DI_LAST_TYPE) {
			argi = i;
			goto err;
		}
	}

	void *ret;
	di_type_t rtype;
	int nret = di_call_callable((void *)m, &rtype, &ret, nargs, atypes,
	                            (const void *const *)args);

	if (nret == 0) {
		nret = di_lua_pushany(L, rtype, ret);
		di_free_value(rtype, ret);
	} else
		argi = -1;

err:
	for (int i = 0; i < nargs; i++)
		di_free_value(atypes[i], args[i]);
	free(args);
	free(atypes);
	if (argi > 0)
		return luaL_argerror(L, argi, "Unhandled lua type");
	else if (argi != 0)
		return luaL_error(L, "Failed to call function %s %d %s", m->name,
		                  argi, strerror(-nret));
	else
		return nret;
}

static int di_lua_method_handler(lua_State *L) {
	struct di_method *m = lua_touserdata(L, lua_upvalueindex(1));
	return _di_lua_method_handler(L, m);
}

static void
di_lua_general_callback(struct di_signal *sig, struct di_listener *l, void **data) {
	struct di_lua_listener *ud = di_get_listener_user_data(l);
	unsigned int nargs;
	auto ts = di_get_signal_arg_types(sig, &nargs);

	// ud might be freed during pcall
	lua_State *L = ud->s->m->L;
	struct di_lua_script *s = ud->s;
	// Prevent script object from being freed during pcall
	di_ref_object((void *)s);

	lua_pushcfunction(L, di_lua_errfunc);

	di_lua_xchg_env(L, s);

	// Get the function
	lua_rawgeti(L, LUA_REGISTRYINDEX, ud->fnref);
	// Push arguments
	for (unsigned int i = 0; i < nargs; i++)
		di_lua_pushany(L, ts[i], data[i]);

	lua_pcall(L, nargs, 0, -nargs - 2);

	di_lua_xchg_env(L, s);

	di_unref_object((void *)&s);
}

static void free_lua_listener(struct di_lua_listener **l) {
	auto ll = *l;

	list_del(&ll->sibling);
	luaL_unref(ll->s->m->L, LUA_REGISTRYINDEX, ll->fnref);
	free(ll->signame);
	di_unref_object((void *)&ll->s);
	free(ll);
}

static int di_lua_add_listener(lua_State *L) {
	if (lua_gettop(L) != 2)
		return luaL_error(L, "'on' only takes 2 arguments");

	struct di_object *o = lua_touserdata(L, lua_upvalueindex(1));
	const char *signame = luaL_checklstring(L, 1, NULL);
	if (lua_type(L, -1) != LUA_TFUNCTION)
		return luaL_argerror(L, 2, "not a function");

	auto ll = tmalloc(struct di_lua_listener, 1);
	ll->signame = strdup(signame);
	ll->o = o;
	ll->fnref = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
	lua_rawget(L, LUA_REGISTRYINDEX);
	di_lua_get_env(L, ll->s);

	ll->l = di_add_untyped_listener(o, signame, ll, (free_fn_t)free_lua_listener,
	                                di_lua_general_callback);

	list_add(&ll->sibling, &ll->s->listeners);

	di_ref_object((void *)ll->s);

	lua_pushlightuserdata(L, ll);

	return 1;
}

static int di_lua_remove_listener(lua_State *L) {
	if (lua_gettop(L) != 1)
		return luaL_error(L, "'remove_listener' takes 1 argument");
	struct di_lua_listener *ll = lua_touserdata(L, 1);
	if (ll == NULL)
		return luaL_error(L, "Listener handle is NULL");

	di_remove_listener(ll->o, ll->signame, ll->l);
	return 0;
}

static int di_lua_call_method(lua_State *L) {
	struct di_object *o = lua_touserdata(L, lua_upvalueindex(1));
	const char *name = luaL_checklstring(L, 1, NULL);
	struct di_method *m = di_find_method(o, name);
	if (!m)
		return luaL_error(L, "method %s not found", name);

	lua_remove(L, 1);
	return _di_lua_method_handler(L, m);
}

static int di_lua_gc(lua_State *L) {
	struct di_lua_object *lo = di_lua_checklobject(L, 1);
	// fprintf(stderr, "lua gc %p\n", lo);
	if (lo->o) {
		di_unref_object(&lo->o);
		list_del(&lo->sibling);
	}
	free(lo);
	return 0;
}

static void di_lua_create_metatable_for_object(lua_State *L, const luaL_Reg *reg);

const luaL_Reg di_lua_methods[] = {
    {"__index", di_lua_getter},
    {"__newindex", di_lua_setter},
    {"__gc", di_lua_gc},
    {0, 0},
};

static void di_lua_create_metatable_for_object(lua_State *L, const luaL_Reg *reg) {
	lua_newtable(L);
	luaL_setfuncs(L, reg, 0);
	lua_pushliteral(L, "__deai");
	lua_pushboolean(L, true);
	lua_rawset(L, -3);
	lua_setmetatable(L, -2);
}

static struct di_lua_object *
di_lua_pushobject(lua_State *L, struct di_object *o, const luaL_Reg *reg) {
	// struct di_lua_script *s;
	void **ptr;
	struct di_lua_object *lo;
	ptr = lua_newuserdata(L, sizeof(void *));

	lo = tmalloc(struct di_lua_object, 1);
	assert(o);
	lo->o = o;
	di_ref_object(o);

	*ptr = lo;
	di_lua_create_metatable_for_object(L, reg);
	return lo;
}

// d is not freed by this function
static int di_lua_pushany(lua_State *L, di_type_t t, void *d) {
	// Check for nil
	if (t == DI_TYPE_OBJECT || t == DI_TYPE_STRING || t == DI_TYPE_POINTER) {
		void *ptr = *(void **)d;
		if (ptr == NULL) {
			lua_pushnil(L);
			return 1;
		}
	}

	if (t == DI_TYPE_ARRAY) {
		struct di_array *tmp = d;
		if (tmp->elem_type == DI_TYPE_NIL) {
			lua_pushnil(L);
			return 1;
		}
	}

	lua_Integer i;
	lua_Number n;
	struct di_lua_object *lo;
	struct di_lua_script *s;
	struct di_array *arr;
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
		s = lua_touserdata(L, -1);
		lua_pop(L, 1);
		lo = di_lua_pushobject(L, *(void **)d, di_lua_methods);
		list_add(&lo->sibling, &s->objects);
		return 1;
	case DI_TYPE_STRING: lua_pushstring(L, *(const char **)d); return 1;
	case DI_TYPE_ARRAY:
		arr = (struct di_array *)d;
		step = di_sizeof_type(arr->elem_type);
		lua_createtable(L, arr->length, 0);
		for (int i = 0; i < arr->length; i++) {
			di_lua_pushany(L, arr->elem_type, arr->arr + step * i);
			lua_rawseti(L, -2, i + 1);
		}
		return 1;
	case DI_TYPE_CALLABLE:
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

	void **args = tmalloc(void *, top - 1);
	di_type_t *atypes = tmalloc(di_type_t, top - 1);

	for (int i = 2; i <= top; i++)
		args[i - 2] = di_lua_type_to_di(L, i, &atypes[i - 2]);

	int ret = di_emit_signal(o, signame, args);

	for (int i = 0; i < top - 1; i++)
		di_free_value(atypes[i], args[i]);
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
	} else if (strcmp(key, "remove_listener") == 0) {
		lua_pushcfunction(L, di_lua_remove_listener);
		return 1;
	}

	struct di_method *m = di_find_method(ud, key);
	if (!m) {
		// look for getter
		di_type_t rt;
		void *ret;
		int rc = di_getv(ud, key, &rt, &ret);
		if (rc != 0) {
			lua_pushnil(L);
			return 1;
		}
		rc = di_lua_pushany(L, rt, ret);
		di_free_value(rt, ret);
		return rc;
	}

	lua_pushlightuserdata(L, m);
	lua_pushcclosure(L, di_lua_method_handler, 1);

	return 1;
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

	int ret = di_setv(ud, key, vt, val);

	di_free_value(vt, val);
	if (ret != 0) {
		if (ret == -EINVAL)
			return luaL_error(L, "property %s type mismatch", key);
		if (ret == -ENOENT)
			return luaL_error(
			    L, "property %s doesn't exist or is read only", key);
	}
	return 0;
}

static void remove_all_listeners(struct di_lua_module *m) {
	struct di_lua_script *s;
	di_lua_get_env(m->L, s);
	di_lua_clear_listener(s);
}

static void di_lua_shutdown(struct di_lua_module *obj) {
	struct di_lua_script *s, *ns;
	list_for_each_entry_safe(s, ns, &obj->scripts, sibling)
	    di_lua_clear_listener(s);
	di_remove_listener((void *)obj->di, "shutdown", obj->shutdown_listener);
}

static void di_lua_dtor(struct di_lua_module *obj) {
	lua_close(obj->L);
}

const char *allowed_os[] = {"time", "difftime", "clock", "tmpname", "date", NULL};

PUBLIC int di_plugin_init(struct deai *di) {
	auto m = di_new_module_with_type("lua", struct di_lua_module);

	di_register_typed_method((void *)m, (di_fn_t)di_lua_load_script,
	                         "load_script", DI_TYPE_OBJECT, 1, DI_TYPE_STRING);
	di_register_typed_method((void *)m, (di_fn_t)remove_all_listeners,
	                         "remove_all_listeners", DI_TYPE_VOID, 0);

	di_register_typed_method((void *)m, (di_fn_t)di_lua_dtor, "__module_dtor",
	                         DI_TYPE_VOID, 0);

	m->L = luaL_newstate();
	luaL_openlibs(m->L);

	struct di_lua_object *lo =
	    di_lua_pushobject(m->L, (void *)di, di_lua_methods);
	lua_setglobal(m->L, "di");

	// Prevent the script from using os
	lua_getglobal(m->L, "os");
	lua_createtable(m->L, 0, 0);
	for (int i = 0; allowed_os[i]; i++) {
		lua_pushstring(m->L, allowed_os[i]);
		lua_pushstring(m->L, allowed_os[i]);
		lua_rawget(m->L, -4);
		lua_rawset(m->L, -3);
	}
	lua_setglobal(m->L, "os");
	lua_pop(m->L, 1);

	// make di_lua_gc happy
	INIT_LIST_HEAD(&m->ldi);
	list_add(&lo->sibling, &m->ldi);

	INIT_LIST_HEAD(&m->scripts);

	di_register_module(di, (void *)m);

	di_ref_object((void *)m);
	m->shutdown_listener = di_add_typed_listener((void *)di, "shutdown", m,
	                                             (free_fn_t)di_cleanup_objectp,
	                                             (di_fn_t)di_lua_shutdown);
	m->di = di;

	return 0;
}
