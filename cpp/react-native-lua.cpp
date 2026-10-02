#include "react-native-lua.h"
#include "LuaValueJsi.h"
#include "LuaValueInputJsi.h"
#include "LuaValueWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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

constexpr double kMaxSafeJsInteger = 9'007'199'254'740'991.0;

int requireJsInt(
    jsi::Runtime& runtime,
    const jsi::Value& value,
    const char* description) {
  if (!value.isNumber()) {
    throw jsi::JSError(runtime, std::string("Expected numeric ") + description);
  }
  const double number = value.asNumber();
  if (!std::isfinite(number) || std::trunc(number) != number ||
      number < static_cast<double>(std::numeric_limits<int>::min()) ||
      number > static_cast<double>(std::numeric_limits<int>::max())) {
    throw jsi::JSError(
        runtime, std::string("Invalid integer ") + description);
  }
  return static_cast<int>(number);
}

lua_Integer requireJsLuaInteger(
    jsi::Runtime& runtime,
    const jsi::Value& value,
    const char* description) {
  if (!value.isNumber()) {
    throw jsi::JSError(runtime, std::string("Expected numeric ") + description);
  }
  const double number = value.asNumber();
  if (!std::isfinite(number) || std::trunc(number) != number ||
      number < -kMaxSafeJsInteger || number > kMaxSafeJsInteger ||
      number < static_cast<double>(std::numeric_limits<lua_Integer>::min()) ||
      number > static_cast<double>(std::numeric_limits<lua_Integer>::max())) {
    throw jsi::JSError(
        runtime, std::string("Invalid Lua integer ") + description);
  }
  return static_cast<lua_Integer>(number);
}

std::size_t requireJsCount(
    jsi::Runtime& runtime,
    const jsi::Value& value,
    const char* description) {
  if (!value.isNumber()) {
    throw jsi::JSError(runtime, std::string("Expected numeric ") + description);
  }
  const double number = value.asNumber();
  if (!std::isfinite(number) || std::trunc(number) != number ||
      number < 0.0 || number > kMaxSafeJsInteger ||
      number > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
    throw jsi::JSError(
        runtime, std::string("Invalid non-negative count ") + description);
  }
  return static_cast<std::size_t>(number);
}

std::uint64_t requireJsTaskId(
    jsi::Runtime& runtime,
    const jsi::Value& value) {
  const std::size_t id = requireJsCount(runtime, value, "task id");
  return static_cast<std::uint64_t>(id);
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
  ToThreadHandle,
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
  ResumeThread,
};

struct StackOperationContext {
  StackOperationKind kind;
  int first = 0;
  lua_Integer second = 0;
  lua_Number number = 0;
  const char* text = nullptr;
  std::size_t textSize = 0;
  int intResult = 0;
  int resultCount = 0;
  lua_Integer integerResult = 0;
  lua_Number numberResult = 0;
  std::size_t sizeResult = 0;
  const char* textResult = nullptr;
  std::size_t textResultSize = 0;
  const void* pointerResult = nullptr;
};

void requireLuaStack(lua_State* state, int slots) {
  if (slots < 0 || !lua_checkstack(state, slots)) {
    luaL_error(state, "Lua stack limit exceeded");
  }
}

void requireStackElements(lua_State* state, int count) {
  if (count < 0 || lua_gettop(state) < count) {
    luaL_error(state, "Lua stack underflow");
  }
}

bool isValueIndex(lua_State* state, int index) {
  const int top = lua_gettop(state);
  return index == LUA_REGISTRYINDEX ||
      (index > 0 && index <= top) ||
      (index < 0 && index >= -top);
}

void requireValueIndex(lua_State* state, int index) {
  if (!isValueIndex(state, index)) {
    luaL_error(state, "invalid Lua stack index %d", index);
  }
}

void requireActualStackIndex(lua_State* state, int index) {
  const int top = lua_gettop(state);
  if (!((index > 0 && index <= top) ||
        (index < 0 && index >= -top))) {
    luaL_error(state, "invalid Lua stack index %d", index);
  }
}

