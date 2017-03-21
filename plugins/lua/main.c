#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
#include <stdio.h>
#include <string.h>

#include <log.h>
#include <plugin.h>

#define tmalloc(type, nmem) (type *)calloc(nmem, sizeof(type))
#define auto __auto_type

static struct di_object *logm;

struct di_lua_module {
	struct di_module;
	lua_State *L;
	struct di_object *log;
};

struct di_lua_script {
	struct di_object;
	const char *path;
	struct di_lua_module *m;
};

static int di_lua_pushany(lua_State *L, di_type_t t, void *d);
static int di_lua_getter(lua_State *L);
static int di_lua_setter(lua_State *L);

static int di_lua_errfunc(lua_State *L) {
	/* Convert error to string, to prevent a follow-up error with lua_concat. */
	auto err = luaL_tolstring(L, -1, NULL);
	lua_pop(L, 1);

	struct di_lua_script *o = lua_touserdata(L, lua_upvalueindex(1));

	if (!luaL_dostring(L, "return debug.traceback(\"error while running "
	                      "function!\", 3)")) {
		auto trace = lua_tostring(L, -1);
		di_log_va(o->m->log, DI_LOG_ERROR,
		          "Failed to run lua script %s: %s\n%s", o->path, err, trace);
	} else {
		auto err2 = luaL_tolstring(L, -1, NULL);
		lua_pop(L, 1);
		di_log_va(o->m->log, DI_LOG_ERROR,
		          "Failed to run lua script %s: %s\n", o->path, err);
		di_log_va(o->m->log, DI_LOG_ERROR,
		          "Failed to generate stack trace %s\n", err2);
	}
	return 1;
}

static struct di_object *di_lua_load_script(struct di_object *obj, const char *path) {
	auto s = tmalloc(struct di_lua_script, 1);
	struct di_lua_module *m = (void *)obj;

	if (luaL_loadfile(m->L, path)) {
		const char *err = lua_tostring(m->L, -1);
		di_log_va(m->log, DI_LOG_ERROR, "Failed to load lua script %s: %s\n",
		          path, err);
		free(s);
		lua_pop(m->L, 1);
		return NULL;
	}

	lua_pushlightuserdata(m->L, obj);
	lua_pushcclosure(m->L, di_lua_errfunc, 1);
	lua_insert(m->L, -2);

	s->path = strdup(path);

	if (lua_pcall(m->L, 0, 0, -2)) {
		free(s);

		// Pop error handling function
		lua_pop(m->L, 1);
		return NULL;
	}

	return (void *)s;
}

static int _di_lua_method_handler(lua_State *L, struct di_method *m) {
	int nargs = lua_gettop(L);

	void **args = calloc(nargs, sizeof(void *));
	di_type_t *atypes = calloc(nargs, sizeof(di_type_t));

#define set_arg(i, t, t2, gfn)                                                      \
	do {                                                                        \
		atypes[i-1] = t;                                                      \
		args[i-1] = alloca(sizeof(t2));                                       \
		*(t2 *)args[i-1] = gfn(L, i);                                         \
	} while (0)

	// Translate lua arguments
	for (int i = 1; i <= nargs; i++) {
		switch (lua_type(L, i)) {
		case LUA_TBOOLEAN:
			set_arg(i, DI_TYPE_UINT8, uint8_t, lua_toboolean);
			break;
		case LUA_TNUMBER:
			if (lua_isinteger(L, i))
				set_arg(i, DI_TYPE_INT64, int64_t, lua_tointeger);
			else
				set_arg(i, DI_TYPE_DOUBLE, double, lua_tonumber);
			break;
			// TODO
		case LUA_TSTRING:
			set_arg(i, DI_TYPE_STRING, const char *, lua_tostring);
			break;
		default:
			di_log_va(logm, DI_LOG_ERROR, "Unhandled lua type at %d: %d\n", i, lua_type(L, i));
			break;
		}
	}
#undef set_arg

	void *ret;
	di_type_t rtype;
	int nret = di_call_callable((void *)m, &rtype, &ret, nargs, atypes,
	                 (const void *const *)args);

	free(args);
	free(atypes);

	if (nret == 0) {
		di_lua_pushany(L, rtype, ret);
		free(ret);
		return nret;
	}
	// XXX: error occured
	return 0;
}

static int di_lua_method_handler(lua_State *L) {
	struct di_method *m = lua_touserdata(L, lua_upvalueindex(1));
	return _di_lua_method_handler(L, m);
}

