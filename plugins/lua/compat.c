#include <lauxlib.h>
#include <lua.h>

#include "compat.h"

#ifdef NEED_LUAL_SETFUNCS
/*
** set functions from list 'l' into table at top - 'nup'; each
** function gets the 'nup' elements at the top as upvalues.
** Returns with only the table at the stack.
*/
void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup) {
	luaL_checkstack(L, nup, "too many upvalues");
	// fill the table with given functions
	for (; l->name != NULL; l++) {
		int i;
		if (l->func == NULL) {
			// place holder?
			lua_pushboolean(L, 0);
		} else {
			// copy upvalues to the top
			for (i = 0; i < nup; i++) {
				lua_pushvalue(L, -nup);
			}

			// closure with those upvalues
			lua_pushcclosure(L, l->func, nup);
		}
		lua_setfield(L, -(nup + 2), l->name);
	}
	// remove upvalues
	lua_pop(L, nup);
}
#endif        // NEED_LUAL_SETFUNCS

#ifdef NEED_LUA_ISINTEGER
/*
 * Returns 1 if the value at the given index is an integer (that is, the value is a
 * number and is represented as an integer), and 0 otherwise.
 */
int lua_isinteger(lua_State *L, int index) {
	if (lua_type(L, index) == LUA_TNUMBER) {
		lua_Number n = lua_tonumber(L, index);
		lua_Integer i = lua_tointeger(L, index);
		if (i == n)
			return 1;
	}
	return 0;
}
#endif        // NEED_LUA_ISINTEGER

#ifdef NEED_LUAL_TOLSTRING
const char *luaL_tolstring(lua_State *L, int idx, size_t *len) {
	// metafield?
	if (luaL_callmeta(L, idx, "__tostring")) {
		if (!lua_isstring(L, -1)) {
			luaL_error(L, "'__tostring' must return a string");
		}
	} else {
		switch (lua_type(L, idx)) {
		case LUA_TNUMBER: {
#ifndef NEED_LUA_ISINTEGER
			// Normally, has the integer type means we are using lua 5.3,
			// which doesn't need luaL_tolstring. But let's put this here
			// just in case
			if (lua_isinteger(L, idx)) {
				lua_pushfstring(L, "%I", (LUAI_UACINT)lua_tointeger(L, idx));
				break;
			}
#endif
			lua_pushfstring(L, "%f", (LUAI_UACNUMBER)lua_tonumber(L, idx));
			break;
		}
		case LUA_TSTRING: lua_pushvalue(L, idx); break;
		case LUA_TBOOLEAN:
			lua_pushstring(L, (lua_toboolean(L, idx) ? "true" : "false"));
			break;
		case LUA_TNIL: lua_pushliteral(L, "nil"); break;
		default: {
			int tt = luaL_getmetafield(L, idx, "__name");
			const char *kind = (tt == LUA_TSTRING) ? lua_tostring(L, -1)
			                                       : luaL_typename(L, idx);
			lua_pushfstring(L, "%s: %p", kind, lua_topointer(L, idx));
			if (tt != LUA_TNIL) {
				// remove '__name'
				lua_remove(L, -2);
			}
			break;
		}
		}
	}
	return lua_tolstring(L, -1, len);
}
#endif        // NEED_LUAL_TOLSTRING
