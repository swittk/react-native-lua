#pragma once

#include "LuaValueReader.h"

#include <jsi/jsi.h>

#include <string>
#include <vector>

namespace rnlua {

struct ValueInputRequest {
  bool globals = false;
  bool singular = false;
  ValueLimits limits;
  std::vector<std::string> names;
  std::vector<LuaValue> values;
};

// Snapshot JavaScript input completely before entering the Lua execution gate.
// JS accessors/proxies may run here; no Lua state is touched until this returns.
ValueInputRequest parseValueInputRequest(
    facebook::jsi::Runtime& runtime,
    const std::string& method,
    const facebook::jsi::Value* arguments,
    std::size_t count);

} // namespace rnlua
