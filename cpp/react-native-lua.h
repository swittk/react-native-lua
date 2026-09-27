#ifndef REACT_NATIVE_LUA_H
#define REACT_NATIVE_LUA_H

#include "LuaRuntime.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <jsi/jsi.h>

namespace facebook {
namespace react {
class CallInvoker;
}
} // namespace facebook

namespace SKRNNativeLua {

int multiply(float a, float b);

/**
 * JSI HostObject that owns one isolated Lua state. The same class is installed
 * by the legacy bridge adapters and the New Architecture binding installers.
 */
class SKRNLuaInterpreter final
    : public facebook::jsi::HostObject,
      public std::enable_shared_from_this<SKRNLuaInterpreter> {
 public:
  SKRNLuaInterpreter(
      std::shared_ptr<facebook::react::CallInvoker> callInvoker,
      rnlua::InterpreterOptions options = {});
  ~SKRNLuaInterpreter() override;

  int doString(const std::string& source);
  int doFile(const std::string& path);
  std::string getLatestError() const;

  /** Installs an explicitly granted native capability before script execution. */
  void withState(const std::function<void(lua_State*)>& callback);

  facebook::jsi::Value get(
      facebook::jsi::Runtime& runtime,
      const facebook::jsi::PropNameID& name) override;
  std::vector<facebook::jsi::PropNameID> getPropertyNames(
      facebook::jsi::Runtime& runtime) override;

 private:
  struct AsyncResult {
    std::uint64_t taskId;
    rnlua::ExecutionResult result;
  };

  std::uint64_t startAsync(const std::string& source, bool isFile);
  bool takeAsyncResult(std::uint64_t taskId, rnlua::ExecutionResult& result);
  void joinCompletedWorker();
  void shutdown() noexcept;
  void runProtectedStateOperation(
      facebook::jsi::Runtime& runtime,
      rnlua::LuaRuntime::ProtectedStateOperation operation,
      void* context);


  std::unique_ptr<rnlua::LuaRuntime> lua_;
  lua_State* state_ = nullptr;
  std::atomic<bool> executing_{false};
  std::atomic<bool> destroyed_{false};
  std::atomic<std::uint64_t> nextTaskId_{1};
  std::thread worker_;
  std::mutex resultMutex_;
  std::deque<AsyncResult> asyncResults_;
};

/** Installs the interpreter factory into the supplied React Native runtime. */
void install(
    facebook::jsi::Runtime& runtime,
    std::shared_ptr<facebook::react::CallInvoker> callInvoker);

/** Removes the interpreter factory during legacy bridge invalidation. */
void cleanup(facebook::jsi::Runtime& runtime);

} // namespace SKRNNativeLua

#endif // REACT_NATIVE_LUA_H
