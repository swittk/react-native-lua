#pragma once

#include "LuaRuntime.h"

#include <cstddef>
#include <string>
#include <vector>

namespace rnlua {

// Bounds apply to the entire plural read, including repeated references.
struct ValueReadOptions {
  std::size_t maxDepth = 32;
  std::size_t maxEntries = 16'384;
  std::size_t maxStringBytes = 1'048'576;
  bool emptyTablesAsArrays = false;
};
constexpr std::size_t kMaxReadRoots = 256;
constexpr std::size_t kMaxReadDepth = 64;
constexpr std::size_t kMaxReadEntries = 262'144;
constexpr std::size_t kMaxReadStringBytes = 16 * 1024 * 1024;

// Owned snapshot: no lua_State pointers, references, or JSI handles escape.
// Objects use parallel keys/children; arrays use children alone.
struct LuaValue {
  enum class Kind { Null, Boolean, Number, String, Array, Object };
  Kind kind = Kind::Null;
  bool boolean = false;
  double number = 0;
  std::string text;
  std::vector<std::string> keys;
  std::vector<LuaValue> children;
};

void validateValueReadOptions(const ValueReadOptions& options);
bool isValidUtf8(const char* bytes, std::size_t length) noexcept;

// Caller owns the interpreter execution gate. Each function uses one protected
// Lua operation and leaves the original stack untouched on success or failure.
std::vector<LuaValue> readLuaValues(
    LuaRuntime& runtime, const std::vector<int>& indices,
    const ValueReadOptions& options = {});
std::vector<LuaValue> readLuaGlobals(
    LuaRuntime& runtime, const std::vector<std::string>& names,
    const ValueReadOptions& options = {});

} // namespace rnlua
