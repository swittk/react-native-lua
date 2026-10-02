#include "../LuaValueReader.h"
#include "../LuaValueWriter.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "../lua_src/lua.h"
}

using rnlua::LuaRuntime;
using rnlua::LuaValue;
using rnlua::ValueLimits;
using rnlua::pushLuaValues;
using rnlua::readLuaGlobals;
using rnlua::readLuaValues;
using rnlua::setLuaGlobals;

static int assertions = 0;

void check(bool condition, const char* message) {
  ++assertions;
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void run(LuaRuntime& rt, const std::string& source) {
  const auto result = rt.executeString(source);
  if (result.code != 0) {
    std::cerr << result.error << '\n';
    std::exit(1);
  }
}

LuaValue number(double value) {
  LuaValue result;
  result.kind = LuaValue::Kind::Number;
  result.number = value;
  return result;
}

LuaValue boolean(bool value) {
  LuaValue result;
  result.kind = LuaValue::Kind::Boolean;
  result.boolean = value;
  return result;
}

LuaValue string(std::string value) {
  LuaValue result;
  result.kind = LuaValue::Kind::String;
  result.text = std::move(value);
  return result;
}

LuaValue array(std::vector<LuaValue> values) {
  LuaValue result;
  result.kind = LuaValue::Kind::Array;
  result.children = std::move(values);
  return result;
}

LuaValue object(
    std::vector<std::string> keys,
    std::vector<LuaValue> values) {
  LuaValue result;
  result.kind = LuaValue::Kind::Object;
  result.keys = std::move(keys);
  result.children = std::move(values);
  return result;
}

const LuaValue& field(const LuaValue& value, const std::string& key) {
  for (std::size_t i = 0; i < value.keys.size(); ++i) {
    if (value.keys[i] == key) return value.children[i];
  }
  throw std::runtime_error("missing field");
}

template <typename Function>
void rejectsRestoringStack(
    LuaRuntime& rt,
    Function function,
    const char* expected) {
  auto* L = rt.stateForAdvancedUse();
  const int top = L ? lua_gettop(L) : 0;
  bool caught = false;
  try {
    function();
  } catch (const std::exception& error) {
    caught = std::string(error.what()).find(expected) != std::string::npos;
    if (!caught) std::cerr << "Unexpected error: " << error.what() << '\n';
  }
  check(caught, expected);
  check(!L || lua_gettop(L) == top, "failed push/set restores stack");
}

int main() {
  LuaRuntime rt;
  auto* L = rt.stateForAdvancedUse();

  const auto nested = object(
      {"name", "enabled", "values", "__proto__", std::string("a\0b", 3)},
      {
          string("hello"),
          boolean(true),
          array({number(1), number(2), number(3)}),
          object({"safe"}, {boolean(true)}),
          string(std::string("x\0y", 3)),
      });

  lua_pushnumber(L, 77);
  const int originalTop = lua_gettop(L);
  pushLuaValues(rt, {number(4.5), nested, LuaValue{}});
  check(lua_gettop(L) == originalTop + 3, "pushValues leaves roots in input order");
  const auto pushed = readLuaValues(rt, {-3, -2, -1});
  check(pushed[0].number == 4.5, "number pushed");
  check(field(pushed[1], "name").text == "hello", "object pushed");
  check(field(pushed[1], "values").children[2].number == 3, "array pushed");
  check(field(field(pushed[1], "__proto__"), "safe").boolean, "special object key pushed raw");
  check(field(pushed[1], std::string("a\0b", 3)).text == std::string("x\0y", 3),
        "embedded zero key/value pushed");
  check(pushed[2].kind == LuaValue::Kind::Null, "root null pushes Lua nil");
  lua_settop(L, originalTop);

  run(rt, R"lua(
    existing = 10
    removed = 11
    raw_newindex_calls = 0
    setmetatable(_G, {
      __newindex = function(t, k, v)
        raw_newindex_calls = raw_newindex_calls + 1
        rawset(t, k, v)
      end
    })
  )lua");
  const int beforeSetTop = lua_gettop(L);
  setLuaGlobals(
      rt,
      {"existing", "fresh", "removed", std::string("x\0y", 3)},
      {number(20), nested, LuaValue{}, string("nul-name")});
  check(lua_gettop(L) == beforeSetTop, "setGlobals leaves stack untouched");
  auto globals = readLuaGlobals(
      rt, {"existing", "fresh", "removed", "raw_newindex_calls", std::string("x\0y", 3)});
  check(globals[0].number == 20, "existing global replaced");
  check(field(globals[1], "name").text == "hello", "new object global set");
  check(globals[2].kind == LuaValue::Kind::Null, "null clears global");
  check(globals[3].number == 0, "raw global set bypasses __newindex");
  check(globals[4].text == "nul-name", "embedded-zero global name set");

  // Materialization failure occurs before any global mutation.
  run(rt, "first_guard=111; second_guard=222");
  LuaValue huge = string(std::string(900000, 'Z'));
  const auto memoryBefore = rt.memoryUsedBytes();
  rt.setMemoryLimitBytes(memoryBefore);
  rejectsRestoringStack(
      rt,
      [&] {
        setLuaGlobals(
            rt,
            {"first_guard", "second_guard"},
            {number(999), huge});
      },
      "memory");
  rt.setMemoryLimitBytes(32 * 1024 * 1024);
  globals = readLuaGlobals(rt, {"first_guard", "second_guard"});
  check(globals[0].number == 111 && globals[1].number == 222,
        "failed staged set changes no globals");

  ValueLimits depth;
  depth.maxDepth = 0;
  rejectsRestoringStack(rt, [&] { pushLuaValues(rt, {nested}, depth); }, "maxDepth");
  ValueLimits hard;
  hard.maxDepth = 65;
  rejectsRestoringStack(rt, [&] { pushLuaValues(rt, {number(1)}, hard); }, "hard safety");

  // Independent Lua runtimes can receive values concurrently.
  LuaRuntime other;
  std::thread thread([&] {
    for (int i = 0; i < 500; ++i) {
      setLuaGlobals(other, {"counter"}, {number(i)});
    }
  });
  for (int i = 0; i < 500; ++i) {
    setLuaGlobals(rt, {"main_counter"}, {number(i)});
  }
  thread.join();
  check(readLuaGlobals(other, {"counter"})[0].number == 499, "second runtime isolated");
  check(readLuaGlobals(rt, {"main_counter"})[0].number == 499, "first runtime isolated");

  rt.close();
  rejectsRestoringStack(rt, [&] { pushLuaValues(rt, {number(1)}); }, "destroyed");
  std::cout << "LuaValueWriter assertions passed: " << assertions << '\n';
}
