#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

struct lua_State;
struct lua_Debug;

namespace rnlua {

constexpr int kDeadlineExceeded = 1001;
constexpr int kCancelled = 1002;
constexpr int kDestroyed = 1003;
constexpr int kFileSystemDenied = 1004;

struct InterpreterOptions {
  std::int64_t executionLimitMs = 10'000;
  std::size_t memoryLimitBytes = 32 * 1024 * 1024;
  std::size_t maxOutputBytes = 64 * 1024;
  std::size_t maxOutputLines = 1'000;
  bool allowFileSystem = false;
  bool allowBytecode = false;
  bool allowNetwork = true;
};

struct ExecutionResult {
  int code = 0;
  int luaStatus = 0;
  std::string reason = "ok";
  std::string error;
  double durationMs = 0;
  std::size_t memoryUsedBytes = 0;
  std::size_t peakMemoryBytes = 0;
  bool outputTruncated = false;
};

/**
 * A single, thread-confined Lua 5.4 runtime.
 *
 * React Native integration serializes access to this object. Advanced native
 * integrations may use withState() to install C functions, but must do so
 * before handing the interpreter to another thread.
 */
class LuaRuntime final {
 public:
  explicit LuaRuntime(InterpreterOptions options = {});
  ~LuaRuntime();

  LuaRuntime(const LuaRuntime&) = delete;
  LuaRuntime& operator=(const LuaRuntime&) = delete;

  ExecutionResult executeString(const std::string& source);
  ExecutionResult executeFile(const std::string& path);

  void requestCancellation() noexcept;
  void resetCancellation() noexcept;
  void close() noexcept;

  bool isOpen() const noexcept;
  bool allowsFileSystem() const noexcept;
  bool allowsBytecode() const noexcept;

  std::int64_t executionLimitMs() const noexcept;
  void setExecutionLimitMs(std::int64_t value) noexcept;
  std::size_t memoryLimitBytes() const noexcept;
  void setMemoryLimitBytes(std::size_t value) noexcept;
  std::size_t memoryUsedBytes() const noexcept;
  std::size_t peakMemoryBytes() const noexcept;
  std::size_t maxOutputBytes() const noexcept;
  void setMaxOutputBytes(std::size_t value) noexcept;
  std::size_t maxOutputLines() const noexcept;
  void setMaxOutputLines(std::size_t value) noexcept;

  std::size_t outputCount() const;
  std::string takeOutput(std::size_t count = 0);
  bool outputWasTruncated() const noexcept;
  std::string latestError() const;

  /**
   * Deliberately low-level native escape hatch for curated capability
   * injection. The callback runs synchronously on the interpreter's owning
   * thread. It must not retain the lua_State pointer.
   */
  void withState(const std::function<void(lua_State*)>& callback);
  lua_State* stateForAdvancedUse() noexcept;

 private:
  struct MemoryState {
    std::atomic<std::size_t> used{0};
    std::atomic<std::size_t> peak{0};
    std::atomic<std::size_t> limit{0};
  };

  static void* allocate(void* userData, void* pointer, std::size_t oldSize,
                        std::size_t newSize) noexcept;
  static void debugHook(lua_State* state, lua_Debug* debugRecord);
  static int print(lua_State* state);
  static int textOnlyLoad(lua_State* state);

  void openLibraries();
  void appendOutput(std::string line);
  ExecutionResult executeLoadedChunk(int loadStatus);
  ExecutionResult makeResult(int status, double durationMs);
  const char* loadMode() const noexcept;
  static std::int64_t steadyNowNs() noexcept;

  InterpreterOptions options_;
  MemoryState memory_;
  lua_State* state_ = nullptr;
  std::atomic<bool> cancellationRequested_{false};
  std::atomic<std::int64_t> deadlineNs_{0};
  std::atomic<bool> outputTruncated_{false};
  mutable std::mutex outputMutex_;
  std::deque<std::string> output_;
  std::size_t outputBytes_ = 0;
  std::string latestError_;
};

} // namespace rnlua
