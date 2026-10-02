#include "LuaValueReader.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <utility>

extern "C" {
#include "lua_src/lua.h"
}

namespace rnlua {

bool isValidUtf8(const char* bytes, std::size_t length) noexcept {
  const auto* s = reinterpret_cast<const unsigned char*>(bytes);
  for (std::size_t i = 0; i < length;) {
    const unsigned char lead = s[i++];
    if (lead < 0x80) continue;
    unsigned int value = 0;
    unsigned int minimum = 0;
    std::size_t trailing = 0;
    if (lead >= 0xc2 && lead <= 0xdf) {
      value = lead & 0x1f; minimum = 0x80; trailing = 1;
    } else if (lead >= 0xe0 && lead <= 0xef) {
      value = lead & 0x0f; minimum = 0x800; trailing = 2;
    } else if (lead >= 0xf0 && lead <= 0xf4) {
      value = lead & 0x07; minimum = 0x10000; trailing = 3;
    } else {
      return false;
    }
    if (trailing > length - i) return false;
    while (trailing--) {
      const unsigned char c = s[i++];
      if ((c & 0xc0) != 0x80) return false;
      value = (value << 6) | (c & 0x3f);
    }
    if (value < minimum || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) return false;
  }
  return true;
}

void validateValueReadOptions(const ValueReadOptions& options) {
  if (options.maxDepth > kMaxReadDepth ||
      options.maxEntries > kMaxReadEntries ||
      options.maxStringBytes > kMaxReadStringBytes) {
    throw std::invalid_argument("Lua bulk read options exceed hard safety limits");
  }
}

namespace {

[[noreturn]] void invalid(const char* message) {
  throw std::runtime_error(std::string("Lua bulk read: ") + message);
}

// All owning C++ state lives outside luaD_pcall. No JSI operations run here.
// After reserving stack space, traversal only uses non-allocating raw Lua API
// calls. Only root-name pushes may allocate in Lua; they hold no local RAII
// values across a possible Lua longjmp. C++ failures are caught by snapshot().
struct Reader {
  const ValueReadOptions& options;
  const std::vector<int>* indices;
  const std::vector<std::string>* names;
  std::vector<LuaValue> values;
  std::vector<const void*> ancestors;
  std::size_t entries = 0;
  std::size_t stringBytes = 0;
  std::exception_ptr failure;

  Reader(const ValueReadOptions& limits, const std::vector<int>* stackIndices,
         const std::vector<std::string>* globalNames)
      : options(limits), indices(stackIndices), names(globalNames),
        values(stackIndices ? stackIndices->size() : globalNames->size()) {
    ancestors.reserve(options.maxDepth);
  }

  void countString(const char* text, std::size_t length) {
    if (length > options.maxStringBytes - stringBytes)
      invalid("maxStringBytes exceeded (keys and values share one byte budget)");
    if (!isValidUtf8(text, length)) invalid("strings and keys must be valid UTF-8");
    stringBytes += length;
  }

  void read(lua_State* L, int index, LuaValue& output, std::size_t depth) {
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
      case LUA_TNIL: return;
      case LUA_TBOOLEAN:
        output.kind = LuaValue::Kind::Boolean;
        output.boolean = lua_toboolean(L, index) != 0;
        return;
      case LUA_TNUMBER: {
        const double number = lua_tonumber(L, index);
        if (!std::isfinite(number)) invalid("numbers must be finite");
        if (lua_isinteger(L, index)) {
          const lua_Integer integer = lua_tointeger(L, index);
          if (integer < -9007199254740991LL || integer > 9007199254740991LL)
            invalid("integer is outside JavaScript's safe integer range");
        }
        output.kind = LuaValue::Kind::Number;
        output.number = number;
        return;
      }
      case LUA_TSTRING: {
        std::size_t length = 0;
        const char* text = lua_tolstring(L, index, &length);
        countString(text, length);
        output.kind = LuaValue::Kind::String;
        output.text.assign(text, length);
        return;
      }
      case LUA_TTABLE:
        readTable(L, index, output, depth);
        return;
      default: invalid("functions, userdata and coroutines are not transferable values");
    }
  }

