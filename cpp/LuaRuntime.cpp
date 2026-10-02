#include "LuaRuntime.h"
#include "LuaSocket.h"
#include "RuntimeSocketHooks.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

extern "C" {
#include "lua_src/lauxlib.h"
#include "lua_src/ldo.h"
#include "lua_src/lstate.h"
#include "lua_src/lua.h"
#include "lua_src/lualib.h"
}

namespace rnlua {
namespace {

constexpr const char* kDeadlineMarker = "__REACT_NATIVE_LUA_DEADLINE__";
constexpr const char* kCancellationMarker = "__REACT_NATIVE_LUA_CANCELLED__";
constexpr std::size_t kMinimumMemoryLimit = 512 * 1024;
constexpr std::size_t kMaximumMemoryLimit = 256 * 1024 * 1024;
constexpr std::int64_t kMaximumExecutionLimitMs = 5 * 60 * 1'000;
constexpr std::size_t kMaximumOutputBytes = 1024 * 1024;
constexpr std::size_t kMaximumOutputLines = 10'000;
constexpr double kSocketWaitSliceSeconds = 0.05;

thread_local lua_State* activeSocketState = nullptr;
thread_local std::atomic<bool>* activeCancellation = nullptr;
thread_local std::atomic<std::int64_t>* activeDeadlineNs = nullptr;
thread_local std::atomic<int>* activeInterruptCode = nullptr;

LuaRuntime* runtimeFor(lua_State* state) {
  return *static_cast<LuaRuntime**>(lua_getextraspace(state));
}

void openLibrary(lua_State* state, const char* name, lua_CFunction function) {
  luaL_requiref(state, name, function, 1);
  lua_pop(state, 1);
}

std::string errorAtTop(lua_State* state) {
  if (state == nullptr || lua_gettop(state) == 0) {
    return {};
  }
  std::size_t size = 0;
  const char* message = lua_tolstring(state, -1, &size);
  return message == nullptr ? std::string{} : std::string(message, size);
}

} // namespace

LuaRuntime::LuaRuntime(InterpreterOptions options) : options_(options) {
  options_.executionLimitMs = std::clamp<std::int64_t>(
      options_.executionLimitMs, 1, kMaximumExecutionLimitMs);
  options_.memoryLimitBytes = std::clamp<std::size_t>(
      options_.memoryLimitBytes, kMinimumMemoryLimit, kMaximumMemoryLimit);
  options_.maxOutputBytes =
      std::min(options_.maxOutputBytes, kMaximumOutputBytes);
  options_.maxOutputLines =
      std::min(options_.maxOutputLines, kMaximumOutputLines);
  memory_.limit.store(options_.memoryLimitBytes, std::memory_order_relaxed);

  state_ = lua_newstate(&LuaRuntime::allocate, &memory_);
  if (state_ == nullptr) {
    throw std::runtime_error("Unable to allocate the Lua interpreter");
  }

  *static_cast<LuaRuntime**>(lua_getextraspace(state_)) = this;
  openLibraries();

  lua_pushlightuserdata(state_, this);
  lua_pushcclosure(state_, &LuaRuntime::print, 1);
  lua_setglobal(state_, "print");
  lua_sethook(
      state_, &LuaRuntime::debugHook, LUA_MASKCOUNT, 1'000);
}

LuaRuntime::~LuaRuntime() {
  close();
}

void* LuaRuntime::allocate(void* userData, void* pointer, std::size_t oldSize,
                           std::size_t newSize) noexcept {
  auto* memory = static_cast<MemoryState*>(userData);
  const std::size_t accountedOldSize = pointer == nullptr ? 0 : oldSize;

  if (newSize == 0) {
    std::free(pointer);
    const std::size_t used = memory->used.load(std::memory_order_relaxed);
    memory->used.store(
        accountedOldSize > used ? 0 : used - accountedOldSize,
        std::memory_order_relaxed);
    return nullptr;
  }

#ifdef RNLUA_TESTING
  const std::int64_t failAfter = memory->failAfter.load(std::memory_order_relaxed);
  if (failAfter == 0) return nullptr;
  if (failAfter > 0) {
    memory->failAfter.store(failAfter - 1, std::memory_order_relaxed);
  }
#endif

  const std::size_t used = memory->used.load(std::memory_order_relaxed);
  const std::size_t base = accountedOldSize > used ? 0 : used - accountedOldSize;
  const std::size_t limit = memory->limit.load(std::memory_order_relaxed);
  if (newSize > limit || base > limit - newSize) {
    return nullptr;
  }

  void* resized = std::realloc(pointer, newSize);
  if (resized == nullptr) {
    return nullptr;
  }

  const std::size_t next = base + newSize;
  memory->used.store(next, std::memory_order_relaxed);
  std::size_t peak = memory->peak.load(std::memory_order_relaxed);
  while (next > peak &&
         !memory->peak.compare_exchange_weak(
             peak, next, std::memory_order_relaxed, std::memory_order_relaxed)) {
  }
  return resized;
}

void LuaRuntime::debugHook(lua_State* state, lua_Debug*) {
  LuaRuntime* runtime = runtimeFor(state);
  if (runtime == nullptr) {
    return;
  }

  int interrupt = runtime->interruptCode_.load(std::memory_order_relaxed);
  if (interrupt == 0 &&
      runtime->cancellationRequested_.load(std::memory_order_relaxed)) {
    interrupt = kCancelled;
    runtime->interruptCode_.store(interrupt, std::memory_order_relaxed);
  }
  const std::int64_t deadline =
      runtime->deadlineNs_.load(std::memory_order_relaxed);
  if (interrupt == 0 && deadline > 0 && steadyNowNs() >= deadline) {
    interrupt = kDeadlineExceeded;
    runtime->interruptCode_.store(interrupt, std::memory_order_relaxed);
  }

  if (interrupt != 0) {
    // Make the interruption sticky. If a script catches this error with pcall
    // or xpcall, the next VM instruction raises it again outside that protected
    // call until the host's outer lua_pcall unwinds.
    lua_sethook(state, &LuaRuntime::debugHook, LUA_MASKCOUNT, 1);
    if (runtime->state_ != nullptr && runtime->state_ != state) {
      lua_sethook(runtime->state_, &LuaRuntime::debugHook, LUA_MASKCOUNT, 1);
    }
    luaL_error(
        state,
        "%s",
        interrupt == kCancelled ? kCancellationMarker : kDeadlineMarker);
    return;
  }

  // A coroutine can retain the one-instruction hook after an interrupted run.
  // Restore the normal cadence the first time it executes in a later run.
  lua_sethook(state, &LuaRuntime::debugHook, LUA_MASKCOUNT, 1'000);
}

int LuaRuntime::print(lua_State* state) {
  auto* runtime = static_cast<LuaRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
  if (runtime == nullptr) {
    return 0;
  }

  const int count = lua_gettop(state);
  std::string line;
  for (int index = 1; index <= count; ++index) {
    if (index > 1) {
      line.push_back('\t');
    }
    std::size_t size = 0;
    const char* value = luaL_tolstring(state, index, &size);
    if (value != nullptr) {
      line.append(value, size);
    }
    lua_pop(state, 1);
  }
  runtime->appendOutput(std::move(line));
  return 0;
}

int LuaRuntime::textOnlyLoad(lua_State* state) {
  const int argc = lua_gettop(state) >= 4 ? 4 : 3;
  lua_settop(state, argc);
  lua_pushliteral(state, "t");
  lua_replace(state, 3);
  lua_pushvalue(state, lua_upvalueindex(1));
  lua_insert(state, 1);
  lua_call(state, argc, LUA_MULTRET);
  return lua_gettop(state);
}

int LuaRuntime::guardedSetmetatable(lua_State* state) {
  if (lua_type(state, 2) == LUA_TTABLE) {
    lua_pushliteral(state, "__gc");
    const int type = lua_rawget(state, 2);
    lua_pop(state, 1);
    if (type != LUA_TNIL) {
      return luaL_error(state, "__gc metamethods are not permitted");
    }
  }
  lua_pushvalue(state, lua_upvalueindex(1));
  lua_insert(state, 1);
  lua_call(state, lua_gettop(state) - 1, 1);
  return 1;
}

void LuaRuntime::openLibraries() {
  openLibrary(state_, LUA_GNAME, luaopen_base);
  openLibrary(state_, LUA_COLIBNAME, luaopen_coroutine);
  openLibrary(state_, LUA_TABLIBNAME, luaopen_table);
  openLibrary(state_, LUA_STRLIBNAME, luaopen_string);
  openLibrary(state_, LUA_MATHLIBNAME, luaopen_math);
  openLibrary(state_, LUA_UTF8LIBNAME, luaopen_utf8);

  if (options_.allowNetwork) {
    openLuaSocket(state_);
  }

  lua_pushnil(state_);
  lua_setglobal(state_, "dofile");
  lua_pushnil(state_);
  lua_setglobal(state_, "loadfile");

  // Lua disables hooks while __gc finalizers run. Untrusted scripts therefore
  // cannot install Lua finalizers that would bypass cancellation/deadlines.
  lua_getglobal(state_, "setmetatable");
  lua_pushcclosure(state_, &LuaRuntime::guardedSetmetatable, 1);
  lua_setglobal(state_, "setmetatable");

  if (!options_.allowBytecode) {
    // Delegate to Lua's original load() so reader-function chunks and the
    // optional environment argument keep their standard Lua 5.4 semantics.
    lua_getglobal(state_, "load");
    lua_pushcclosure(state_, &LuaRuntime::textOnlyLoad, 1);
    lua_setglobal(state_, "load");
  }
}

ExecutionResult LuaRuntime::executeString(const std::string& source) {
  if (!isOpen()) {
    return ExecutionResult{kDestroyed, LUA_ERRRUN, "destroyed", "Interpreter is destroyed"};
  }

  outputTruncated_.store(false, std::memory_order_relaxed);
  interruptCode_.store(0, std::memory_order_relaxed);
  lua_sethook(state_, &LuaRuntime::debugHook, LUA_MASKCOUNT, 1'000);
  const auto started = std::chrono::steady_clock::now();
  const std::int64_t limitNs = options_.executionLimitMs * 1'000'000;
  deadlineNs_.store(steadyNowNs() + limitNs, std::memory_order_relaxed);
  const int loadStatus =
      luaL_loadbufferx(state_, source.data(), source.size(), "=(react-native-lua)", loadMode());
  ExecutionResult result = executeLoadedChunk(loadStatus);
  deadlineNs_.store(0, std::memory_order_relaxed);
  result.durationMs = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started)
                          .count();
  result.memoryUsedBytes = memoryUsedBytes();
  result.peakMemoryBytes = peakMemoryBytes();
  result.outputTruncated = outputWasTruncated();
  return result;
}

ExecutionResult LuaRuntime::executeFile(const std::string& path) {
  if (!isOpen()) {
    return ExecutionResult{kDestroyed, LUA_ERRRUN, "destroyed", "Interpreter is destroyed"};
  }
  if (!options_.allowFileSystem) {
    ExecutionResult result;
    result.code = kFileSystemDenied;
    result.luaStatus = LUA_ERRFILE;
    result.reason = "file-system-denied";
    result.error = "File execution was not granted to this interpreter";
    result.memoryUsedBytes = memoryUsedBytes();
    result.peakMemoryBytes = peakMemoryBytes();
    return result;
  }

  std::string normalized = path;
  if (normalized.rfind("file://", 0) == 0) {
    normalized.erase(0, 7);
  }

  outputTruncated_.store(false, std::memory_order_relaxed);
  interruptCode_.store(0, std::memory_order_relaxed);
  lua_sethook(state_, &LuaRuntime::debugHook, LUA_MASKCOUNT, 1'000);
  const auto started = std::chrono::steady_clock::now();
  deadlineNs_.store(
      steadyNowNs() + options_.executionLimitMs * 1'000'000,
      std::memory_order_relaxed);
  const int loadStatus = luaL_loadfilex(state_, normalized.c_str(), loadMode());
  ExecutionResult result = executeLoadedChunk(loadStatus);
  deadlineNs_.store(0, std::memory_order_relaxed);
  result.durationMs = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - started)
                          .count();
  result.memoryUsedBytes = memoryUsedBytes();
  result.peakMemoryBytes = peakMemoryBytes();
  result.outputTruncated = outputWasTruncated();
  return result;
}

ExecutionResult LuaRuntime::executeLoadedChunk(int loadStatus) {
  int status = loadStatus;
  if (loadStatus == LUA_OK) {
    activeSocketState = state_;
    activeCancellation = &cancellationRequested_;
    activeDeadlineNs = &deadlineNs_;
    activeInterruptCode = &interruptCode_;
    status = lua_pcall(state_, 0, LUA_MULTRET, 0);
    activeSocketState = nullptr;
    activeCancellation = nullptr;
    activeDeadlineNs = nullptr;
    activeInterruptCode = nullptr;
  }
  return makeResult(status, 0);
}

ExecutionResult LuaRuntime::makeResult(int status, double durationMs) {
  ExecutionResult result;
  result.code = status;
  result.luaStatus = status;
  result.durationMs = durationMs;
  result.memoryUsedBytes = memoryUsedBytes();
  result.peakMemoryBytes = peakMemoryBytes();
  result.outputTruncated = outputWasTruncated();

  const int interrupt = interruptCode_.load(std::memory_order_relaxed);
  if (interrupt == kDeadlineExceeded || interrupt == kCancelled) {
    result.code = interrupt;
    result.reason = interrupt == kCancelled ? "cancelled" : "deadline";
    result.error = interrupt == kCancelled
        ? "Execution cancelled"
        : "Execution deadline exceeded";
    latestError_ = result.error;
    return result;
  }

  if (status == LUA_OK) {
    result.reason = "ok";
    latestError_.clear();
    return result;
  }

  result.error = errorAtTop(state_);
  latestError_ = result.error;
  if (status == LUA_ERRMEM) {
    result.reason = "memory-limit";
    if (result.error.empty()) {
      result.error = "Lua memory limit exceeded";
    }
  } else if (status == LUA_ERRSYNTAX) {
    result.reason = "syntax";
  } else if (status == LUA_ERRFILE) {
    result.reason = "file";
  } else {
    result.reason = "runtime";
  }
  latestError_ = result.error;
  return result;
}

void LuaRuntime::appendOutput(std::string line) {
  std::lock_guard<std::mutex> lock(outputMutex_);
  const std::size_t byteLimit = options_.maxOutputBytes;
  const std::size_t lineLimit = options_.maxOutputLines;
  if (byteLimit == 0 || lineLimit == 0 || output_.size() >= lineLimit ||
      outputBytes_ >= byteLimit) {
    outputTruncated_.store(true, std::memory_order_relaxed);
    return;
  }
  if (line.size() > byteLimit - outputBytes_) {
    line.resize(byteLimit - outputBytes_);
    outputTruncated_.store(true, std::memory_order_relaxed);
  }
  outputBytes_ += line.size();
  output_.push_back(std::move(line));
}

void LuaRuntime::requestCancellation() noexcept {
  cancellationRequested_.store(true, std::memory_order_relaxed);
}

void LuaRuntime::resetCancellation() noexcept {
  cancellationRequested_.store(false, std::memory_order_relaxed);
}

void LuaRuntime::close() noexcept {
  if (state_ != nullptr) {
    lua_sethook(state_, nullptr, 0, 0);
    lua_close(state_);
    state_ = nullptr;
  }
}

bool LuaRuntime::isOpen() const noexcept {
  return state_ != nullptr;
}

bool LuaRuntime::allowsFileSystem() const noexcept {
  return options_.allowFileSystem;
}

bool LuaRuntime::allowsBytecode() const noexcept {
  return options_.allowBytecode;
}

std::int64_t LuaRuntime::executionLimitMs() const noexcept {
  return options_.executionLimitMs;
}

void LuaRuntime::setExecutionLimitMs(std::int64_t value) noexcept {
  options_.executionLimitMs =
      std::clamp<std::int64_t>(value, 1, kMaximumExecutionLimitMs);
}

std::size_t LuaRuntime::memoryLimitBytes() const noexcept {
  return memory_.limit.load(std::memory_order_relaxed);
}

void LuaRuntime::setMemoryLimitBytes(std::size_t value) noexcept {
  options_.memoryLimitBytes = std::clamp<std::size_t>(
      value, kMinimumMemoryLimit, kMaximumMemoryLimit);
  memory_.limit.store(options_.memoryLimitBytes, std::memory_order_relaxed);
}

std::size_t LuaRuntime::memoryUsedBytes() const noexcept {
  return memory_.used.load(std::memory_order_relaxed);
}

std::size_t LuaRuntime::peakMemoryBytes() const noexcept {
  return memory_.peak.load(std::memory_order_relaxed);
}

#ifdef RNLUA_TESTING
void LuaRuntime::failAllocationsAfterForTesting(
    std::int64_t successfulAllocations) noexcept {
  memory_.failAfter.store(
      std::max<std::int64_t>(0, successfulAllocations),
      std::memory_order_relaxed);
}

void LuaRuntime::clearAllocationFailureForTesting() noexcept {
  memory_.failAfter.store(-1, std::memory_order_relaxed);
}
#endif

std::size_t LuaRuntime::maxOutputBytes() const noexcept {
  return options_.maxOutputBytes;
}

void LuaRuntime::setMaxOutputBytes(std::size_t value) noexcept {
  std::lock_guard<std::mutex> lock(outputMutex_);
  options_.maxOutputBytes = std::min(value, kMaximumOutputBytes);
  while (!output_.empty() && outputBytes_ > options_.maxOutputBytes) {
    outputBytes_ -= output_.front().size();
    output_.pop_front();
    outputTruncated_.store(true, std::memory_order_relaxed);
  }
}

std::size_t LuaRuntime::maxOutputLines() const noexcept {
  return options_.maxOutputLines;
}

void LuaRuntime::setMaxOutputLines(std::size_t value) noexcept {
  std::lock_guard<std::mutex> lock(outputMutex_);
  options_.maxOutputLines = std::min(value, kMaximumOutputLines);
  while (output_.size() > options_.maxOutputLines) {
    outputBytes_ -= output_.front().size();
    output_.pop_front();
    outputTruncated_.store(true, std::memory_order_relaxed);
  }
}

std::size_t LuaRuntime::outputCount() const {
  std::lock_guard<std::mutex> lock(outputMutex_);
  return output_.size();
}

std::string LuaRuntime::takeOutput(std::size_t count) {
  std::lock_guard<std::mutex> lock(outputMutex_);
  const std::size_t take = count == 0 ? output_.size() : std::min(count, output_.size());
  std::string result;
  for (std::size_t index = 0; index < take; ++index) {
    if (index > 0) {
      result.push_back('\n');
    }
    result.append(output_.front());
    outputBytes_ -= output_.front().size();
    output_.pop_front();
  }
  return result;
}

bool LuaRuntime::outputWasTruncated() const noexcept {
  return outputTruncated_.load(std::memory_order_relaxed);
}

std::string LuaRuntime::latestError() const {
  return latestError_;
}

void LuaRuntime::withState(const std::function<void(lua_State*)>& callback) {
  if (!isOpen()) {
    throw std::runtime_error("Interpreter is destroyed");
  }
  callback(state_);
}

void LuaRuntime::runProtectedStateOperation(
    ProtectedStateOperation operation,
    void* context,
    std::int64_t limitMs) {
  if (!isOpen() || operation == nullptr) {
    throw std::runtime_error("Interpreter is destroyed");
  }

  const int originalTop = lua_gettop(state_);
  const std::int64_t previousDeadline =
      deadlineNs_.load(std::memory_order_relaxed);
  const int previousInterrupt =
      interruptCode_.load(std::memory_order_relaxed);

  cancellationRequested_.store(false, std::memory_order_relaxed);
  interruptCode_.store(0, std::memory_order_relaxed);
  const std::int64_t boundedMs = std::clamp<std::int64_t>(
      limitMs, 1, std::min<std::int64_t>(options_.executionLimitMs, 1'000));
  deadlineNs_.store(
      steadyNowNs() + boundedMs * 1'000'000,
      std::memory_order_relaxed);
  lua_sethook(state_, &LuaRuntime::debugHook, LUA_MASKCOUNT, 1'000);
  activeSocketState = state_;
  activeCancellation = &cancellationRequested_;
  activeDeadlineNs = &deadlineNs_;
  activeInterruptCode = &interruptCode_;

  const ptrdiff_t protectedTop = savestack(state_, state_->top.p);
  const int status =
      luaD_pcall(state_, operation, context, protectedTop, 0);
  const int interrupt = interruptCode_.load(std::memory_order_relaxed);
  std::string error;
  if (status != LUA_OK && lua_gettop(state_) > 0 &&
      lua_type(state_, -1) == LUA_TSTRING) {
    std::size_t size = 0;
    const char* message = lua_tolstring(state_, -1, &size);
    if (message != nullptr) {
      error.assign(message, size);
    }
  }

  activeSocketState = nullptr;
  activeCancellation = nullptr;
  activeDeadlineNs = nullptr;
  activeInterruptCode = nullptr;
  deadlineNs_.store(previousDeadline, std::memory_order_relaxed);
  interruptCode_.store(previousInterrupt, std::memory_order_relaxed);
  lua_sethook(state_, &LuaRuntime::debugHook, LUA_MASKCOUNT, 1'000);

  if (status != LUA_OK && lua_gettop(state_) > originalTop) {
    lua_settop(state_, originalTop);
  }
  if (interrupt == kCancelled) {
    throw std::runtime_error("Lua stack operation was cancelled");
  }
  if (interrupt == kDeadlineExceeded) {
    throw std::runtime_error("Lua stack operation exceeded its deadline");
  }
  if (status == LUA_OK) {
    return;
  }
  throw std::runtime_error(
      error.empty() ? "Lua stack operation failed" : std::move(error));
}

lua_State* LuaRuntime::stateForAdvancedUse() noexcept {
  return state_;
}

const char* LuaRuntime::loadMode() const noexcept {
  return options_.allowBytecode ? nullptr : "t";
}

std::int64_t LuaRuntime::steadyNowNs() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

double socketWaitTimeout(double requestedSeconds) {
  if (activeSocketState == nullptr || activeCancellation == nullptr ||
      activeDeadlineNs == nullptr) {
    return requestedSeconds;
  }
  if (activeCancellation->load(std::memory_order_relaxed)) {
    if (activeInterruptCode != nullptr) {
      activeInterruptCode->store(kCancelled, std::memory_order_relaxed);
    }
    luaL_error(activeSocketState, "%s", kCancellationMarker);
  }
  const std::int64_t remainingNs =
      activeDeadlineNs->load(std::memory_order_relaxed) -
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();
  if (remainingNs <= 0) {
    if (activeInterruptCode != nullptr) {
      activeInterruptCode->store(kDeadlineExceeded, std::memory_order_relaxed);
    }
    luaL_error(activeSocketState, "%s", kDeadlineMarker);
  }
  const double remainingSeconds = static_cast<double>(remainingNs) / 1.0e9;
  double result = std::min(remainingSeconds, kSocketWaitSliceSeconds);
  if (requestedSeconds >= 0.0) {
    result = std::min(result, requestedSeconds);
  }
  return std::max(0.0, result);
}

namespace {

int pendingSocketInterrupt() noexcept {
  if (activeSocketState == nullptr || activeCancellation == nullptr ||
      activeDeadlineNs == nullptr) {
    return 0;
  }
  if (activeCancellation->load(std::memory_order_relaxed)) {
    if (activeInterruptCode != nullptr) {
      activeInterruptCode->store(kCancelled, std::memory_order_relaxed);
    }
    return kCancelled;
  }
  const std::int64_t deadline =
      activeDeadlineNs->load(std::memory_order_relaxed);
  if (deadline > 0) {
    const std::int64_t nowNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    if (nowNs >= deadline) {
      if (activeInterruptCode != nullptr) {
        activeInterruptCode->store(kDeadlineExceeded, std::memory_order_relaxed);
      }
      return kDeadlineExceeded;
    }
  }
  return 0;
}

#ifndef _WIN32
struct AddrInfoResolverJob {
  std::mutex mutex;
  std::condition_variable condition;
  bool done = false;
  bool abandoned = false;
  int status = EAI_FAIL;
  struct addrinfo* result = nullptr;
};
#endif

} // namespace

void checkSocketInterrupt() {
  const int interrupt = pendingSocketInterrupt();
  if (interrupt == kCancelled) {
    luaL_error(activeSocketState, "%s", kCancellationMarker);
  }
  if (interrupt == kDeadlineExceeded) {
    luaL_error(activeSocketState, "%s", kDeadlineMarker);
  }
}

#ifndef _WIN32
int socketGetAddrInfo(
    const char* node,
    const char* service,
    const struct addrinfo* hints,
    struct addrinfo** result) {
  if (result == nullptr) {
    return EAI_FAIL;
  }
  *result = nullptr;

  if (activeSocketState == nullptr || activeCancellation == nullptr ||
      activeDeadlineNs == nullptr) {
    return ::getaddrinfo(node, service, hints, result);
  }
  if (pendingSocketInterrupt() != 0) {
    return EAI_AGAIN;
  }

  const bool hasNode = node != nullptr;
  const bool hasService = service != nullptr;
  const std::string nodeCopy = hasNode ? node : "";
  const std::string serviceCopy = hasService ? service : "";
  const bool hasHints = hints != nullptr;
  struct addrinfo hintsCopy {};
  if (hasHints) {
    hintsCopy.ai_flags = hints->ai_flags;
    hintsCopy.ai_family = hints->ai_family;
    hintsCopy.ai_socktype = hints->ai_socktype;
    hintsCopy.ai_protocol = hints->ai_protocol;
  }

  auto job = std::make_shared<AddrInfoResolverJob>();
  std::thread([
      job,
      hasNode,
      hasService,
      nodeCopy,
      serviceCopy,
      hasHints,
      hintsCopy]() mutable {
    struct addrinfo* resolved = nullptr;
    const int status = ::getaddrinfo(
        hasNode ? nodeCopy.c_str() : nullptr,
        hasService ? serviceCopy.c_str() : nullptr,
        hasHints ? &hintsCopy : nullptr,
        &resolved);
    {
      std::lock_guard<std::mutex> lock(job->mutex);
      job->status = status;
      job->done = true;
      if (job->abandoned) {
        if (resolved != nullptr) {
          ::freeaddrinfo(resolved);
        }
      } else {
        job->result = resolved;
      }
    }
    job->condition.notify_all();
  }).detach();

  for (;;) {
    {
      std::unique_lock<std::mutex> lock(job->mutex);
      if (job->done) {
        const int status = job->status;
        *result = job->result;
        job->result = nullptr;
        return status;
      }
      job->condition.wait_for(lock, std::chrono::milliseconds(50));
    }

    if (pendingSocketInterrupt() != 0) {
      std::lock_guard<std::mutex> lock(job->mutex);
      job->abandoned = true;
      if (job->done && job->result != nullptr) {
        ::freeaddrinfo(job->result);
        job->result = nullptr;
      }
      return EAI_AGAIN;
    }
  }
}
#endif

} // namespace rnlua

extern "C" double rnlua_socket_wait_timeout(double requested_seconds) {
  return rnlua::socketWaitTimeout(requested_seconds);
}

extern "C" void rnlua_socket_check_interrupt(void) {
  rnlua::checkSocketInterrupt();
}

#ifndef _WIN32
extern "C" int rnlua_socket_getaddrinfo(
    const char* node,
    const char* service,
    const struct addrinfo* hints,
    struct addrinfo** result) {
  return rnlua::socketGetAddrInfo(node, service, hints, result);
}
#endif
