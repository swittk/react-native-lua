#include "LuaValueWriter.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

extern "C" {
#include "lua_src/lauxlib.h"
#include "lua_src/lua.h"
}

namespace rnlua {
namespace {

[[noreturn]] void invalid(const char* message) {
  throw std::runtime_error(std::string("Lua bulk value push/set: ") + message);
}

struct ValidationBudget {
  const ValueLimits& limits;
  std::size_t entries = 0;
  std::size_t stringBytes = 0;

  void countString(const std::string& value) {
    if (!isValidUtf8(value.data(), value.size())) invalid("strings and keys must be valid UTF-8");
    if (value.size() > limits.maxStringBytes - stringBytes) invalid("maxStringBytes exceeded");
    stringBytes += value.size();
  }

  void visit(const LuaValue& value, std::size_t depth) {
    switch (value.kind) {
      case LuaValue::Kind::Null:
      case LuaValue::Kind::Boolean:
        return;
      case LuaValue::Kind::Number:
        if (!std::isfinite(value.number)) invalid("numbers must be finite");
        return;
      case LuaValue::Kind::String:
        countString(value.text);
        return;
      case LuaValue::Kind::Array:
      case LuaValue::Kind::Object:
        break;
    }
    if (depth >= limits.maxDepth) invalid("maxDepth exceeded");
    if (value.kind == LuaValue::Kind::Object && value.keys.size() != value.children.size())
      invalid("invalid internal object shape");
    if (value.children.size() > limits.maxEntries - entries) invalid("maxEntries exceeded");
    entries += value.children.size();
    if (value.kind == LuaValue::Kind::Object)
      for (const auto& key : value.keys) countString(key);
    for (const auto& child : value.children) visit(child, depth + 1);
  }
};

void validateValues(
    const std::vector<LuaValue>& values,
    const std::vector<std::string>* names,
    const ValueLimits& limits) {
  ValidationBudget budget{limits};
  if (names) {
    if (names->size() != values.size()) invalid("global names/value count mismatch");
    for (std::size_t i = 0; i < names->size(); ++i) {
      for (std::size_t j = 0; j < i; ++j) {
        if ((*names)[i] == (*names)[j]) invalid("duplicate global names are not supported");
      }
      budget.countString((*names)[i]);
    }
  }
  for (const auto& value : values) budget.visit(value, 0);
}

struct Writer {
  const std::vector<LuaValue>& values;
  const std::vector<std::string>* names;
  const ValueLimits& limits;
  std::exception_ptr failure;

  Writer(
      const std::vector<LuaValue>& input,
      const std::vector<std::string>* globalNames,
      const ValueLimits& bounds)
      : values(input), names(globalNames), limits(bounds) {}

  void pushOne(lua_State* L, const LuaValue& value, std::size_t depth) {
    switch (value.kind) {
      case LuaValue::Kind::Null:
        lua_pushnil(L);
        return;
      case LuaValue::Kind::Boolean:
        lua_pushboolean(L, value.boolean ? 1 : 0);
        return;
      case LuaValue::Kind::Number:
        if (std::trunc(value.number) == value.number &&
            std::fabs(value.number) <= 9007199254740991.0 &&
            !(value.number == 0 && std::signbit(value.number))) {
          lua_pushinteger(L, static_cast<lua_Integer>(value.number));
        } else {
          lua_pushnumber(L, value.number);
        }
        return;
      case LuaValue::Kind::String:
        lua_pushlstring(L, value.text.data(), value.text.size());
        return;
      case LuaValue::Kind::Array:
      case LuaValue::Kind::Object:
        break;
    }

    if (depth >= limits.maxDepth) invalid("maxDepth exceeded");
    if (value.kind == LuaValue::Kind::Array) {
      lua_createtable(L, static_cast<int>(value.children.size()), 0);
      const int table = lua_gettop(L);
      for (std::size_t i = 0; i < value.children.size(); ++i) {
        pushOne(L, value.children[i], depth + 1);
        lua_rawseti(L, table, static_cast<lua_Integer>(i + 1));
      }
      return;
    }

    if (value.keys.size() != value.children.size()) {
      invalid("invalid internal object shape");
    }
    lua_createtable(L, 0, static_cast<int>(value.children.size()));
    const int table = lua_gettop(L);
    for (std::size_t i = 0; i < value.children.size(); ++i) {
      lua_pushlstring(L, value.keys[i].data(), value.keys[i].size());
      pushOne(L, value.children[i], depth + 1);
      lua_rawset(L, table);
    }
  }

  static int rawSet(lua_State* L) {
    // args: globals table, already-created key string, already-created value
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_rawset(L, 1);
    return 0;
  }

  static void pushValuesOperation(lua_State* L, void* opaque) {
    auto& writer = *static_cast<Writer*>(opaque);
    const int originalTop = lua_gettop(L);
    try {
      const std::size_t reserve =
          writer.values.size() + writer.limits.maxDepth * 3 + 16;
      if (reserve > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
          !lua_checkstack(L, static_cast<int>(reserve))) {
        invalid("cannot reserve Lua stack space");
      }
      for (const auto& value : writer.values) writer.pushOne(L, value, 0);
    } catch (...) {
      writer.failure = std::current_exception();
      lua_settop(L, originalTop);
    }
  }