static int di_lua_add_listener(lua_State *L) {
	// TODO

	return 0;
}

static int di_lua_call_method(lua_State *L) {
	luaL_argcheck(L, lua_isuserdata(L, 1), 1, "expecting userdata");
	luaL_argcheck(L, lua_isstring(L, 2), 2, "expecting string");

	struct di_object *o = *(void **)lua_touserdata(L, 1);
	const char *name = lua_tostring(L, 2);
	struct di_method *m = di_find_method(o, name);

	if (!m)
		return 0;

	return _di_lua_method_handler(L, m);
}

const luaL_Reg di_lua_methods[] = {
    {"__index", di_lua_getter}, {"__newindex", di_lua_setter}, {0, 0},
};

static void di_lua_create_metatable_for_object(lua_State *L) {
	lua_newtable(L);
	luaL_setfuncs(L, di_lua_methods, 0);
	lua_setmetatable(L, -2);
}

static int di_lua_pushany(lua_State *L, di_type_t t, void *d) {
	lua_Integer i;
	lua_Number n;
	void **ptr;
	switch (t) {
	case DI_TYPE_UINT8: i = *(uint8_t *)d; goto pushint;
	case DI_TYPE_UINT16: i = *(uint16_t *)d; goto pushint;
	case DI_TYPE_UINT32: i = *(uint32_t *)d; goto pushint;
	case DI_TYPE_UINT64: i = *(uint64_t *)d; goto pushint;
	case DI_TYPE_INT8: i = *(int8_t *)d; goto pushint;
	case DI_TYPE_INT16: i = *(int16_t *)d; goto pushint;
	case DI_TYPE_INT32: i = *(int32_t *)d; goto pushint;
	case DI_TYPE_INT64: i = *(int64_t *)d; goto pushint;
	case DI_TYPE_FLOAT: n = *(float *)d; goto pushnumber;
	case DI_TYPE_DOUBLE: n = *(double *)d; goto pushnumber;
	case DI_TYPE_POINTER:
		// bad idea
		lua_pushlightuserdata(L, *(void **)d);
		return 1;
	case DI_TYPE_OBJECT:
		ptr = lua_newuserdata(L, sizeof(void *));
		*ptr = *(void **)d;
		di_lua_create_metatable_for_object(L);
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

static int di_lua_getter(lua_State *L) {
	if (lua_gettop(L) != 2)
		return luaL_error(L, "wrong number of arguments to __index");

	const char *key = lua_tostring(L, 2);
	lua_pop(L, 1);
	if (strcmp(key, "on") == 0) {
		lua_pushcfunction(L, di_lua_add_listener);
		return 1;
	}
	if (strcmp(key, "call") == 0) {
		lua_pushcfunction(L, di_lua_call_method);
		return 1;
	}

	struct di_object *ud = *(void **)lua_touserdata(L, 1);
	lua_pop(L, 1);

	struct di_method *m = di_find_method(ud, key);
	if (!m) {
		// look for getter
		const size_t bsz = strlen(key) + 7;
		char *buf = malloc(bsz);
		snprintf(buf, bsz, "__get_%s", key);
		m = di_find_method(ud, buf);
		free(buf);

		if (!m) {
			lua_pushnil(L);
			return 1;
		}

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

	return 0;
}

static void di_lua_add_module(lua_State *L, struct di_module *m) {
	lua_pushstring(L, m->name);
	void **xm = lua_newuserdata(L, sizeof(void *));
	*xm = m;
	di_lua_create_metatable_for_object(L);
	lua_rawset(L, -3);
}

int di_plugin_init(struct deai *di) {
	struct di_object *log = (void *)di_find_module(di, "log");
	if (!log)
		return -1;

	auto m = di_new_module_with_type("lua", struct di_lua_module);
	m->log = log;
	logm = log;

	auto fn = di_create_typed_method((di_fn_t)di_lua_load_script, "load_script",
	                                 DI_TYPE_OBJECT, 1, DI_TYPE_STRING);

	if (di_register_typed_method((void *)m, (void *)fn) != 0) {
		free(fn);
		di_free_module((void *)m);
	}

	m->L = luaL_newstate();
	luaL_openlibs(m->L);

	lua_newtable(m->L);
	// Loop over modules
	struct di_module *dm = di_get_modules(di);
	while (dm) {
		di_lua_add_module(m->L, dm);
		dm = di_next_module(dm);
	}

	lua_setglobal(m->L, "di");

	di_register_module(di, (void *)m);
	return 0;
}
