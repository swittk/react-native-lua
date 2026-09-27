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

enum class StackOperationKind {
  PushBoolean,
  PushInteger,
  PushNumber,
  PushNil,
  PushGlobalTable,
  PushString,
  PushThread,
  PushValue,
  RawGet,
  RawLen,
  RawSet,
  Remove,
  Insert,
  Replace,
  SetMetatable,
  SetTable,
  SetTop,
  GetTable,
  ToBoolean,
  ToClose,
  ToInteger,
  ToNumber,
  ToString,
  ToPointer,
  ToThread,
  Type,
  RawEqual,
  RawGetI,
  RawSetI,
  Rotate,
  SetI,
  SetIUserValue,
  SetField,
  SetGlobal,
  GetGlobal,
  VarAsNumber,
  StringToNumber,
};

struct StackOperationContext {
  StackOperationKind kind;
  int first = 0;
  lua_Integer second = 0;
  lua_Number number = 0;
  const char* text = nullptr;
  std::size_t textSize = 0;
  int intResult = 0;
  lua_Integer integerResult = 0;
  lua_Number numberResult = 0;
  std::size_t sizeResult = 0;
  const char* textResult = nullptr;
  std::size_t textResultSize = 0;
  const void* pointerResult = nullptr;
  lua_State* threadResult = nullptr;
};

void requireLuaStack(lua_State* state, int slots) {
  if (!lua_checkstack(state, slots)) {
    luaL_error(state, "Lua stack limit exceeded");
  }
}