  static void setGlobalsOperation(lua_State* L, void* opaque) {
    auto& writer = *static_cast<Writer*>(opaque);
    const int originalTop = lua_gettop(L);
    try {
      if (writer.names == nullptr || writer.names->size() != writer.values.size()) {
        invalid("global names/value count mismatch");
      }
      const std::size_t count = writer.values.size();
      const std::size_t reserve =
          count * 3 + writer.limits.maxDepth * 3 + 32;
      if (reserve > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
          !lua_checkstack(L, static_cast<int>(reserve))) {
        invalid("cannot reserve Lua stack space");
      }

      lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
      const int globals = lua_gettop(L);
      if (lua_type(L, globals) != LUA_TTABLE) invalid("global environment is not a table");

      // Stage names, old values, and all non-nil new values before mutating _G.
      lua_createtable(L, static_cast<int>(count), 0);
      const int stagedNames = lua_gettop(L);
      lua_createtable(L, static_cast<int>(count), 0);
      const int oldValues = lua_gettop(L);
      lua_createtable(L, static_cast<int>(count), 0);
      const int newValues = lua_gettop(L);

      for (std::size_t i = 0; i < count; ++i) {
        const lua_Integer slot = static_cast<lua_Integer>(i + 1);
        const auto& name = (*writer.names)[i];

        lua_pushlstring(L, name.data(), name.size());
        lua_rawseti(L, stagedNames, slot);

        lua_rawgeti(L, stagedNames, slot);
        lua_rawget(L, globals);
        lua_rawseti(L, oldValues, slot);

        if (writer.values[i].kind != LuaValue::Kind::Null) {
          writer.pushOne(L, writer.values[i], 0);
          lua_rawseti(L, newValues, slot);
        }
      }

      // Commit every non-nil value first. These are the only assignments that
      // can allocate in Lua 5.4: luaH_newkey returns immediately for nil values.
      // If one fails, rollback touches only keys that were never deleted:
      // existing keys are overwritten in-place and newly inserted keys are
      // restored to nil. Neither operation requires a table allocation.
      for (std::size_t i = 0; i < count; ++i) {
        if (writer.values[i].kind == LuaValue::Kind::Null) continue;
        const lua_Integer slot = static_cast<lua_Integer>(i + 1);
        lua_pushcfunction(L, &Writer::rawSet);
        lua_pushvalue(L, globals);
        lua_rawgeti(L, stagedNames, slot);
        lua_rawgeti(L, newValues, slot);

        const int status = lua_pcall(L, 3, 0, 0);
        if (status != LUA_OK) {
          std::string message = "global assignment failed";
          if (lua_type(L, -1) == LUA_TSTRING) {
            std::size_t length = 0;
            if (const char* text = lua_tolstring(L, -1, &length)) {
              message.assign(text, length);
            }
          }
          lua_pop(L, 1);

          for (std::size_t j = 0; j <= i; ++j) {
            if (writer.values[j].kind == LuaValue::Kind::Null) continue;
            const lua_Integer restoreSlot = static_cast<lua_Integer>(j + 1);
            lua_rawgeti(L, stagedNames, restoreSlot);
            lua_rawgeti(L, oldValues, restoreSlot);
            lua_rawset(L, globals);
          }
          throw std::runtime_error(
              std::string("Lua bulk value push/set: global assignment commit failed: ") +
              message);
        }
      }

      // Deletions cannot allocate (Lua 5.4's luaH_newkey returns before
      // insertion for nil values), so perform them only after every potentially
      // allocating assignment has succeeded.
      for (std::size_t i = 0; i < count; ++i) {
        if (writer.values[i].kind != LuaValue::Kind::Null) continue;
        const lua_Integer slot = static_cast<lua_Integer>(i + 1);
        lua_rawgeti(L, stagedNames, slot);
        lua_pushnil(L);
        lua_rawset(L, globals);
      }
      lua_settop(L, originalTop);
    } catch (...) {
      writer.failure = std::current_exception();
      lua_settop(L, originalTop);
    }
  }
};

} // namespace

void pushLuaValues(
    LuaRuntime& runtime,
    const std::vector<LuaValue>& values,
    const ValueLimits& limits) {
  validateValueLimits(limits);
  if (values.size() > kMaxValueRoots) invalid("too many values");
  validateValues(values, nullptr, limits);
  Writer writer(values, nullptr, limits);
  runtime.runProtectedStateOperation(&Writer::pushValuesOperation, &writer);
  if (writer.failure) std::rethrow_exception(writer.failure);
}

void setLuaGlobals(
    LuaRuntime& runtime,
    const std::vector<std::string>& names,
    const std::vector<LuaValue>& values,
    const ValueLimits& limits) {
  validateValueLimits(limits);
  if (names.size() > kMaxValueRoots || values.size() > kMaxValueRoots)
    invalid("too many globals");
  validateValues(values, &names, limits);
  Writer writer(values, &names, limits);
  runtime.runProtectedStateOperation(&Writer::setGlobalsOperation, &writer);
  if (writer.failure) std::rethrow_exception(writer.failure);
}

} // namespace rnlua
