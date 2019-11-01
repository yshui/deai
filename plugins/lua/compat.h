#pragma once
#include <lauxlib.h>
#include <lua.h>
#ifdef NEED_LUAL_SETFUNCS
/*
 * set functions from list 'l' into table at top - 'nup'; each
 * function gets the 'nup' elements at the top as upvalues.
 * Returns with only the table at the stack.
 */
void luaL_setfuncs(lua_State *L, const luaL_Reg *l, int nup);
#endif

#ifdef NEED_LUA_ISINTEGER
/*
 * Returns 1 if the value at the given index is an integer (that is, the value is a
 * number and is represented as an integer), and 0 otherwise.
 */
int lua_isinteger(lua_State *L, int index);
#endif

#ifdef NEED_LUAL_TOLSTRING
const char *luaL_tolstring(lua_State *L, int idx, size_t *len);
#endif