void performStackOperation(lua_State* state, void* rawContext) {
  auto* context = static_cast<StackOperationContext*>(rawContext);
  switch (context->kind) {
    case StackOperationKind::PushBoolean:
      requireLuaStack(state, 1);
      lua_pushboolean(state, context->first != 0);
      return;
    case StackOperationKind::PushInteger:
      requireLuaStack(state, 1);
      lua_pushinteger(state, context->second);
      return;
    case StackOperationKind::PushNumber:
      requireLuaStack(state, 1);
      lua_pushnumber(state, context->number);
      return;
    case StackOperationKind::PushNil:
      requireLuaStack(state, 1);
      lua_pushnil(state);
      return;
    case StackOperationKind::PushGlobalTable:
      requireLuaStack(state, 1);
      lua_pushglobaltable(state);
      return;
    case StackOperationKind::PushString:
      requireLuaStack(state, 1);
      lua_pushlstring(state, context->text, context->textSize);
      return;
    case StackOperationKind::PushThread:
      requireLuaStack(state, 1);
      context->intResult = lua_pushthread(state);
      return;
    case StackOperationKind::PushValue:
      requireLuaStack(state, 1);
      lua_pushvalue(state, context->first);
      return;
    case StackOperationKind::RawGet:
      context->intResult = lua_rawget(state, context->first);
      return;
    case StackOperationKind::RawLen:
      context->sizeResult = lua_rawlen(state, context->first);
      return;
    case StackOperationKind::RawSet:
      lua_rawset(state, context->first);
      return;
    case StackOperationKind::Remove:
      lua_remove(state, context->first);
      return;
    case StackOperationKind::Insert:
      lua_insert(state, context->first);
      return;
    case StackOperationKind::Replace:
      lua_replace(state, context->first);
      return;
    case StackOperationKind::SetMetatable:
      context->intResult = lua_setmetatable(state, context->first);
      return;
    case StackOperationKind::SetTable:
      lua_settable(state, context->first);
      return;
    case StackOperationKind::SetTop:
      lua_settop(state, context->first);
      return;
    case StackOperationKind::GetTable:
      context->intResult = lua_gettable(state, context->first);
      return;
    case StackOperationKind::ToBoolean:
      context->intResult = lua_toboolean(state, context->first);
      return;
    case StackOperationKind::ToClose:
      lua_toclose(state, context->first);
      return;
    case StackOperationKind::ToInteger:
      context->integerResult = lua_tointeger(state, context->first);
      return;
    case StackOperationKind::ToNumber:
      context->numberResult = lua_tonumber(state, context->first);
      return;
    case StackOperationKind::ToString:
      context->textResult =
          lua_tolstring(state, context->first, &context->textResultSize);
      return;
    case StackOperationKind::ToPointer:
      context->pointerResult = lua_topointer(state, context->first);
      return;
    case StackOperationKind::ToThread:
      context->threadResult = lua_tothread(state, context->first);
      return;
    case StackOperationKind::Type:
      context->intResult = lua_type(state, context->first);
      return;
    case StackOperationKind::RawEqual:
      context->intResult =
          lua_rawequal(state, context->first, static_cast<int>(context->second));
      return;
    case StackOperationKind::RawGetI:
      requireLuaStack(state, 1);
      context->intResult = lua_rawgeti(state, context->first, context->second);
      return;
    case StackOperationKind::RawSetI:
      lua_rawseti(state, context->first, context->second);
      return;
    case StackOperationKind::Rotate:
      lua_rotate(state, context->first, static_cast<int>(context->second));
      return;
    case StackOperationKind::SetI:
      lua_seti(state, context->first, context->second);
      return;
    case StackOperationKind::SetIUserValue:
      context->intResult =
          lua_setiuservalue(state, context->first, static_cast<int>(context->second));
      return;
    case StackOperationKind::SetField:
      lua_setfield(state, context->first, context->text);
      return;
    case StackOperationKind::SetGlobal:
      lua_setglobal(state, context->text);
      return;
    case StackOperationKind::GetGlobal:
      requireLuaStack(state, 1);
      context->intResult = lua_getglobal(state, context->text);
      return;
    case StackOperationKind::VarAsNumber:
      requireLuaStack(state, 1);
      lua_getglobal(state, context->text);
      context->numberResult =
          lua_isnumber(state, -1) ? lua_tonumber(state, -1) : 0;
      lua_pop(state, 1);
      return;
    case StackOperationKind::StringToNumber:
      requireLuaStack(state, 1);
      context->sizeResult = lua_stringtonumber(state, context->text);
      return;
  }
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

void SKRNLuaInterpreter::runProtectedStateOperation(
    jsi::Runtime& runtime,
    rnlua::LuaRuntime::ProtectedStateOperation operation,
    void* context) {
  if (lua_ == nullptr || destroyed_.load() || !lua_->isOpen()) {
    throw jsi::JSError(runtime, "Lua interpreter is destroyed");
  }
  if (executing_.load()) {
    throw jsi::JSError(runtime, "Lua interpreter is executing asynchronously");
  }
  try {
    lua_->runProtectedStateOperation(operation, context);
  } catch (const std::exception& error) {
    throw jsi::JSError(runtime, error.what());
  }
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
    return makeHostFunction(runtime, name, 1, [self](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a, std::size_t n) {
      if (n < 1 || !a[0].isNumber()) throw jsi::JSError(runtime, "Expected count");
      StackOperationContext operation{StackOperationKind::SetTop};
      const int count = static_cast<int>(a[0].asNumber());
      const int top = self->state_ == nullptr ? 0 : lua_gettop(self->state_);
      operation.first = std::max(0, top - std::max(0, count));
      self->runProtectedStateOperation(runtime, &performStackOperation, &operation);
      return jsi::Value::undefined();
    });
  }
  if (method == "pushboolean" || method == "pushinteger" || method == "pushnumber") {
    return makeHostFunction(runtime, name, 1, [self, method](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a, std::size_t n) {
      if (n < 1 || !a[0].isNumber()) throw jsi::JSError(runtime, "Expected number");
      StackOperationContext operation{
          method == "pushboolean" ? StackOperationKind::PushBoolean
          : method == "pushinteger" ? StackOperationKind::PushInteger
                                    : StackOperationKind::PushNumber};
      operation.first = a[0].asNumber() != 0 ? 1 : 0;
      operation.second = static_cast<lua_Integer>(a[0].asNumber());
      operation.number = a[0].asNumber();
      self->runProtectedStateOperation(runtime, &performStackOperation, &operation);
      return jsi::Value::undefined();
    });
  }
  if (method == "pushnil" || method == "pushglobaltable") {
    return makeHostFunction(runtime, name, 0, [self, method](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value*, std::size_t) {
      StackOperationContext operation{
          method == "pushnil" ? StackOperationKind::PushNil
                              : StackOperationKind::PushGlobalTable};
      self->runProtectedStateOperation(runtime, &performStackOperation, &operation);
      return jsi::Value::undefined();
    });
  }
  if (method == "pushstring") {
    return makeHostFunction(runtime, name, 1, [self](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a, std::size_t n) {
      if (n < 1 || !a[0].isString()) throw jsi::JSError(runtime, "Expected string");
      const std::string value = a[0].asString(runtime).utf8(runtime);
      StackOperationContext operation{StackOperationKind::PushString};
      operation.text = value.data();
      operation.textSize = value.size();
      self->runProtectedStateOperation(runtime, &performStackOperation, &operation);
      return jsi::Value::undefined();
    });
  }
  if (method == "pushthread") {
    return makeHostFunction(runtime, name, 0, [self](jsi::Runtime& runtime, const jsi::Value&, const jsi::Value*, std::size_t) {
      StackOperationContext operation{StackOperationKind::PushThread};
      self->runProtectedStateOperation(runtime, &performStackOperation, &operation);
      return jsi::Value(operation.intResult);
    });
  }
  if (method == "pushvalue" || method == "rawget" || method == "rawlen" ||
      method == "rawset" || method == "remove" || method == "insert" ||
      method == "replace" || method == "setmetatable" || method == "settable" ||
      method == "settop" || method == "gettable" || method == "toboolean" ||
      method == "toclose" || method == "tointeger" || method == "tonumber" ||
      method == "tostring" || method == "topointer" ||
      method == "tothread" || method == "type") {
    StackOperationKind kind =
        method == "pushvalue" ? StackOperationKind::PushValue :
        method == "rawget" ? StackOperationKind::RawGet :
        method == "rawlen" ? StackOperationKind::RawLen :
        method == "rawset" ? StackOperationKind::RawSet :
        method == "remove" ? StackOperationKind::Remove :
        method == "insert" ? StackOperationKind::Insert :
        method == "replace" ? StackOperationKind::Replace :
        method == "setmetatable" ? StackOperationKind::SetMetatable :
        method == "settable" ? StackOperationKind::SetTable :
        method == "settop" ? StackOperationKind::SetTop :
        method == "gettable" ? StackOperationKind::GetTable :
        method == "toboolean" ? StackOperationKind::ToBoolean :
        method == "toclose" ? StackOperationKind::ToClose :
        method == "tointeger" ? StackOperationKind::ToInteger :
        method == "tonumber" ? StackOperationKind::ToNumber :
        method == "tostring" ? StackOperationKind::ToString :
        method == "topointer" ? StackOperationKind::ToPointer :
        method == "tothread" ? StackOperationKind::ToThread :
                               StackOperationKind::Type;
    return makeHostFunction(runtime, name, 1, [self, method, kind](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a,
        std::size_t n) -> jsi::Value {
      const int index =
          n > 0 && a[0].isNumber() ? static_cast<int>(a[0].asNumber()) : -1;
      StackOperationContext operation{kind};
      operation.first = index;
      self->runProtectedStateOperation(runtime, &performStackOperation, &operation);
      if (method == "rawget" || method == "setmetatable" ||
          method == "gettable" || method == "toboolean" ||
          method == "type") {
        return jsi::Value(operation.intResult);
      }
      if (method == "rawlen") {
        return jsi::Value(static_cast<double>(operation.sizeResult));
      }
      if (method == "tointeger") {
        return jsi::Value(static_cast<double>(operation.integerResult));
      }
      if (method == "tonumber") {
        return jsi::Value(operation.numberResult);
      }
      if (method == "tostring") {
        return operation.textResult == nullptr
            ? jsi::Value::null()
            : jsi::String::createFromUtf8(
                  runtime,
                  std::string(operation.textResult, operation.textResultSize));
      }
      if (method == "topointer") {
        return jsi::Value(static_cast<double>(
            reinterpret_cast<std::uintptr_t>(operation.pointerResult)));
      }
      if (method == "tothread") {
        return jsi::Value(static_cast<double>(
            reinterpret_cast<std::uintptr_t>(operation.threadResult)));
      }
      return jsi::Value::undefined();
    });
  }
  if (method == "rawequal" || method == "rawgeti" || method == "rawseti" ||
      method == "rotate" || method == "seti" || method == "setiuservalue") {
    StackOperationKind kind =
        method == "rawequal" ? StackOperationKind::RawEqual :
        method == "rawgeti" ? StackOperationKind::RawGetI :
        method == "rawseti" ? StackOperationKind::RawSetI :
        method == "rotate" ? StackOperationKind::Rotate :
        method == "seti" ? StackOperationKind::SetI :
                           StackOperationKind::SetIUserValue;
    return makeHostFunction(runtime, name, 2, [self, method, kind](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a,
        std::size_t n) -> jsi::Value {
      if (n < 2 || !a[0].isNumber() || !a[1].isNumber()) {
        throw jsi::JSError(runtime, "Expected two numbers");
      }
      StackOperationContext operation{kind};
      operation.first = static_cast<int>(a[0].asNumber());
      operation.second = static_cast<lua_Integer>(a[1].asNumber());
      self->runProtectedStateOperation(runtime, &performStackOperation, &operation);
      if (method == "rawequal" || method == "rawgeti" ||
          method == "setiuservalue") {
        return jsi::Value(operation.intResult);
      }
      return jsi::Value::undefined();
    });
  }
  if (method == "setfield") {
    return makeHostFunction(runtime, name, 2, [self](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a,
        std::size_t n) {
      if (n < 2 || !a[0].isNumber() || !a[1].isString()) {
        throw jsi::JSError(runtime, "Expected index and field");
      }
      const std::string key = a[1].asString(runtime).utf8(runtime);
      StackOperationContext operation{StackOperationKind::SetField};
      operation.first = static_cast<int>(a[0].asNumber());
      operation.text = key.c_str();
      self->runProtectedStateOperation(runtime, &performStackOperation, &operation);
      return jsi::Value::undefined();
    });
  }
  if (method == "setglobal" || method == "getglobal" ||
      method == "var_asnumber" || method == "stringtonumber") {
    StackOperationKind kind =
        method == "setglobal" ? StackOperationKind::SetGlobal :
        method == "getglobal" ? StackOperationKind::GetGlobal :
        method == "var_asnumber" ? StackOperationKind::VarAsNumber :
                                   StackOperationKind::StringToNumber;
    return makeHostFunction(runtime, name, 1, [self, method, kind](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a,
        std::size_t n) -> jsi::Value {
      if (n < 1 || !a[0].isString()) {
        throw jsi::JSError(runtime, "Expected string");
      }
      const std::string value = a[0].asString(runtime).utf8(runtime);
      StackOperationContext operation{kind};
      operation.text = value.c_str();
      self->runProtectedStateOperation(runtime, &performStackOperation, &operation);
      if (method == "stringtonumber") {
        return jsi::Value(static_cast<double>(operation.sizeResult));
      }
      if (method == "var_asnumber") {
        return jsi::Value(operation.numberResult);
      }
      return jsi::Value::undefined();
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
