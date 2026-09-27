# react-native-lua

An embedded Lua 5.4.9 runtime with LuaSocket for React Native 0.73.6 and newer.
The same package supports the RN 0.73 legacy architecture and RN 0.83's New
Architecture TurboModule/JSI installation path.

The public interpreter is a JSI HostObject in both architectures. Only the
native binding installation changes; interpreter creation, Lua, LuaSocket, and
the resource controls are shared.

## Installation

```sh
pnpm add react-native-lua
```

Run `pod install` for iOS and rebuild the native app. Expo apps require a
development/native build; Expo Go cannot load custom native code. Supported
hosts are React Native >= 0.73.6, and the pod's iOS deployment floor is 13.4.

## Usage

```ts
import {luaInterpreter, LUA_ERROR_CODE} from 'react-native-lua';

const lua = luaInterpreter({
  executionLimitMs: 2_000,
  memoryLimitBytes: 8 * 1024 * 1024,
  maxOutputBytes: 16 * 1024,
  maxOutputLines: 256,
  allowNetwork: true,
});

const status = lua.dostring(`
  local socket = require('socket')
  print(_VERSION, socket._VERSION)
`);

if (status === 0) {
  console.log(lua.getPrint());
} else if (status === LUA_ERROR_CODE.LUA_DEADLINE_EXCEEDED) {
  console.warn('script exceeded its deadline');
} else {
  console.warn(lua.getLatestError());
}
```

`dostringasync` and `dofileasync` retain their callback signatures. Lua runs on
one owned native worker per interpreter, while JavaScript polls plain native
result data. No JSI callback or runtime pointer crosses the worker boundary.
New code can use `executeStringAsync` / `executeFileAsync` for a structured
Promise result. Call `cancel()` to stop active work or `destroy()` to cancel,
join, and close deterministically.

The HostObject also retains low-level Lua stack operations for advanced native
and JavaScript callers (`push*`, `getglobal`, `settable`, `type`, and related
methods).

## LuaSocket and raw networking

LuaSocket is compiled into every platform binary and enabled by default. These
in-memory modules are available:

- `socket`, `socket.core`, `socket.http`, `socket.ftp`, `socket.smtp`,
  `socket.url`, `socket.headers`, and `socket.tp`;
- `mime`, `mime.core`, `ltn12`, and `mbox`;
- TCP, UDP, `socket.select`, and Unix stream/datagram sockets.

This is intentional raw socket access, not a wrapper around React Native's
networking API. A script can connect, listen, send UDP datagrams, or access a
Unix socket with the host app's OS privileges. No TLS implementation is
bundled, so `socket.http` handles plain HTTP; HTTPS requires a separately
provided LuaSec-compatible module.

Set `allowNetwork: false` when creating an interpreter to omit LuaSocket and
its private `require` loader. A higher-level policy layer should use that
option and expose separately approved network actions rather than run untrusted
scripts with generic sockets.

When networking is enabled, `require` can resolve only compiled-in/preloaded
modules. The global `package` table stays hidden, and filesystem and native
dynamic module searchers are removed. Socket waits and `socket.sleep` are
sliced so cancellation remains prompt and the interpreter deadline is
enforced. Blocking OS DNS resolution runs on a detached helper that owns copied
resolver inputs and its result; the Lua execution thread polls cancellation and
the deadline and may return before that helper finishes. Large UDP/Unix-datagram
receive buffers use Lua's capped allocator.

## Security and resource boundaries

New interpreters expose Lua's base, coroutine, table, string, math, and UTF-8
libraries. `io`, `os`, `package`, and `debug` are not ambient globals.
The native `dofile` / `executeFileResult` interpreter methods are denied unless
`allowFileSystem` is true. The Lua global `dofile` remains unavailable even
when file execution is enabled. Bytecode is rejected unless `allowBytecode`
is true.

The bundled Lua source removes `os.execute` and the other process/environment/
filesystem-mutating OS functions. They remain absent even if a native
integrator explicitly opens the reduced OS library.

Each interpreter has:

- a monotonic execution deadline checked by a Lua instruction hook and socket
  wait integration;
- a capped Lua allocator (32 MiB by default, 512 KiB minimum);
- bounded captured output (64 KiB and 1,000 lines by default);
- explicit cancellation and destruction;
- text-only loading by default.

Run the native security, resource, and loopback networking suite with:

```sh
pnpm test:native
```

## Curated capability injection

The generic engine intentionally contains no application-specific command
namespace. A higher-level native adapter can construct `SKRNLuaInterpreter`,
set `allowNetwork: false`, and call `withState(...)` before exposure to install
only explicitly granted Lua C functions/tables.

Keep policy, permission prompts, command schemas, and host-action dispatch in
that adapter. Do not expose arbitrary `NativeModules`, process APIs, dynamic
loaders, or raw filesystem/network access to untrusted scripts.

## RN 0.73.6 old-architecture example

`example/` is a real RN 0.73.6 app with `newArchEnabled=false`. It autolinks
this checkout, creates the actual HostObject, loads LuaSocket helpers, runs Lua
coroutines, and verifies that unsafe ambient libraries remain unavailable.

The example has its own RN 0.73 dependency tree, but this workspace also
installs the root RN 0.83 development toolchain. Use Node 20.19.4 or newer for
the root-level pnpm commands below (Node 22 is also supported):

```sh
pnpm install
pnpm pods
pnpm example:ios
# or: pnpm example:android
```

## License

MIT. The bundled LuaSocket sources are also MIT licensed and retain their
upstream copyright notices.
