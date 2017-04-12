#include <assert.h>
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdio.h>
#include <string.h>

#include "list.h"
#include <log.h>
#include <plugin.h>

#define tmalloc(type, nmem) (type *)calloc(nmem, sizeof(type))
#define auto __auto_type

#define DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY "__deai.di_lua.script_object"

struct di_lua_module {
	struct di_module;
	lua_State *L;
	struct di_object *log;

	struct list_head scripts;
};

struct di_lua_listener {
	struct di_listener *l;
	struct di_object *o;
	char *signame;
	struct list_head sibling;
};

struct di_lua_script {
	struct di_object;
	char *path;
	struct di_lua_module *m; // NULL means the lua module has been freed
	struct list_head listeners;
	struct list_head sibling;
};

struct di_lua_listener_data {
	lua_State *L;
	struct di_lua_script *s;
	int r;
};

static int di_lua_pushany(lua_State *L, di_type_t t, void *d);
static int di_lua_getter(lua_State *L);
static int di_lua_setter(lua_State *L);

static int di_lua_pcall_with_env(lua_State *L, int nargs, int nresults, int errfunc,
                                 struct di_lua_script *s) {
	lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
	lua_pushlightuserdata(L, s);
	lua_rawset(L, LUA_REGISTRYINDEX);

	int ret = lua_pcall(L, nargs, nresults, errfunc);

	// unset the script object
	lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
	lua_pushnil(L);
	lua_rawset(L, LUA_REGISTRYINDEX);

	return ret;
}

static int di_lua_errfunc(lua_State *L) {
	/* Convert error to string, to prevent a follow-up error with lua_concat. */
	auto err = luaL_tolstring(L, -1, NULL);

	lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
	lua_rawget(L, LUA_REGISTRYINDEX);
	struct di_lua_script *o = lua_touserdata(L, -1);

	if (!luaL_dostring(L, "return debug.traceback(\"error while running "
	                      "function!\", 3)")) {
		auto trace = lua_tostring(L, -1);
		di_log_va(o->m->log, DI_LOG_ERROR,
		          "Failed to run lua script %s: %s\n%s", o->path, err, trace);
	} else {
		auto err2 = luaL_tolstring(L, -1, NULL);
		di_log_va(o->m->log, DI_LOG_ERROR,
		          "Failed to run lua script %s: %s\n", o->path, err);
		di_log_va(o->m->log, DI_LOG_ERROR,
		          "Failed to generate stack trace %s\n", err2);
	}
	return 1;
}

static inline void _remove_listener(lua_State *L, struct di_lua_listener *ll) {
	struct di_lua_listener_data *ud = di_remove_listener(ll->o, ll->signame, ll->l);
	list_del(&ll->sibling);
	luaL_unref(L, LUA_REGISTRYINDEX, ud->r);
	free(ll->signame);
	free(ll);

	di_unref_object((void *)ud->s);
	free(ud);
}

static void di_lua_clear_listener(struct di_lua_script *s) {
	// Remove all listeners
	struct di_lua_listener *ll, *nll;
	list_for_each_entry_safe(ll, nll, &s->listeners, sibling)
		_remove_listener(s->m->L, ll);
}

static void di_lua_free_script(struct di_lua_script *s) {
	//fprintf(stderr, "free lua script %p, %s\n", s->m ,s->path);
	di_lua_clear_listener(s);
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
	auto s = di_new_object_with_type(struct di_lua_script);
	auto fn = di_create_typed_method((void *)di_lua_free_script, "__dtor",
	                                 DI_TYPE_VOID, 0);
	di_register_typed_method((void *)s, fn);
	s->m = NULL;

	struct di_lua_module *m = (void *)obj;

	lua_pushcfunction(m->L, di_lua_errfunc);

	if (luaL_loadfile(m->L, path)) {
		const char *err = lua_tostring(m->L, -1);
		di_log_va(m->log, DI_LOG_ERROR, "Failed to load lua script %s: %s\n",
		          path, err);
		di_unref_object((void *)s);
		lua_pop(m->L, 2);
		return NULL;
	}

	s->path = strdup(path);
	INIT_LIST_HEAD(&s->listeners);

	int ret = di_lua_pcall_with_env(m->L, 0, 0, -2, s);

	if (ret != 0) {
		di_unref_object((void *)s);

		// Pop error handling function
		lua_pop(m->L, 1);
		s = NULL;
	}

	list_add(&s->sibling, &m->scripts);
	// Don't hold ref. If lua module goes away first, script will become
	// defunct so that's fine.
	s->m = m;

	return (void *)s;
}

