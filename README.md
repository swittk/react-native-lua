# react-native-lua

An embedded Lua 5.4.9 runtime for React Native. One package supports both the
legacy bridge used by the RN 0.64 example and generated TurboModules/JSI binding
installation in RN 0.83 New Architecture apps.

The public interpreter is a JSI HostObject in both cases. Architecture only
changes how `SKRNNativeLuaNewInterpreter` is installed into the JavaScript
runtime; interpreter creation and the Lua C/C++ core are shared.

## Installation

```sh
yarn add react-native-lua
```

Run `pod install` for iOS and rebuild the native app. Expo apps require a
development/native build; Expo Go cannot load custom native code.

The pod keeps the package's original iOS 10 deployment floor. A consuming
React Native or Expo app can choose its own higher deployment target.

## Usage

```ts
import {luaInterpreter, LUA_ERROR_CODE} from 'react-native-lua';

const lua = luaInterpreter({
  executionLimitMs: 2_000,
  memoryLimitBytes: 8 * 1024 * 1024,
  maxOutputBytes: 16 * 1024,
  maxOutputLines: 256,
});

const status = lua.dostring(`
  local total = 0
  for i = 1, 10 do total = total + i end
  print(total)
`);

if (status === 0) {
  console.log(lua.getPrint()); // 55
} else if (status === LUA_ERROR_CODE.LUA_DEADLINE_EXCEEDED) {
  console.warn('script exceeded its deadline');
} else {
  console.warn(lua.getLatestError());
}
```

`dostringasync` and `dofileasync` keep their existing callback signatures, but
Lua now runs on one owned native worker per interpreter. Native code publishes
plain result data; a small JavaScript poll delivers the callback on the JS
thread, so no JSI callback or runtime pointer crosses threads. New code can use
`executeStringAsync` / `executeFileAsync` for a structured Promise result.
Call `cancel()` to stop an active worker or `destroy()` to cancel, join, and
close it deterministically.

The HostObject also retains the low-level Lua stack operations for advanced
callers (`push*`, `getglobal`, `settable`, `type`, and related methods).

## Security and resource boundaries

New interpreters default to an allowlist of Lua's base, coroutine, table,
string, math, and UTF-8 libraries. `io`, `os`, `package`, and `debug` are not
ambient globals. `dofile` is denied unless `allowFileSystem` is explicitly set,
and bytecode is rejected unless `allowBytecode` is explicitly set.

The bundled Lua source also removes `os.execute` from `loslib.c`. It remains
absent even if a native integrator explicitly opens the OS library.

Each interpreter has:

- a monotonic execution deadline checked by a Lua instruction hook;
- a capped Lua allocator (32 MiB by default, 512 KiB minimum);
- bounded captured output (64 KiB and 1,000 lines by default);
- explicit cancellation and destruction;
- text-only loading by default.

Run the native security/resource regression test with:

```sh
yarn test:native
```

## Curated capability injection

The generic engine intentionally contains no ScriptWeaver or RetroZero command
namespace. A higher-level native adapter can construct `SKRNLuaInterpreter`
and call `withState(...)` before exposing it, registering only specific Lua C
functions/tables that the user granted. For example, a ScriptWeaver adapter
might inject `retro.notify()` and `retro.readSetting()` while omitting network,
filesystem, process, and arbitrary native-module access.

Keep policy, permission prompts, command schemas, and host-action dispatch in
that adapter. Do not enable all Lua OS/package libraries as a substitute for
capability injection.

## Legacy Monterey example

`example/` remains a real RN 0.64.3 app and links this package through CocoaPods
and Gradle. It creates the actual HostObject, runs Lua coroutines, prints the
Lua version and verifies that the ambient `os` library is unavailable.

Use the older Node/Yarn/Xcode toolchain appropriate for RN 0.64 on Monterey:

```sh
yarn
yarn example
yarn pods
yarn example ios
```

## License

MIT