  void readTable(lua_State* L, int index, LuaValue& output, std::size_t depth) {
    if (depth >= options.maxDepth) invalid("maxDepth exceeded");
    const void* identity = lua_topointer(L, index);
    if (std::find(ancestors.begin(), ancestors.end(), identity) != ancestors.end())
      invalid("cyclic table");
    ancestors.push_back(identity);

    // Inspect every raw key. rawlen alone silently truncates sparse/mixed tables.
    std::size_t count = 0;
    lua_Integer highest = 0;
    bool numeric = false;
    bool named = false;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
      if (entries >= options.maxEntries) invalid("maxEntries exceeded");
      ++entries;
      ++count;
      if (lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2)) {
        const lua_Integer key = lua_tointeger(L, -2);
        if (key < 1 || key > static_cast<lua_Integer>(options.maxEntries))
          invalid("array keys must be dense positive integers");
        numeric = true;
        highest = std::max(highest, key);
      } else if (lua_type(L, -2) == LUA_TSTRING) {
        named = true;
      } else {
        invalid("table keys must be strings or dense positive integers");
      }
      if (numeric && named) invalid("mixed numeric and string table keys");
      lua_pop(L, 1);
    }
    if (numeric && highest != static_cast<lua_Integer>(count))
      invalid("sparse arrays are not supported");

    const bool array = numeric || (count == 0 && options.emptyTablesAsArrays);
    output.kind = array ? LuaValue::Kind::Array : LuaValue::Kind::Object;
    output.children.resize(count);
    if (array) {
      for (std::size_t i = 0; i < count; ++i) {
        lua_rawgeti(L, index, static_cast<lua_Integer>(i + 1));
        read(L, -1, output.children[i], depth + 1);
        lua_pop(L, 1);
      }
    } else {
      output.keys.reserve(count);
      std::size_t i = 0;
      lua_pushnil(L);
      while (lua_next(L, index) != 0) {
        std::size_t length = 0;
        const char* key = lua_tolstring(L, -2, &length); // proven string; no coercion
        countString(key, length);
        output.keys.emplace_back(key, length);
        read(L, -1, output.children[i++], depth + 1);
        lua_pop(L, 1);
      }
    }
    ancestors.pop_back();
  }

  static void snapshot(lua_State* L, void* opaque) {
    auto& reader = *static_cast<Reader*>(opaque);
    const int originalTop = lua_gettop(L);
    try {
      // lua_checkstack returns false on allocation failure; all later traversal
      // pushes fit this reservation. Existing entries (including to-close ones)
      // are never consumed or moved.
      if (!lua_checkstack(L, static_cast<int>(reader.options.maxDepth * 3 + 8)))
        invalid("cannot reserve Lua stack space");
      if (reader.indices) {
        for (std::size_t i = 0; i < reader.indices->size(); ++i) {
          const int requested = (*reader.indices)[i];
          if (requested == 0 || requested < -originalTop || requested > originalTop)
            invalid("invalid stack index (pseudo-indices are not supported)");
          const int absolute = requested < 0 ? originalTop + requested + 1 : requested;
          reader.read(L, absolute, reader.values[i], 0);
        }
      } else {
        lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
        const int globals = lua_gettop(L);
        if (lua_type(L, globals) != LUA_TTABLE) invalid("global environment is not a table");
        for (std::size_t i = 0; i < reader.names->size(); ++i) {
          lua_pushlstring(L, (*reader.names)[i].data(), (*reader.names)[i].size());
          lua_rawget(L, globals); // do not invoke _G.__index
          reader.read(L, -1, reader.values[i], 0);
          lua_pop(L, 1);
        }
      }
    } catch (...) {
      reader.failure = std::current_exception();
    }
    lua_settop(L, originalTop);
  }
};

} // namespace

std::vector<LuaValue> readLuaValues(
    LuaRuntime& runtime, const std::vector<int>& indices, const ValueReadOptions& options) {
  validateValueReadOptions(options);
  if (indices.size() > kMaxReadRoots) invalid("too many requested values");
  Reader reader(options, &indices, nullptr);
  runtime.runProtectedStateOperation(&Reader::snapshot, &reader);
  if (reader.failure) std::rethrow_exception(reader.failure);
  return std::move(reader.values);
}

std::vector<LuaValue> readLuaGlobals(
    LuaRuntime& runtime, const std::vector<std::string>& names, const ValueReadOptions& options) {
  validateValueReadOptions(options);
  if (names.size() > kMaxReadRoots) invalid("too many requested globals");
  Reader reader(options, nullptr, &names);
  for (const auto& name : names) reader.countString(name.data(), name.size());
  runtime.runProtectedStateOperation(&Reader::snapshot, &reader);
  if (reader.failure) std::rethrow_exception(reader.failure);
  return std::move(reader.values);
}

} // namespace rnlua
