#include "react-native-lua.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>

extern "C" {
#include "lua_src/lauxlib.h"
#include "lua_src/lua.h"
}

namespace SKRNNativeLua {
namespace {

namespace jsi = facebook::jsi;

template <typename Function>
jsi::Value makeHostFunction(
    jsi::Runtime& runtime,
    const jsi::PropNameID& name,
    unsigned int argumentCount,
    Function&& function) {
  return jsi::Function::createFromHostFunction(
      runtime,
      name,
      argumentCount,
      std::forward<Function>(function));
}

double numberOption(
    jsi::Runtime& runtime,
    const jsi::Object& options,
    const char* name,
    double fallback) {
  const jsi::Value value = options.getProperty(runtime, name);
  if (!value.isNumber()) {
    return fallback;
  }
  const double number = value.asNumber();
  return std::isfinite(number) ? number : fallback;
}

bool boolOption(
    jsi::Runtime& runtime,
    const jsi::Object& options,
    const char* name,
    bool fallback) {
  const jsi::Value value = options.getProperty(runtime, name);
  return value.isBool() ? value.getBool() : fallback;
}

rnlua::InterpreterOptions interpreterOptions(
    jsi::Runtime& runtime,
    const jsi::Value* arguments,
    std::size_t count) {
  rnlua::InterpreterOptions result;
  if (count == 0 || !arguments[0].isObject()) {
    return result;
  }

  const jsi::Object options = arguments[0].asObject(runtime);
  result.executionLimitMs = static_cast<std::int64_t>(std::clamp(
      numberOption(runtime, options, "executionLimitMs", result.executionLimitMs),
      1.0, 300'000.0));
  result.memoryLimitBytes = static_cast<std::size_t>(std::clamp(
      numberOption(runtime, options, "memoryLimitBytes", result.memoryLimitBytes),
      0.0, 256.0 * 1024.0 * 1024.0));
  result.maxOutputBytes = static_cast<std::size_t>(std::clamp(
      numberOption(runtime, options, "maxOutputBytes", result.maxOutputBytes),
      0.0, 1024.0 * 1024.0));
  result.maxOutputLines = static_cast<std::size_t>(std::clamp(
      numberOption(runtime, options, "maxOutputLines", result.maxOutputLines),
      0.0, 10'000.0));
  result.allowFileSystem =
      boolOption(runtime, options, "allowFileSystem", result.allowFileSystem);
  result.allowBytecode =
      boolOption(runtime, options, "allowBytecode", result.allowBytecode);
  result.allowNetwork =
      boolOption(runtime, options, "allowNetwork", result.allowNetwork);
  // JS callers may opt into file loading or bytecode. Networking is bundled
  // by default but can be removed per interpreter; process, filesystem module
  // loading, and debug APIs are never ambient.
  return result;
}

jsi::Object executionResultObject(
    jsi::Runtime& runtime,
    const rnlua::ExecutionResult& result) {
  jsi::Object value(runtime);
  value.setProperty(runtime, "code", result.code);
  value.setProperty(runtime, "luaStatus", result.luaStatus);
  value.setProperty(runtime, "reason", result.reason);
  value.setProperty(runtime, "error", result.error);
  value.setProperty(runtime, "durationMs", result.durationMs);
  value.setProperty(
      runtime, "memoryUsedBytes", static_cast<double>(result.memoryUsedBytes));
  value.setProperty(
      runtime, "peakMemoryBytes", static_cast<double>(result.peakMemoryBytes));
  value.setProperty(runtime, "outputTruncated", result.outputTruncated);
  return value;
}

const std::vector<std::string> kInterpreterKeys = {
    "dostringasync", "dofileasync", "dostring", "dofile", "printCount",
    "executeStringResult", "executeFileResult", "startStringAsync",
    "startFileAsync", "takeAsyncResult", "executing",
    "getPrint", "getLatestError", "pop", "pushboolean", "pushglobaltable",
    "pushinteger", "pushnil", "pushnumber", "pushstring", "pushthread",
    "pushvalue", "rawequal", "rawget", "rawgeti", "rawlen", "rawset",
    "rawseti", "remove", "insert", "replace", "resetthread", "resume",
    "rotate", "setfield", "setglobal", "seti", "setiuservalue",
    "setmetatable", "settable", "settop", "gettop", "status",
    "stringtonumber", "gettable", "getglobal", "var_asnumber", "toboolean",
    "toclose", "tointeger", "tonumber", "tostring", "topointer",
    "tothread", "type", "typename", "yield", "valid", "executionLimit",
    "setExecutionLimit",
    "memoryLimitBytes", "setMemoryLimitBytes", "memoryUsedBytes",
    "peakMemoryBytes", "maxOutputBytes", "setMaxOutputBytes",
    "maxOutputLines", "setMaxOutputLines", "outputTruncated", "cancel",
    "destroy"};

} // namespace

SKRNLuaInterpreter::SKRNLuaInterpreter(
    std::shared_ptr<facebook::react::CallInvoker>,
    rnlua::InterpreterOptions options)
    : lua_(new rnlua::LuaRuntime(options)),
      state_(lua_->stateForAdvancedUse()) {}

SKRNLuaInterpreter::~SKRNLuaInterpreter() {
  shutdown();
}

int SKRNLuaInterpreter::doString(const std::string& source) {
  if (lua_ == nullptr || !lua_->isOpen()) {
    return rnlua::kDestroyed;
  }
  return lua_->executeString(source).code;
}

int SKRNLuaInterpreter::doFile(const std::string& path) {
  if (lua_ == nullptr || !lua_->isOpen()) {
    return rnlua::kDestroyed;
  }
  return lua_->executeFile(path).code;
}

std::string SKRNLuaInterpreter::getLatestError() const {
  return lua_ == nullptr ? "Interpreter is destroyed" : lua_->latestError();
}

std::uint64_t SKRNLuaInterpreter::startAsync(
    const std::string& source,
    bool isFile) {
  if (destroyed_.load() || lua_ == nullptr || !lua_->isOpen()) {
    throw std::runtime_error("Lua interpreter is destroyed");
  }
  bool expected = false;
  if (!executing_.compare_exchange_strong(expected, true)) {
    throw std::runtime_error("Lua interpreter is already executing");
  }

  joinCompletedWorker();
  lua_->resetCancellation();
  const std::uint64_t taskId = nextTaskId_.fetch_add(1);
  try {
    worker_ = std::thread([this, taskId, source, isFile] {
      rnlua::ExecutionResult result;
      try {
        result = isFile ? lua_->executeFile(source) : lua_->executeString(source);
      } catch (const std::exception& error) {
        result.code = LUA_ERRRUN;
        result.luaStatus = LUA_ERRRUN;
        result.reason = "runtime";
        result.error = error.what();
      } catch (...) {
        result.code = LUA_ERRRUN;
        result.luaStatus = LUA_ERRRUN;
        result.reason = "runtime";
        result.error = "Unknown native execution failure";
      }
      {
        std::lock_guard<std::mutex> lock(resultMutex_);
        asyncResults_.push_back({taskId, std::move(result)});
        while (asyncResults_.size() > 16) {
          asyncResults_.pop_front();
        }
        // Publish completion while holding the same lock used by result
        // polling, so JS cannot observe a result while `executing` is stale.
        executing_.store(false);
      }
    });
  } catch (...) {
    executing_.store(false);
    throw;
  }
  return taskId;
}

bool SKRNLuaInterpreter::takeAsyncResult(
    std::uint64_t taskId,
    rnlua::ExecutionResult& result) {
  std::lock_guard<std::mutex> lock(resultMutex_);
  auto item = std::find_if(
      asyncResults_.begin(),
      asyncResults_.end(),
      [taskId](const AsyncResult& candidate) {
        return candidate.taskId == taskId;
      });
  if (item == asyncResults_.end()) {
    return false;
  }
  result = std::move(item->result);
  asyncResults_.erase(item);
  return true;
}

void SKRNLuaInterpreter::joinCompletedWorker() {
  if (worker_.joinable()) {
    worker_.join();
  }
}

void SKRNLuaInterpreter::shutdown() noexcept {
  if (destroyed_.exchange(true)) {
    return;
  }
  if (lua_ != nullptr) {
    lua_->requestCancellation();
  }
  joinCompletedWorker();
  if (lua_ != nullptr) {
    lua_->close();
  }
  state_ = nullptr;
  executing_.store(false);
}

void SKRNLuaInterpreter::withState(
    const std::function<void(lua_State*)>& callback) {
  if (lua_ == nullptr || destroyed_.load()) {
    throw std::runtime_error("Interpreter is destroyed");
  }
  if (executing_.load()) {
    throw std::runtime_error("Interpreter is executing");
  }
  lua_->withState(callback);
}

jsi::Value SKRNLuaInterpreter::get(
    jsi::Runtime& runtime,
    const jsi::PropNameID& name) {
  const std::string method = name.utf8(runtime);
  const auto self = shared_from_this();
  const auto state = [self, &runtime]() -> lua_State* {
    if (self->lua_ == nullptr || !self->lua_->isOpen() || self->state_ == nullptr) {
      throw jsi::JSError(runtime, "Lua interpreter is destroyed");
    }
    if (self->executing_.load()) {
      throw jsi::JSError(runtime, "Lua interpreter is executing asynchronously");
    }
    return self->state_;
  };

  if (method == "valid") {
    return jsi::Value(!destroyed_.load() && lua_ != nullptr && lua_->isOpen());
  }
  if (method == "executing") {
    return jsi::Value(executing_.load());
  }
  if (method == "printCount") {
    return jsi::Value(static_cast<double>(lua_ == nullptr ? 0 : lua_->outputCount()));
  }
  if (method == "executionLimit") {
    return jsi::Value(static_cast<double>(lua_->executionLimitMs()));
  }
  if (method == "memoryLimitBytes") {
    return jsi::Value(static_cast<double>(lua_->memoryLimitBytes()));
  }
  if (method == "memoryUsedBytes") {
    return jsi::Value(static_cast<double>(lua_->memoryUsedBytes()));
  }
  if (method == "peakMemoryBytes") {
    return jsi::Value(static_cast<double>(lua_->peakMemoryBytes()));
  }
  if (method == "maxOutputBytes") {
    return jsi::Value(static_cast<double>(lua_->maxOutputBytes()));
  }
  if (method == "maxOutputLines") {
    return jsi::Value(static_cast<double>(lua_->maxOutputLines()));
  }
  if (method == "outputTruncated") {
    return jsi::Value(lua_->outputWasTruncated());
  }

  if (method == "getPrint") {
    return makeHostFunction(runtime, name, 1, [self](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* arguments,
        std::size_t count) -> jsi::Value {
      const std::size_t requested = count > 0 && arguments[0].isNumber()
          ? static_cast<std::size_t>(std::max(0.0, arguments[0].asNumber()))
          : 0;
      return jsi::String::createFromUtf8(runtime, self->lua_->takeOutput(requested));
    });
  }
  if (method == "getLatestError") {
    return makeHostFunction(runtime, name, 0, [self](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value*,
        std::size_t) -> jsi::Value {
      if (self->executing_.load()) {
        throw jsi::JSError(runtime, "Lua interpreter is executing asynchronously");
      }
      return jsi::String::createFromUtf8(runtime, self->getLatestError());
    });
  }
  if (method == "dostring" || method == "dofile" ||
      method == "executeStringResult" || method == "executeFileResult") {
    const bool isFile = method == "dofile" || method == "executeFileResult";
    const bool structured =
        method == "executeStringResult" || method == "executeFileResult";
    return makeHostFunction(runtime, name, 1, [self, isFile, structured](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* arguments,
        std::size_t count) -> jsi::Value {
      if (count < 1 || !arguments[0].isString()) {
        throw jsi::JSError(runtime, "Expected a Lua source string");
      }
      if (self->executing_.exchange(true)) {
        throw jsi::JSError(runtime, "Lua interpreter is already executing");
      }
      try {
        self->joinCompletedWorker();
        self->lua_->resetCancellation();
        const std::string value = arguments[0].asString(runtime).utf8(runtime);
        const rnlua::ExecutionResult result = isFile
            ? self->lua_->executeFile(value)
            : self->lua_->executeString(value);
        self->executing_.store(false);
        return structured
            ? jsi::Value(executionResultObject(runtime, result))
            : jsi::Value(result.code);
      } catch (...) {
        self->executing_.store(false);
        throw;
      }
    });
  }
  if (method == "dostringasync" || method == "dofileasync" ||
      method == "startStringAsync" || method == "startFileAsync") {
    const bool isFile = method == "dofileasync" || method == "startFileAsync";
    const bool legacyName = method == "dostringasync" || method == "dofileasync";
    return makeHostFunction(runtime, name, legacyName ? 2 : 1, [self, isFile, legacyName](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* arguments,
        std::size_t count) -> jsi::Value {
      if (count < 1 || !arguments[0].isString()) {
        throw jsi::JSError(runtime, "Expected a Lua source string");
      }
      const std::string value = arguments[0].asString(runtime).utf8(runtime);
      try {
        const std::uint64_t taskId = self->startAsync(value, isFile);
        // Public legacy callbacks are implemented by the TypeScript wrapper so
        // no jsi::Function or runtime pointer crosses the worker boundary.
        return legacyName
            ? jsi::Value::undefined()
            : jsi::Value(static_cast<double>(taskId));
      } catch (const std::exception& error) {
        throw jsi::JSError(runtime, error.what());
      }
    });
  }
  if (method == "takeAsyncResult") {
    return makeHostFunction(runtime, name, 1, [self](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* arguments,
        std::size_t count) -> jsi::Value {
      if (count < 1 || !arguments[0].isNumber()) {
        throw jsi::JSError(runtime, "Expected an async task id");
      }
      rnlua::ExecutionResult result;
      if (!self->takeAsyncResult(
              static_cast<std::uint64_t>(arguments[0].asNumber()), result)) {
        return jsi::Value::null();
      }
      return executionResultObject(runtime, result);
    });
  }
  if (method == "setExecutionLimit" || method == "setMemoryLimitBytes" ||
      method == "setMaxOutputBytes" || method == "setMaxOutputLines") {
    return makeHostFunction(runtime, name, 1, [self, method](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* arguments,
        std::size_t count) -> jsi::Value {
      if (count < 1 || !arguments[0].isNumber()) {
        throw jsi::JSError(runtime, "Expected a numeric limit");
      }
      if (self->executing_.load()) {
        throw jsi::JSError(runtime, "Cannot change limits while Lua is executing");
      }
      double value = arguments[0].asNumber();
      if (!std::isfinite(value)) {
        throw jsi::JSError(runtime, "Limit must be finite");
      }
      if (method == "setExecutionLimit") {
        value = std::clamp(value, 1.0, 300'000.0);
        self->lua_->setExecutionLimitMs(static_cast<std::int64_t>(value));
      } else if (method == "setMemoryLimitBytes") {
        value = std::clamp(value, 0.0, 256.0 * 1024.0 * 1024.0);
        self->lua_->setMemoryLimitBytes(static_cast<std::size_t>(value));
      } else if (method == "setMaxOutputBytes") {
        value = std::clamp(value, 0.0, 1024.0 * 1024.0);
        self->lua_->setMaxOutputBytes(static_cast<std::size_t>(value));
      } else {
        value = std::clamp(value, 0.0, 10'000.0);
        self->lua_->setMaxOutputLines(static_cast<std::size_t>(value));
      }
      return jsi::Value::undefined();
    });
  }
  if (method == "cancel") {
    return makeHostFunction(runtime, name, 0, [self](
        jsi::Runtime&, const jsi::Value&, const jsi::Value*, std::size_t) {
      self->lua_->requestCancellation();
      return jsi::Value::undefined();
    });
  }
  if (method == "destroy") {
    return makeHostFunction(runtime, name, 0, [self](
        jsi::Runtime&, const jsi::Value&, const jsi::Value*, std::size_t) {
      self->shutdown();
      return jsi::Value::undefined();
    });
  }

  if (method == "pop") {
    return makeHostFunction(runtime, name, 1, [state](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a, std::size_t n) {
      if (n < 1) throw jsi::JSError(runtime, "Expected count");
      lua_pop(state(), static_cast<int>(a[0].asNumber()));
      return jsi::Value::undefined();
    });
  }
  if (method == "pushboolean" || method == "pushinteger" || method == "pushnumber") {
    return makeHostFunction(runtime, name, 1, [state, method](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a, std::size_t n) {
      if (n < 1 || !a[0].isNumber()) throw jsi::JSError(runtime, "Expected number");
      if (method == "pushboolean") lua_pushboolean(state(), a[0].asNumber() != 0);
      else if (method == "pushinteger") lua_pushinteger(state(), static_cast<lua_Integer>(a[0].asNumber()));
      else lua_pushnumber(state(), a[0].asNumber());
      return jsi::Value::undefined();
    });
  }
  if (method == "pushnil" || method == "pushglobaltable") {
    return makeHostFunction(runtime, name, 0, [state, method](jsi::Runtime&, const jsi::Value&, const jsi::Value*, std::size_t) {
      if (method == "pushnil") lua_pushnil(state()); else lua_pushglobaltable(state());
      return jsi::Value::undefined();
    });
  }
  if (method == "pushstring") {
    return makeHostFunction(runtime, name, 1, [state](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a, std::size_t n) {
      if (n < 1 || !a[0].isString()) throw jsi::JSError(runtime, "Expected string");
      const std::string value = a[0].asString(runtime).utf8(runtime);
      lua_pushlstring(state(), value.data(), value.size());
      return jsi::Value::undefined();
    });
  }
  if (method == "pushthread") {
    return makeHostFunction(runtime, name, 0, [state](jsi::Runtime&, const jsi::Value&, const jsi::Value*, std::size_t) {
      return jsi::Value(lua_pushthread(state()));
    });
  }
  if (method == "pushvalue" || method == "rawget" || method == "rawlen" ||
      method == "rawset" || method == "remove" || method == "insert" ||
      method == "replace" || method == "setmetatable" || method == "settable" ||
      method == "settop" || method == "gettable" || method == "toboolean" ||
      method == "toclose" || method == "tointeger" || method == "tonumber" ||
      method == "tostring" || method == "topointer" ||
      method == "tothread" || method == "type") {
    return makeHostFunction(runtime, name, 1, [state, method](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a, std::size_t n) -> jsi::Value {
      const int index = n > 0 && a[0].isNumber() ? static_cast<int>(a[0].asNumber()) : -1;
      lua_State* L = state();
      if (method == "pushvalue") { lua_pushvalue(L, index); return jsi::Value::undefined(); }
      if (method == "rawget") return jsi::Value(lua_rawget(L, index));
      if (method == "rawlen") return jsi::Value(static_cast<double>(lua_rawlen(L, index)));
      if (method == "rawset") { lua_rawset(L, index); return jsi::Value::undefined(); }
      if (method == "remove") { lua_remove(L, index); return jsi::Value::undefined(); }
      if (method == "insert") { lua_insert(L, index); return jsi::Value::undefined(); }
      if (method == "replace") { lua_replace(L, index); return jsi::Value::undefined(); }
      if (method == "setmetatable") return jsi::Value(lua_setmetatable(L, index));
      if (method == "settable") { lua_settable(L, index); return jsi::Value::undefined(); }
      if (method == "settop") { lua_settop(L, index); return jsi::Value::undefined(); }
      if (method == "gettable") return jsi::Value(lua_gettable(L, index));
      if (method == "toboolean") return jsi::Value(lua_toboolean(L, index));
      if (method == "toclose") { lua_toclose(L, index); return jsi::Value::undefined(); }
      if (method == "tointeger") return jsi::Value(static_cast<double>(lua_tointeger(L, index)));
      if (method == "tonumber") return jsi::Value(lua_tonumber(L, index));
      if (method == "tostring") {
        std::size_t size = 0;
        const char* value = lua_tolstring(L, index, &size);
        return value == nullptr ? jsi::Value::null() : jsi::String::createFromUtf8(runtime, std::string(value, size));
      }
      if (method == "topointer") return jsi::Value(static_cast<double>(reinterpret_cast<std::uintptr_t>(lua_topointer(L, index))));
      if (method == "tothread") return jsi::Value(static_cast<double>(reinterpret_cast<std::uintptr_t>(lua_tothread(L, index))));
      return jsi::Value(lua_type(L, index));
    });
  }
  if (method == "rawequal" || method == "rawgeti" || method == "rawseti" ||
      method == "rotate" || method == "seti" || method == "setiuservalue") {
    return makeHostFunction(runtime, name, 2, [state, method](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a, std::size_t n) -> jsi::Value {
      if (n < 2 || !a[0].isNumber() || !a[1].isNumber()) throw jsi::JSError(runtime, "Expected two numbers");
      const int first = static_cast<int>(a[0].asNumber());
      const lua_Integer second = static_cast<lua_Integer>(a[1].asNumber());
      lua_State* L = state();
      if (method == "rawequal") return jsi::Value(lua_rawequal(L, first, static_cast<int>(second)));
      if (method == "rawgeti") return jsi::Value(lua_rawgeti(L, first, second));
      if (method == "rawseti") { lua_rawseti(L, first, second); return jsi::Value::undefined(); }
      if (method == "rotate") { lua_rotate(L, first, static_cast<int>(second)); return jsi::Value::undefined(); }
      if (method == "seti") { lua_seti(L, first, second); return jsi::Value::undefined(); }
      return jsi::Value(lua_setiuservalue(L, first, static_cast<int>(second)));
    });
  }
  if (method == "setfield") {
    return makeHostFunction(runtime, name, 2, [state](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a, std::size_t n) {
      if (n < 2 || !a[0].isNumber() || !a[1].isString()) throw jsi::JSError(runtime, "Expected index and field");
      const std::string key = a[1].asString(runtime).utf8(runtime);
      lua_setfield(state(), static_cast<int>(a[0].asNumber()), key.c_str());
      return jsi::Value::undefined();
    });
  }
  if (method == "setglobal" || method == "getglobal" || method == "var_asnumber" || method == "stringtonumber") {
    return makeHostFunction(runtime, name, 1, [state, method](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a, std::size_t n) -> jsi::Value {
      if (n < 1 || !a[0].isString()) throw jsi::JSError(runtime, "Expected string");
      const std::string value = a[0].asString(runtime).utf8(runtime);
      lua_State* L = state();
      if (method == "setglobal") { lua_setglobal(L, value.c_str()); return jsi::Value::undefined(); }
      if (method == "getglobal") { lua_getglobal(L, value.c_str()); return jsi::Value::undefined(); }
      if (method == "stringtonumber") return jsi::Value(static_cast<double>(lua_stringtonumber(L, value.c_str())));
      lua_getglobal(L, value.c_str());
      const lua_Number result = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0;
      lua_pop(L, 1);
      return jsi::Value(result);
    });
  }
  if (method == "typename") {
    return makeHostFunction(runtime, name, 1, [state](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a, std::size_t n) {
      if (n < 1 || !a[0].isNumber()) throw jsi::JSError(runtime, "Expected type number");
      return jsi::String::createFromUtf8(runtime, lua_typename(state(), static_cast<int>(a[0].asNumber())));
    });
  }
  if (method == "gettop" || method == "status" || method == "resetthread") {
    return makeHostFunction(runtime, name, 0, [state, method](jsi::Runtime&, const jsi::Value&, const jsi::Value*, std::size_t) {
      if (method == "gettop") return jsi::Value(lua_gettop(state()));
      if (method == "status") return jsi::Value(lua_status(state()));
      return jsi::Value(lua_resetthread(state()));
    });
  }
  if (method == "resume") {
    return makeHostFunction(runtime, name, 2, [state](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a, std::size_t n) -> jsi::Value {
      if (n < 2) throw jsi::JSError(runtime, "Expected thread handle and argument count");
      auto* from = reinterpret_cast<lua_State*>(static_cast<std::uintptr_t>(a[0].asNumber()));
      int results = 0;
      const int code = lua_resume(state(), from, static_cast<int>(a[1].asNumber()), &results);
      jsi::Object value(runtime);
      value.setProperty(runtime, "result", code);
      value.setProperty(runtime, "nresults", results);
      return value;
    });
  }
  if (method == "yield") {
    return makeHostFunction(runtime, name, 1, [](jsi::Runtime& runtime,
        const jsi::Value&, const jsi::Value*, std::size_t) -> jsi::Value {
      throw jsi::JSError(
          runtime,
          "yield cannot safely cross a JSI HostFunction boundary; use Lua coroutine APIs");
    });
  }

  return jsi::Value::undefined();
}

std::vector<jsi::PropNameID> SKRNLuaInterpreter::getPropertyNames(jsi::Runtime& runtime) {
  std::vector<jsi::PropNameID> result;
  result.reserve(kInterpreterKeys.size());
  for (const std::string& key : kInterpreterKeys) {
    result.push_back(jsi::PropNameID::forUtf8(runtime, key));
  }
  return result;
}

void install(
    jsi::Runtime& runtime,
    std::shared_ptr<facebook::react::CallInvoker>) {
  auto factory = jsi::Function::createFromHostFunction(
      runtime,
      jsi::PropNameID::forAscii(runtime, "SKRNNativeLuaNewInterpreter"),
      1,
      [](jsi::Runtime& runtime, const jsi::Value&,
         const jsi::Value* arguments, std::size_t count) -> jsi::Value {
        auto interpreter = std::make_shared<SKRNLuaInterpreter>(
            nullptr, interpreterOptions(runtime, arguments, count));
        return jsi::Object::createFromHostObject(runtime, std::move(interpreter));
      });
  runtime.global().setProperty(
      runtime, "SKRNNativeLuaNewInterpreter", std::move(factory));
}

void cleanup(jsi::Runtime& runtime) {
  runtime.global().setProperty(
      runtime, "SKRNNativeLuaNewInterpreter", jsi::Value::undefined());
}

int multiply(float a, float b) {
  return static_cast<int>(a * b);
}

} // namespace SKRNNativeLua