void requireTableIndex(lua_State* state, int index) {
  requireValueIndex(state, index);
  if (lua_type(state, index) != LUA_TTABLE) {
    luaL_error(state, "Lua table expected at index %d", index);
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
      requireValueIndex(state, context->first);
      requireLuaStack(state, 1);
      lua_pushvalue(state, context->first);
      return;
    case StackOperationKind::RawGet:
      requireStackElements(state, 1);
      requireTableIndex(state, context->first);
      context->intResult = lua_rawget(state, context->first);
      return;
    case StackOperationKind::RawLen:
      requireValueIndex(state, context->first);
      context->sizeResult = lua_rawlen(state, context->first);
      return;
    case StackOperationKind::RawSet:
      requireStackElements(state, 2);
      requireTableIndex(state, context->first);
      lua_rawset(state, context->first);
      return;
    case StackOperationKind::Remove:
      requireActualStackIndex(state, context->first);
      lua_remove(state, context->first);
      return;
    case StackOperationKind::Insert:
      requireActualStackIndex(state, context->first);
      lua_insert(state, context->first);
      return;
    case StackOperationKind::Replace:
      requireStackElements(state, 1);
      requireActualStackIndex(state, context->first);
      lua_replace(state, context->first);
      return;
    case StackOperationKind::SetMetatable: {
      requireStackElements(state, 1);
      requireValueIndex(state, context->first);
      const int metatableType = lua_type(state, -1);
      if (metatableType != LUA_TNIL && metatableType != LUA_TTABLE) {
        luaL_error(state, "metatable must be a table or nil");
      }
      context->intResult = lua_setmetatable(state, context->first);
      return;
    }
    case StackOperationKind::SetTable:
      requireStackElements(state, 2);
      requireValueIndex(state, context->first);
      lua_settable(state, context->first);
      return;
    case StackOperationKind::SetTop: {
      const int top = lua_gettop(state);
      if (context->first >= 0) {
        requireLuaStack(state, std::max(0, context->first - top));
      } else if (context->first < -(top + 1)) {
        luaL_error(state, "invalid Lua stack top %d", context->first);
      }
      lua_settop(state, context->first);
      return;
    }
    case StackOperationKind::GetTable:
      requireStackElements(state, 1);
      requireValueIndex(state, context->first);
      context->intResult = lua_gettable(state, context->first);
      return;
    case StackOperationKind::ToBoolean:
      requireValueIndex(state, context->first);
      context->intResult = lua_toboolean(state, context->first);
      return;
    case StackOperationKind::ToClose:
      requireActualStackIndex(state, context->first);
      lua_toclose(state, context->first);
      return;
    case StackOperationKind::ToInteger:
      requireValueIndex(state, context->first);
      context->integerResult = lua_tointeger(state, context->first);
      return;
    case StackOperationKind::ToNumber:
      requireValueIndex(state, context->first);
      context->numberResult = lua_tonumber(state, context->first);
      return;
    case StackOperationKind::ToString:
      requireValueIndex(state, context->first);
      context->textResult =
          lua_tolstring(state, context->first, &context->textResultSize);
      return;
    case StackOperationKind::ToPointer:
      requireValueIndex(state, context->first);
      context->pointerResult = lua_topointer(state, context->first);
      return;
    case StackOperationKind::ToThreadHandle:
      requireValueIndex(state, context->first);
      if (lua_type(state, context->first) != LUA_TTHREAD) {
        luaL_error(state, "Lua thread expected at index %d", context->first);
      }
      requireLuaStack(state, 1);
      lua_pushvalue(state, context->first);
      context->intResult = luaL_ref(state, LUA_REGISTRYINDEX);
      return;
    case StackOperationKind::Type:
      requireValueIndex(state, context->first);
      context->intResult = lua_type(state, context->first);
      return;
    case StackOperationKind::RawEqual:
      requireValueIndex(state, context->first);
      requireValueIndex(state, static_cast<int>(context->second));
      context->intResult =
          lua_rawequal(state, context->first, static_cast<int>(context->second));
      return;
    case StackOperationKind::RawGetI:
      requireTableIndex(state, context->first);
      requireLuaStack(state, 1);
      context->intResult = lua_rawgeti(state, context->first, context->second);
      return;
    case StackOperationKind::RawSetI:
      requireStackElements(state, 1);
      requireTableIndex(state, context->first);
      lua_rawseti(state, context->first, context->second);
      return;
    case StackOperationKind::Rotate: {
      requireActualStackIndex(state, context->first);
      const int amount = static_cast<int>(context->second);
      const int top = lua_gettop(state);
      const int segmentLength =
          context->first > 0 ? top - context->first + 1 : -context->first;
      const long long amount64 = static_cast<long long>(amount);
      if (amount64 > segmentLength || amount64 < -segmentLength) {
        luaL_error(state, "invalid Lua rotation amount %d", amount);
      }
      lua_rotate(state, context->first, amount);
      return;
    }
    case StackOperationKind::SetI:
      requireStackElements(state, 1);
      requireValueIndex(state, context->first);
      lua_seti(state, context->first, context->second);
      return;
    case StackOperationKind::SetIUserValue:
      requireStackElements(state, 1);
      requireActualStackIndex(state, context->first);
      if (lua_type(state, context->first) != LUA_TUSERDATA) {
        luaL_error(state, "full userdata expected at index %d", context->first);
      }
      context->intResult =
          lua_setiuservalue(state, context->first, static_cast<int>(context->second));
      return;
    case StackOperationKind::SetField:
      requireStackElements(state, 1);
      requireValueIndex(state, context->first);
      lua_setfield(state, context->first, context->text);
      return;
    case StackOperationKind::SetGlobal:
      requireStackElements(state, 1);
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
    case StackOperationKind::ResumeThread: {
      const int argumentCount = static_cast<int>(context->second);
      if (context->first <= 0 || argumentCount < 0) {
        luaL_error(state, "invalid Lua thread handle or argument count");
      }
      requireStackElements(state, argumentCount);
      requireLuaStack(state, 1);
      const int type =
          lua_rawgeti(state, LUA_REGISTRYINDEX, context->first);
      if (type != LUA_TTHREAD) {
        lua_pop(state, 1);
        luaL_error(state, "invalid or expired Lua thread handle");
      }
      lua_State* thread = lua_tothread(state, -1);
      lua_pop(state, 1);
      if (thread == nullptr || thread == state) {
        luaL_error(state, "coroutine thread expected");
      }
      if (!lua_checkstack(thread, argumentCount)) {
        luaL_error(state, "Lua coroutine stack limit exceeded");
      }
      if (argumentCount > 0) {
        lua_xmove(state, thread, argumentCount);
      }

      int resultCount = 0;
      const int code =
          lua_resume(thread, state, argumentCount, &resultCount);
      context->intResult = code;
      context->resultCount = resultCount;

      if (resultCount > 0) {
        requireLuaStack(state, resultCount);
        lua_xmove(thread, state, resultCount);
      }
      if (code != LUA_YIELD) {
        luaL_unref(state, LUA_REGISTRYINDEX, context->first);
      }
      return;
    }
  }
}

