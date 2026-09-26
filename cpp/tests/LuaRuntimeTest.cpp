#include "../LuaRuntime.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

extern "C" {
#include "../lua_src/lauxlib.h"
#include "../lua_src/lua.h"
#include "../lua_src/lualib.h"
}

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

} // namespace

int main() {
  {
    rnlua::LuaRuntime runtime;
    const auto result = runtime.executeString(
        "assert(os == nil); assert(io == nil); assert(package == nil); "
        "assert(debug == nil); assert(type(require) == 'function'); "
        "local ok = pcall(require, 'not.bundled'); assert(not ok)");
    require(result.code == 0, "unsafe standard libraries must be absent");
  }

  {
    rnlua::LuaRuntime runtime;
    const auto result = runtime.executeString(R"lua(
      local socket = require('socket')
      assert(type(socket.tcp) == 'function')
      assert(type(socket.udp) == 'function')
      assert(type(require('socket.core')) == 'table')
      assert(type(require('mime.core')) == 'table')
      assert(type(require('mime')) == 'table')
      assert(type(require('ltn12')) == 'table')
      assert(type(require('socket.http').request) == 'function')
      assert(type(require('socket.ftp').get) == 'function')
      assert(type(require('socket.smtp').send) == 'function')
      assert(type(require('socket.unix').stream) == 'function')

      local server = assert(socket.bind('127.0.0.1', 0))
      server:settimeout(1)
      local address, port = assert(server:getsockname())
      local client = assert(socket.tcp())
      client:settimeout(1)
      assert(client:connect(address, port))
      local peer = assert(server:accept())
      peer:settimeout(1)
      assert(client:send('ping'))
      assert(peer:receive(4) == 'ping')
      peer:close(); client:close(); server:close()

      local receiver = assert(socket.udp())
      assert(receiver:setsockname('127.0.0.1', 0))
      receiver:settimeout(1)
      local udpAddress, udpPort = assert(receiver:getsockname())
      local sender = assert(socket.udp())
      assert(sender:sendto('pong', udpAddress, udpPort))
      assert(receiver:receive() == 'pong')
      sender:close(); receiver:close()
    )lua");
    require(result.code == 0,
            "bundled LuaSocket modules and loopback TCP/UDP must work: " +
                result.error);
  }

  {
    const std::string suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string streamPath = "/tmp/rnlua-stream-" + suffix + ".sock";
    const std::string datagramPath = "/tmp/rnlua-dgram-" + suffix + ".sock";
    std::remove(streamPath.c_str());
    std::remove(datagramPath.c_str());

    rnlua::LuaRuntime runtime;
    runtime.withState([&](lua_State* state) {
      lua_pushlstring(state, streamPath.data(), streamPath.size());
      lua_setglobal(state, "UNIX_STREAM_PATH");
      lua_pushlstring(state, datagramPath.data(), datagramPath.size());
      lua_setglobal(state, "UNIX_DATAGRAM_PATH");
    });
    const auto result = runtime.executeString(R"lua(
      local unix = require('socket.unix')

      local server = assert(unix.stream())
      assert(server:bind(UNIX_STREAM_PATH))
      assert(server:listen(1))
      server:settimeout(1)
      local client = assert(unix.stream())
      client:settimeout(1)
      assert(client:connect(UNIX_STREAM_PATH))
      local peer = assert(server:accept())
      peer:settimeout(1)
      assert(client:send('unix-stream'))
      assert(peer:receive(11) == 'unix-stream')
      peer:close(); client:close(); server:close()

      local receiver = assert(unix.dgram())
      assert(receiver:setsockname(UNIX_DATAGRAM_PATH))
      receiver:settimeout(1)
      local sender = assert(unix.dgram())
      assert(sender:sendto('unix-dgram', UNIX_DATAGRAM_PATH))
      assert(receiver:receive() == 'unix-dgram')
      sender:close(); receiver:close()
    )lua");
    std::remove(streamPath.c_str());
    std::remove(datagramPath.c_str());
    require(result.code == 0,
            "Unix stream/datagram loopback must work: " + result.error);
  }

  {
    rnlua::InterpreterOptions options;
    options.allowNetwork = false;
    rnlua::LuaRuntime runtime(options);
    const auto result = runtime.executeString(
        "assert(require == nil); assert(package == nil)");
    require(result.code == 0, "network-free states must not expose LuaSocket");
  }

  {
    rnlua::LuaRuntime runtime;
    runtime.withState([](lua_State* state) {
      luaL_requiref(state, LUA_OSLIBNAME, luaopen_os, 1);
      lua_pop(state, 1);
    });
    const auto result = runtime.executeString(
        "assert(os.execute == nil); assert(os.exit == nil); "
        "assert(os.getenv == nil); assert(os.remove == nil); "
        "assert(os.rename == nil); assert(os.setlocale == nil); "
        "assert(os.tmpname == nil); assert(type(os.time) == 'function')");
    require(result.code == 0,
            "ambient native OS capabilities must be absent when os is explicitly opened");
  }

  {
    rnlua::InterpreterOptions options;
    options.executionLimitMs = 20;
    rnlua::LuaRuntime runtime(options);
    const auto result = runtime.executeString("while true do end");
    require(result.code == rnlua::kDeadlineExceeded,
            "infinite loops must hit the execution deadline");
  }

  {
    rnlua::InterpreterOptions options;
    options.executionLimitMs = 20;
    rnlua::LuaRuntime runtime(options);
    const auto result = runtime.executeString(
        "require('socket').sleep(2)");
    require(result.code == rnlua::kDeadlineExceeded,
            "blocking socket waits must share the execution deadline");
  }

  {
    rnlua::InterpreterOptions options;
    options.memoryLimitBytes = 512 * 1024;
    rnlua::LuaRuntime runtime(options);
    const auto result = runtime.executeString(
        "local values = {}; while true do values[#values + 1] = "
        "string.rep('x', 8192) end");
    require(result.reason == "memory-limit", "allocations must hit the memory cap");
  }

  {
    rnlua::InterpreterOptions options;
    options.executionLimitMs = 10'000;
    rnlua::LuaRuntime runtime(options);
    rnlua::ExecutionResult result;
    const auto started = std::chrono::steady_clock::now();
    std::thread worker([&] {
      result = runtime.executeString("require('socket').sleep(10)");
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    runtime.requestCancellation();
    worker.join();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    require(result.code == rnlua::kCancelled,
            "cancellation must interrupt a blocking socket wait");
    require(elapsed.count() < 500,
            "blocking socket cancellation must remain prompt");
  }

  {
    rnlua::InterpreterOptions options;
    options.maxOutputBytes = 8;
    options.maxOutputLines = 2;
    rnlua::LuaRuntime runtime(options);
    const auto result = runtime.executeString("print('abcd'); print('efgh'); print('x')");
    require(result.code == 0, "bounded print test must execute");
    require(result.outputTruncated, "overflowing output must report truncation");
    require(runtime.outputCount() == 2, "output line cap must be enforced");
    require(runtime.takeOutput() == "abcd\nefgh", "captured output must remain readable");
  }

  {
    rnlua::LuaRuntime first;
    rnlua::LuaRuntime second;
    require(first.executeString("value = 11").code == 0,
            "first independent state must execute");
    require(second.executeString("assert(value == nil); value = 22").code == 0,
            "second state must not observe the first state");
    require(first.executeString("assert(value == 11)").code == 0,
            "first state must retain its own globals");
  }

  {
    rnlua::InterpreterOptions options;
    options.executionLimitMs = 10'000;
    rnlua::LuaRuntime runtime(options);
    rnlua::ExecutionResult result;
    std::thread worker([&] {
      result = runtime.executeString("while true do end");
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    runtime.requestCancellation();
    worker.join();
    require(result.code == rnlua::kCancelled,
            "cross-thread cancellation must stop a running interpreter");
  }

  {
    rnlua::LuaRuntime runtime;
    const auto denied = runtime.executeFile("/tmp/react-native-lua-denied.lua");
    require(denied.code == rnlua::kFileSystemDenied,
            "file execution must require an explicit capability");
    runtime.close();
    require(runtime.executeString("return 1").code == rnlua::kDestroyed,
            "closed interpreters must reject later execution cleanly");
  }

  std::cout << "LuaRuntime security/resource tests passed\n";
  return 0;
}
