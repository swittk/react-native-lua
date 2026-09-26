#pragma once

struct lua_State;

namespace rnlua {

/** Registers the bundled LuaSocket modules using an in-memory-only loader. */
void openLuaSocket(lua_State* state);

} // namespace rnlua