const std::vector<std::string> kInterpreterKeys = {
    "dostringasync", "dofileasync", "dostring", "dofile", "printCount",
    "executeStringResult", "executeFileResult", "startStringAsync",
    "startFileAsync", "takeAsyncResult", "executing",
    "readValues", "readGlobals", "readValue", "readGlobal",
    "pushValue", "pushValues", "setGlobal", "setGlobals",
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

  if (method == "readValues" || method == "readGlobals" ||
      method == "readValue" || method == "readGlobal") {
    return makeHostFunction(runtime, name, 2, [self, method](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* arguments,
        std::size_t count) -> jsi::Value {
      // Input accessors and result construction belong to JS, outside the Lua
      // critical section. Snapshot everything before acquiring execution rights.
      const auto request = rnlua::parseValueReadRequest(runtime, method, arguments, count);
      const auto objectCreate = runtime.global().getPropertyAsObject(runtime, "Object")
          .getPropertyAsFunction(runtime, "create");
      if (self->destroyed_.load() || self->lua_ == nullptr) {
        throw jsi::JSError(runtime, "Lua interpreter is destroyed");
      }
      bool expected = false;
      if (!self->executing_.compare_exchange_strong(expected, true)) {
        throw jsi::JSError(runtime, "Lua interpreter is executing; bulk reads require an idle interpreter");
      }
      std::vector<rnlua::LuaValue> values;
      try {
        values = request.globals
            ? rnlua::readLuaGlobals(*self->lua_, request.names, request.options)
            : rnlua::readLuaValues(*self->lua_, request.indices, request.options);
      } catch (const std::exception& error) {
        self->executing_.store(false);
        throw jsi::JSError(runtime, error.what());
      } catch (...) {
        self->executing_.store(false);
        throw jsi::JSError(runtime, "Lua bulk read failed");
      }
      self->executing_.store(false);
      return rnlua::valueReadResult(runtime, request, values, objectCreate);
    });
  }

  if (method == "pushValue" || method == "pushValues" ||
      method == "setGlobal" || method == "setGlobals") {
    const unsigned int arity = method == "setGlobal" ? 3u : 2u;
    return makeHostFunction(runtime, name, arity, [self, method](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* arguments,
        std::size_t count) -> jsi::Value {
      // Snapshot all JS input before claiming the Lua execution gate. Property
      // accessors/proxies may execute arbitrary JS and can reenter this interpreter.
      const auto request =
          rnlua::parseValueInputRequest(runtime, method, arguments, count);
      if (self->destroyed_.load() || self->lua_ == nullptr) {
        throw jsi::JSError(runtime, "Lua interpreter is destroyed");
      }
      bool expected = false;
      if (!self->executing_.compare_exchange_strong(expected, true)) {
        throw jsi::JSError(
            runtime,
            "Lua interpreter is executing; bulk push/set requires an idle interpreter");
      }
      try {
        if (request.globals) {
          rnlua::setLuaGlobals(
              *self->lua_, request.names, request.values, request.limits);
        } else {
          rnlua::pushLuaValues(*self->lua_, request.values, request.limits);
        }
      } catch (const std::exception& error) {
        self->executing_.store(false);
        throw jsi::JSError(runtime, error.what());
      } catch (...) {
        self->executing_.store(false);
        throw jsi::JSError(runtime, "Lua bulk push/set failed");
      }
      self->executing_.store(false);
      return jsi::Value::undefined();
    });
  }

  if (method == "getPrint") {
    return makeHostFunction(runtime, name, 1, [self](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* arguments,
        std::size_t count) -> jsi::Value {
      const std::size_t requested =
          count > 0 ? requireJsCount(runtime, arguments[0], "print count") : 0;
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
      if (count < 1) {
        throw jsi::JSError(runtime, "Expected an async task id");
      }
      const std::uint64_t taskId = requireJsTaskId(runtime, arguments[0]);
      rnlua::ExecutionResult result;
      if (!self->takeAsyncResult(taskId, result)) {
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
      if (n < 1) throw jsi::JSError(runtime, "Expected count");
      const int count = requireJsInt(runtime, a[0], "pop count");
      if (count < 0) {
        throw jsi::JSError(runtime, "Pop count must be non-negative");
      }
      StackOperationContext operation{StackOperationKind::SetTop};
      const int top = self->state_ == nullptr ? 0 : lua_gettop(self->state_);
      operation.first = std::max(0, top - count);
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
      const double number = a[0].asNumber();
      operation.first = number != 0 ? 1 : 0;
      operation.second = method == "pushinteger"
          ? requireJsLuaInteger(runtime, a[0], "value")
          : 0;
      operation.number = number;
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
        method == "tothread" ? StackOperationKind::ToThreadHandle :
                               StackOperationKind::Type;
    return makeHostFunction(runtime, name, 1, [self, method, kind](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a,
        std::size_t n) -> jsi::Value {
      if (n < 1) {
        throw jsi::JSError(runtime, "Expected stack index");
      }
      StackOperationContext operation{kind};
      operation.first = requireJsInt(runtime, a[0], "stack index");
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
        return jsi::Value(static_cast<double>(operation.intResult));
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
      if (n < 2) {
        throw jsi::JSError(runtime, "Expected two numeric arguments");
      }
      StackOperationContext operation{kind};
      operation.first = requireJsInt(runtime, a[0], "stack index");
      if (method == "rawequal" || method == "rotate" ||
          method == "setiuservalue") {
        operation.second =
            requireJsInt(runtime, a[1], "stack index/slot/rotation");
      } else {
        operation.second =
            requireJsLuaInteger(runtime, a[1], "Lua integer key");
      }
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
      if (n < 2 || !a[1].isString()) {
        throw jsi::JSError(runtime, "Expected index and field");
      }
      const std::string key = a[1].asString(runtime).utf8(runtime);
      StackOperationContext operation{StackOperationKind::SetField};
      operation.first = requireJsInt(runtime, a[0], "stack index");
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
      if (n < 1) throw jsi::JSError(runtime, "Expected type number");
      const int type = requireJsInt(runtime, a[0], "Lua type");
      if (type < LUA_TNONE || type >= LUA_NUMTYPES) {
        throw jsi::JSError(runtime, "Invalid Lua type number");
      }
      return jsi::String::createFromUtf8(runtime, lua_typename(state(), type));
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
    return makeHostFunction(runtime, name, 2, [self](
        jsi::Runtime& runtime, const jsi::Value&, const jsi::Value* a,
        std::size_t n) -> jsi::Value {
      if (n < 2) {
        throw jsi::JSError(runtime, "Expected thread handle and argument count");
      }
      StackOperationContext operation{StackOperationKind::ResumeThread};
      operation.first = requireJsInt(runtime, a[0], "thread handle");
      const int argumentCount =
          requireJsInt(runtime, a[1], "resume argument count");
      if (argumentCount < 0) {
        throw jsi::JSError(runtime, "Resume argument count must be non-negative");
      }
      operation.second = argumentCount;
      self->runProtectedStateOperation(
          runtime, &performStackOperation, &operation);
      jsi::Object value(runtime);
      value.setProperty(runtime, "result", operation.intResult);
      value.setProperty(runtime, "nresults", operation.resultCount);
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
