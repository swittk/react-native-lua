#pragma once
#include "LuaValueReader.h"
#include <jsi/jsi.h>

namespace rnlua {

struct ValueReadRequest {
  bool globals = false;
  bool singular = false;
  ValueReadOptions options;
  std::vector<int> indices;
  std::vector<std::string> names;
};

// Parse JS arguments before entering Lua; JS getters may execute arbitrary JS.
ValueReadRequest parseValueReadRequest(
    facebook::jsi::Runtime& runtime, const std::string& method,
    const facebook::jsi::Value* arguments, std::size_t count);

// Materialize only after leaving/restoring Lua. Maps have null prototypes, so
// special keys (__proto__, constructor) remain ordinary own data properties.
facebook::jsi::Value valueReadResult(
    facebook::jsi::Runtime& runtime, const ValueReadRequest& request,
    const std::vector<LuaValue>& values, const facebook::jsi::Function& objectCreate);

} // namespace rnlua