static void *di_lua_type_to_di(lua_State *L, int i, di_type_t *t);

static int
di_lua_table_to_array(lua_State *L, int index, int nelem, struct di_array *ret) {
	lua_pushinteger(L, 1);
	lua_rawget(L, index);

	di_type_t t;
	void *retd = di_lua_type_to_di(L, -1, &t);
	ret->elem_type = t;
	lua_pop(L, 1);

	size_t sz = di_sizeof_type(ret->elem_type);
	assert(sz != 0);
	ret->arr = calloc(nelem, sz);
	memcpy(ret->arr, ret, sz);
	free(retd);

	for (int i = 2; i <= nelem; i++) {
		lua_pushinteger(L, i);
		lua_rawget(L, index);

		retd = di_lua_type_to_di(L, -1, &t);
		lua_pop(L, 1);
		memcpy(ret->arr + sz * (i - 1), ret, sz);
		free(retd);
	}
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

static struct di_object *di_lua_checkobject(lua_State *L, int index) {
	if (di_lua_isobject(L, index))
		return *(struct di_object **)lua_touserdata(L, index);
	luaL_argerror(L, index, "not a di_object");
	__builtin_unreachable();
}

static int di_lua_checkarray(lua_State *L, int index) {
	lua_pushnil(L);
	if (lua_next(L, index) == 0)
		// Empty array
		return 0;

	int i = 1;

	// get arr[1]
	lua_pushinteger(L, i++);
	lua_rawget(L, index);

	di_type_t t0;
	void *ret = di_lua_type_to_di(L, -1, &t0);
	free(ret);
	// Pop 2 value, top of stack is the key
	lua_pop(L, 2);

	if (t0 == DI_TYPE_VOID || t0 >= DI_LAST_TYPE) {
		lua_pop(L, 1);
		return -1;
	}

	while (lua_next(L, index) != 0) {
		lua_pushinteger(L, i++);
		lua_rawget(L, index);

		di_type_t t;
		ret = di_lua_type_to_di(L, -1, &t);
		free(ret);
		// pop 2 value
		lua_pop(L, 2);
		if (t != t0) {
			// pop 1 key
			lua_pop(L, 1);
			return -1;
		}

		if (i == INT_MAX) {
			// Array too big
			lua_pop(L, 1);
			return i - 1;
		}
	}
	return i - 1;
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

	int nelem;
	void *ret;
	switch (lua_type(L, i)) {
	case LUA_TBOOLEAN:
		ret_arg(i, DI_TYPE_NUINT, unsigned int, lua_toboolean);
		break;
	case LUA_TNUMBER:
		if (lua_isinteger(L, i))
			ret_arg(i, DI_TYPE_INT, int64_t, lua_tointeger);
		else
			ret_arg(i, DI_TYPE_FLOAT, double, lua_tonumber);
	case LUA_TSTRING: ret_arg(i, DI_TYPE_STRING, const char *, lua_tostring);
	case LUA_TUSERDATA:
		if (!di_lua_isobject(L, i))
			goto type_error;
		ret_arg(i, DI_TYPE_OBJECT, void *, *(void **)lua_touserdata);
	case LUA_TTABLE:
		// Must be a array
		if ((nelem = di_lua_checkarray(L, i)) < 0)
			goto type_error;
		*t = DI_TYPE_ARRAY;
		ret = calloc(1, sizeof(struct di_array));
		di_lua_table_to_array(L, i, nelem, ret);
		return ret;
	type_error:
	default: *t = DI_LAST_TYPE; return NULL;
	}
#undef ret_arg
}

static int _di_lua_method_handler(lua_State *L, struct di_method *m) {
	int nargs = lua_gettop(L);

	void **args = calloc(nargs, sizeof(void *));
	di_type_t *atypes = calloc(nargs, sizeof(di_type_t));
	int argi = 0;
	// Translate lua arguments
	for (int i = 1; i <= nargs; i++) {
		args[i - 1] = di_lua_type_to_di(L, i, atypes + i - 1);
		if (!args[i - 1] || atypes[i - 1] >= DI_LAST_TYPE) {
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
		free(ret);
	} else
		argi = -1;

err:
	for (int i = 0; i < nargs; i++)
		free(args[i]);
	free(args);
	free(atypes);
	if (argi > 0)
		return luaL_argerror(L, argi, "Unhandled lua type");
	else if (argi != 0)
		return luaL_error(L, "Failed to call function %s", m->name);
	else
		return nret;
}

static int di_lua_method_handler(lua_State *L) {
	struct di_method *m = lua_touserdata(L, lua_upvalueindex(1));
	return _di_lua_method_handler(L, m);
}

static void di_lua_general_callback(struct di_signal *sig, void **data) {
	auto ld = *(struct di_listener_data **)data[0];
	struct di_lua_listener_data *ud = ld->user_data;
	unsigned int nargs;
	auto ts = di_get_signal_arg_types(sig, &nargs);

	lua_pushcfunction(ud->L, di_lua_errfunc);

	// Get the function
	lua_rawgeti(ud->L, LUA_REGISTRYINDEX, ud->r);
	// Push arguments
	for (unsigned int i = 1; i <= nargs; i++) {
		di_lua_pushany(ud->L, ts[i - 1], data[i]);
		if (ts[i - 1] == DI_TYPE_OBJECT) {
			// hold reference
			struct di_object *o = *(void **)data[i];
			di_ref_object(o);
		}
	}

	// ud might be freed during pcall
	struct di_lua_script *s = ud->s;
	// Prevent script object from being freed during pcall
	di_ref_object((void *)s);
	di_lua_pcall_with_env(ud->L, nargs, 0, -nargs - 2, s);
	di_unref_object((void *)s);
}

static int di_lua_add_listener(lua_State *L) {
	if (lua_gettop(L) != 2)
		return luaL_error(L, "'on' only takes 2 arguments");

	struct di_object *o = lua_touserdata(L, lua_upvalueindex(1));
	const char *signame = luaL_checklstring(L, 1, NULL);
	if (lua_type(L, -1) != LUA_TFUNCTION)
		return luaL_argerror(L, 2, "not a function");

	auto ud = tmalloc(struct di_lua_listener_data, 1);
	ud->r = luaL_ref(L, LUA_REGISTRYINDEX);
	ud->L = L;

	lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
	lua_rawget(L, LUA_REGISTRYINDEX);
	ud->s = lua_touserdata(L, -1);

	struct di_listener *l =
	    di_add_untyped_listener(o, signame, ud, di_lua_general_callback);

	auto ll = tmalloc(struct di_lua_listener, 1);
	ll->l = l;
	ll->signame = strdup(signame);
	ll->o = o;
	list_add(&ll->sibling, &ud->s->listeners);

	di_ref_object((void *)ud->s);

	lua_pushlightuserdata(L, ll);

	return 1;
}

static int di_lua_remove_listener(lua_State *L) {
	if (lua_gettop(L) != 1)
		return luaL_error(L, "'remove_listener' takes 1 argument");
	struct di_lua_listener *ll = lua_touserdata(L, 1);
	if (ll == NULL)
		return luaL_error(L, "Listener handle is NULL");

	_remove_listener(L, ll);
	return 0;
}

static int di_lua_call_method(lua_State *L) {
	struct di_object *o = lua_touserdata(L, lua_upvalueindex(1));
	const char *name = luaL_checklstring(L, 1, NULL);
	struct di_method *m = di_find_method(o, name);
	if (!m)
		return 0;

	lua_remove(L, 1);
	return _di_lua_method_handler(L, m);
}

static int di_lua_gc(lua_State *L) {
	struct di_object *o = di_lua_checkobject(L, 1);
	lua_pushliteral(L, DI_LUA_REGISTRY_SCRIPT_OBJECT_KEY);
	lua_rawget(L, LUA_REGISTRYINDEX);
	struct di_lua_script *s = lua_touserdata(L, -1);
	//fprintf(stderr, "lua gc\n");
	di_unref_object(o);
	return 0;
}

static void di_lua_create_metatable_for_object(lua_State *L, const luaL_Reg *reg);

const luaL_Reg di_lua_methods[] = {
    {"__index", di_lua_getter},
    {"__newindex", di_lua_setter},
    {"__gc", di_lua_gc},
    {0, 0},
};

static int di_lua_module_getter(lua_State *L) {
	if (lua_gettop(L) != 2)
		return luaL_error(L, "wrong number of arguments to __index");

	const char *key = luaL_checkstring(L, 2);
	struct deai *di = (void *)di_lua_checkobject(L, 1);

	struct di_module *dm = di_find_module(di, key);
	if (!dm)
		return luaL_error(L, "not such module: %s", key);

	void **xm = lua_newuserdata(L, sizeof(void *));
	*xm = dm;
	di_lua_create_metatable_for_object(L, di_lua_methods);

	return 1;
}

const luaL_Reg di_lua_di_methods[] = {
    {"__index", di_lua_module_getter}, {0, 0},
};

static void di_lua_create_metatable_for_object(lua_State *L, const luaL_Reg *reg) {
	lua_newtable(L);
	luaL_setfuncs(L, reg, 0);
	lua_pushliteral(L, "__deai");
	lua_pushboolean(L, true);
	lua_rawset(L, -3);
	lua_setmetatable(L, -2);
}

static int di_lua_pushany(lua_State *L, di_type_t t, void *d) {
	lua_Integer i;
	lua_Number n;
	void **ptr;
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
		ptr = lua_newuserdata(L, sizeof(void *));
		*ptr = *(void **)d;
		di_lua_create_metatable_for_object(L, di_lua_methods);
		return 1;
	case DI_TYPE_STRING: lua_pushstring(L, *(const char **)d); return 1;
	case DI_TYPE_ARRAY:
	// not implemented
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

	void **args = alloca(sizeof(void *) * (top - 1));
	di_type_t *atypes = alloca(sizeof(di_type_t) * (top - 1));

	for (int i = 2; i <= top; i++)
		args[i - 2] = di_lua_type_to_di(L, i, &atypes[i - 2]);

	int ret = di_emit_signal(o, signame, args);

	for (int i = 0; i < top - 1; i++)
		free(args[i]);

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
		lua_pushcclosure(L, di_lua_add_listener, 1);
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
		const size_t bsz = strlen(key) + 7;
		char *buf = malloc(bsz);
		snprintf(buf, bsz, "__get_%s", key);
		m = di_find_method(ud, buf);
		free(buf);

		if (!m)
			return luaL_error(L, "neither a method or a property with "
			                     "name %s can be found",
			                  key);

		di_type_t rt;
		void *ret;
		int status = di_call_callable_v((void *)m, &rt, &ret, DI_LAST_TYPE);
		if (status != 0) {
			lua_pushnil(L);
			return 1;
		}
		status = di_lua_pushany(L, rt, ret);
		free(ret);
		return status;
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

	const size_t bsz = strlen(key) + 7;
	char *buf = malloc(bsz);
	snprintf(buf, bsz, "__set_%s", key);
	struct di_method *m = di_find_method(ud, buf);
	free(buf);

	if (m) {
		// remove key and table
		lua_remove(L, 1);
		lua_remove(L, 1);
		return _di_lua_method_handler(L, m);
	}

	return luaL_error(L, "property %s doesn't exist", key);
}

static void di_lua_dtor(struct di_lua_module *obj) {
	lua_close(obj->L);
	di_unref_object((void *)obj->log);

	struct di_lua_script *s, *ns;
	list_for_each_entry_safe(s, ns, &obj->scripts, sibling) {
		di_ref_object((void *)s);
		di_destroy_object((void *)s);
		di_unref_object((void *)s);
	}
}

PUBLIC int di_plugin_init(struct deai *di) {
	struct di_object *log = (void *)di_find_module(di, "log");
	if (!log)
		return -1;

	auto m = di_new_module_with_type("lua", struct di_lua_module);
	m->log = log;

	auto fn = di_create_typed_method((di_fn_t)di_lua_load_script, "load_script",
	                                 DI_TYPE_OBJECT, 1, DI_TYPE_STRING);

	auto dtor = di_create_typed_method((di_fn_t)di_lua_dtor, "__module_dtor",
	                                   DI_TYPE_VOID, 0);

	if (di_register_typed_method((void *)m, (void *)fn) != 0)
		goto out;
	fn = NULL;

	if (di_register_typed_method((void *)m, (void *)dtor) != 0)
		goto out;
	dtor = NULL;

	m->L = luaL_newstate();
	luaL_openlibs(m->L);

	void **xdi = lua_newuserdata(m->L, sizeof(void *));
	*xdi = (void *)di;
	di_lua_create_metatable_for_object(m->L, di_lua_di_methods);
	lua_setglobal(m->L, "di");
	INIT_LIST_HEAD(&m->scripts);

	di_register_module(di, (void *)m);

out:
	free(fn);
	free(dtor);
	di_unref_object((void *)m);
	return 0;
}
