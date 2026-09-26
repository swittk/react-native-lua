#include "LuaSocket.h"

extern "C" {
#include "lua_src/lauxlib.h"
#include "lua_src/lua.h"
#include "lua_src/lualib.h"
#include "lua_luasocket/luasocket.h"
#include "lua_luasocket/mime.h"
#include "lua_luasocket/unix.h"
#include "lua_luasocket/luasocket_lua_amalgamation/luasocket_lua_amalgamation.h"

}

namespace rnlua {
namespace {

void addPreload(lua_State* state, const char* name, lua_CFunction loader) {
  luaL_getsubtable(state, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
  lua_pushcfunction(state, loader);
  lua_setfield(state, -2, name);
  lua_pop(state, 1);
}

void restrictPackageToPreloads(lua_State* state) {
  lua_getglobal(state, LUA_LOADLIBNAME);
  const int packageIndex = lua_gettop(state);

  /* Keep only Lua's preload searcher; filesystem and native loaders stay off. */
  lua_getfield(state, packageIndex, "searchers");
  const int oldSearchersIndex = lua_gettop(state);
  lua_createtable(state, 1, 0);
  const int bundledSearchersIndex = lua_gettop(state);
  lua_rawgeti(state, oldSearchersIndex, 1);
  lua_rawseti(state, bundledSearchersIndex, 1);
  lua_setfield(state, packageIndex, "searchers");
  lua_pop(state, 1);

  const char* disabledFields[] = {
      "loadlib", "searchpath", "path", "cpath", "config", nullptr};
  for (const char** field = disabledFields; *field != nullptr; ++field) {
    lua_pushnil(state);
    lua_setfield(state, packageIndex, *field);
  }
  lua_pop(state, 1);
}

} // namespace

void openLuaSocket(lua_State* state) {
  /* luaopen_package also installs require; it is locked down before scripts run. */
  luaL_requiref(state, LUA_LOADLIBNAME, luaopen_package, 1);
  lua_pop(state, 1);
  restrictPackageToPreloads(state);

  addPreload(state, "socket.core", luaopen_socket_core);
  addPreload(state, "mime.core", luaopen_mime_core);
  addPreload(state, "socket.unix", luaopen_socket_unix);
  /* SMTP needs date/time formatting; this bundled OS subset has no process,
   * environment, locale, or filesystem mutators (see lua_src/loslib.c). */
  addPreload(state, "_rnlua.socket.os", luaopen_os);
  luasocket_preload_luasrc_definitions(state);

  /* The loader remains captured by require, but package is not ambient. */
  lua_pushnil(state);
  lua_setglobal(state, LUA_LOADLIBNAME);
  luaL_getsubtable(state, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
  lua_pushnil(state);
  lua_setfield(state, -2, LUA_LOADLIBNAME);
  lua_pop(state, 1);
}

} // namespace rnlua
