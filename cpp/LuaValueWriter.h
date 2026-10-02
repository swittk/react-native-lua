#pragma once

#include "LuaValueReader.h"

#include <string>
#include <vector>

namespace rnlua {

// Caller owns the interpreter execution gate.
void pushLuaValues(
    LuaRuntime& runtime,
    const std::vector<LuaValue>& values,
    const ValueLimits& limits = {});

// Raw global assignment; no __newindex. All requested assignments are staged
// before mutation. On a commit failure, previously changed globals are restored.
void setLuaGlobals(
    LuaRuntime& runtime,
    const std::vector<std::string>& names,
    const std::vector<LuaValue>& values,
    const ValueLimits& limits = {});

} // namespace rnlua
