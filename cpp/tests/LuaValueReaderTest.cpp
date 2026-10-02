#include "../LuaValueReader.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <thread>

extern "C" {
#include "../lua_src/lauxlib.h"
#include "../lua_src/lua.h"
}

using rnlua::LuaValue;
using rnlua::LuaRuntime;
using rnlua::ValueReadOptions;
using rnlua::readLuaGlobals;
using rnlua::readLuaValues;
static int assertions = 0;

void check(bool condition, const char* message) {
  ++assertions;
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void run(LuaRuntime& rt, const std::string& source) {
  const auto result = rt.executeString(source);
  if (result.code != 0) { std::cerr << result.error << '\n'; std::exit(1); }
}
const LuaValue& field(const LuaValue& value, const std::string& key) {
  for (std::size_t i = 0; i < value.keys.size(); ++i)
    if (value.keys[i] == key) return value.children[i];
  throw std::runtime_error("missing snapshot key: " + key);
}
template<typename Function>
void rejects(LuaRuntime& rt, Function function, const char* expected) {
  lua_State* L = rt.stateForAdvancedUse();
  const int top = L ? lua_gettop(L) : 0;
  bool caught = false;
  try { function(); } catch (const std::exception& error) {
    caught = std::string(error.what()).find(expected) != std::string::npos;
    if (!caught) std::cerr << "Unexpected error: " << error.what() << '\n';
  }
  check(caught, expected);
  check(!L || lua_gettop(L) == top, "failure must restore stack top");
}

int main() {
  LuaRuntime rt;
  run(rt, R"lua(
    a = {number = 3.25, flag = false, name = "hello", values = {1, true, "three"}}
    empty = {}
    alias = {4, 5}
    shared = {left = alias, right = alias}
    unicode = "ภาษาไทย 😀"
    special = {["__proto__"] = {polluted = true}, ["a\0b"] = "x\0y"}
    trap = setmetatable({value=42}, {
      __index=function() error("__index ran") end,
      __pairs=function() error("__pairs ran") end,
      __len=function() error("__len ran") end,
    })
  )lua");
  lua_State* L = rt.stateForAdvancedUse();
  lua_pushnumber(L, 123);
  lua_pushnil(L);
  lua_getglobal(L, "a");
  const int top = lua_gettop(L);
  auto values = readLuaValues(rt, {-1, 1, -2, -1});
  check(lua_gettop(L) == top, "success restores stack");
  check(lua_tonumber(L, 1) == 123 && lua_isnil(L, 2), "original stack contents unchanged");
  check(values[1].number == 123 && values[2].kind == LuaValue::Kind::Null, "relative/absolute/nil reads");
  check(field(values[0], "number").number == 3.25, "number snapshot");
  check(!field(values[0], "flag").boolean, "false is not nil");
  check(field(values[0], "values").kind == LuaValue::Kind::Array, "dense array conversion");
  check(field(values[0], "values").children[2].text == "three", "array order");
  auto globals = readLuaGlobals(rt, {"missing", "unicode", "special", "shared", "empty", "trap"});
  check(globals[0].kind == LuaValue::Kind::Null, "missing global is null");
  check(globals[1].text == "ภาษาไทย 😀", "valid UTF-8 preserved");
  check(field(globals[2], std::string("a\0b",3)).text == std::string("x\0y",3), "embedded zeros preserved");
  check(field(field(globals[2], "__proto__"), "polluted").boolean, "special keys preserved as data");
  check(field(globals[3], "left").children[0].number == 4, "shared acyclic aliases allowed");
  check(globals[4].kind == LuaValue::Kind::Object, "empty table defaults to object");
  check(field(globals[5], "value").number == 42, "metamethods ignored");
  ValueReadOptions arrayEmpty; arrayEmpty.emptyTablesAsArrays = true;
  check(readLuaGlobals(rt, {"empty"}, arrayEmpty)[0].kind == LuaValue::Kind::Array, "explicit empty-array policy");
  check(readLuaGlobals(rt, {}).empty() && readLuaValues(rt, {}).empty(), "empty bulk requests");
  rejects(rt, [&]{ readLuaValues(rt, {0}); }, "stack index");
  rejects(rt, [&]{ readLuaValues(rt, {100}); }, "stack index");
  rejects(rt, [&]{ readLuaValues(rt, {-100}); }, "stack index");
  rejects(rt, [&]{ readLuaValues(rt, {LUA_REGISTRYINDEX}); }, "stack index");
  rejects(rt, [&]{ readLuaValues(rt, std::vector<int>(257, 1)); }, "too many");

  // Must not invoke a global metatable either (raw registry environment lookup).
  run(rt, "setmetatable(_G, {__index=function() error('global trap ran') end})");
  check(readLuaGlobals(rt, {"missing_again"})[0].kind == LuaValue::Kind::Null, "raw globals bypass __index");
  run(rt, "setmetatable(_G, nil)");

  for (const auto* source : {"bad={};bad.self=bad", "bad={};local b={bad};bad.b=b"}) {
    run(rt, source);
    rejects(rt, [&]{ readLuaGlobals(rt, {"bad"}); }, "cyclic");
  }
  for (const auto* source : {"bad=function()end", "bad=coroutine.create(function()end)",
                             "bad={x=function()end}"}) {
    run(rt, source);
    rejects(rt, [&]{ readLuaGlobals(rt, {"bad"}); }, "not transferable");
  }
  lua_pushlightuserdata(L, L);
  rejects(rt, [&]{ readLuaValues(rt, {-1}); }, "not transferable");
  lua_pop(L, 1);
  lua_newuserdatauv(L, 1, 0);
  rejects(rt, [&]{ readLuaValues(rt, {-1}); }, "not transferable");
  lua_pop(L, 1);

  for (const auto* source : {"bad={1,2,x=3}", "bad={[false]=1}", "bad={[{}]=1}",
                             "bad={[0]=1}", "bad={[-1]=2}", "bad={[1.5]=2}",
                             "bad={[1000000000]=1}"}) {
    run(rt, source);
    rejects(rt, [&]{ readLuaGlobals(rt, {"bad"}); }, "keys");
  }
  run(rt, "bad={[1]=1,[3]=3}");
  rejects(rt, [&]{ readLuaGlobals(rt, {"bad"}); }, "sparse");
  for (const auto* source : {"bad=0/0", "bad=1/0", "bad=-1/0"}) {
    run(rt, source);
    rejects(rt, [&]{ readLuaGlobals(rt, {"bad"}); }, "finite");
  }
  run(rt, "bad=math.maxinteger");
  rejects(rt, [&]{ readLuaGlobals(rt, {"bad"}); }, "safe integer");
  for (const auto* source : {"bad=string.char(255)", "bad={[string.char(192,128)]=1}",
                             "bad=string.char(237,160,128)", "bad=string.char(244,144,128,128)"}) {
    run(rt, source);
    rejects(rt, [&]{ readLuaGlobals(rt, {"bad"}); }, "UTF-8");
  }
  run(rt, "small={1,2}; nested={x={y=1}}; text='12345'");
  ValueReadOptions limit;
  limit.maxEntries = 3;
  rejects(rt, [&]{ readLuaGlobals(rt, {"small", "small"}, limit); }, "maxEntries");
  limit = {}; limit.maxDepth = 1;
  rejects(rt, [&]{ readLuaGlobals(rt, {"nested"}, limit); }, "maxDepth");
  limit = {}; limit.maxStringBytes = 8;
  rejects(rt, [&]{ readLuaGlobals(rt, {"text"}, limit); }, "maxStringBytes");
  limit = {}; limit.maxDepth = 65;
  rejects(rt, [&]{ readLuaGlobals(rt, {"small"}, limit); }, "hard safety");

  // Failed allocation inside the Lua protection boundary must not leak or leave
  // hooks/state corrupt. Long random global name is absent from Lua string pool.
  rt.setMemoryLimitBytes(rt.memoryUsedBytes());
  rejects(rt, [&]{ readLuaGlobals(rt, {std::string(900000, 'Z')}); }, "memory");
  rt.setMemoryLimitBytes(32 * 1024 * 1024);
  check(readLuaGlobals(rt, {"small"})[0].children.size() == 2, "recovery after allocation failure");

  // Snapshots are detached, and state-local even during parallel independent reads.
  LuaRuntime other;
  run(other, "a=987");
  std::thread thread([&]{
    for (int i=0; i<1000; ++i) {
      if (readLuaGlobals(other,{"a"})[0].number != 987) std::abort();
    }
  });
  for (int i=0; i<1000; ++i) check(readLuaGlobals(rt,{"a"})[0].kind == LuaValue::Kind::Object, "isolated reads");
  thread.join();
  run(rt, "a.number=777");
  check(field(values[0], "number").number == 3.25, "snapshots do not alias Lua memory");

  // A Breakout-like object graph; library-only conversion cost, not UI/FPS proof.
  run(rt, R"lua(frame={revision=1,width=320,height=420,commands={}}
    for i=1,20 do frame.commands[i]={id='brick-'..i,type='rect',x=i,y=3,width=44,height=18,
      paint={style='fill',color='#FB7185'}} end)lua");
  const auto start=std::chrono::steady_clock::now();
  for(int i=0;i<2000;++i) readLuaGlobals(rt,{"frame"});
  const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  std::cout << "Native snapshot 20-primitives mean_ms=" << elapsed/2000 << '\n';
  rt.close();
  rejects(rt, [&]{ readLuaGlobals(rt, {"a"}); }, "destroyed");
  std::cout << "LuaValueReader assertions passed: " << assertions << '\n';
}
