# RetroZero Lua resurrection / integration

## Package state

- Bundled runtime: Lua 5.4.9.
- Validated modern target: Expo SDK 55 / React Native 0.83.10.
- RetroZero application target: iOS 15.1.
- Package name/native module: `react-native-lua` / `SKNativeLua`.
- JavaScript creation API: `luaInterpreter(options?)` in `src/index.tsx`.
- Shared native interpreter: `cpp/LuaRuntime.*` and
  `cpp/react-native-lua.*`.
- iOS library floor: 10.0 (unchanged from the original podspec).
- No Swift is required by the library or example.

## Proven dual React Native entrypoints

### iOS

`ios/Lua.h` uses `#ifdef RCT_NEW_ARCH_ENABLED`:

- New Architecture: conforms to generated `NativeLuaSpec` and
  `RCTTurboModuleWithJSIBindings`.
- Legacy: conforms to `RCTBridgeModule`.

`ios/Lua.mm` returns `NativeLuaSpecJSI` and installs the shared factory through
`installJSIBindingsWithRuntime:callInvoker:` in New Architecture builds. Its
legacy branch obtains the RN 0.64 `RCTCxxBridge` runtime and calls the exact
same `SKRNNativeLua::install` function.

### Android

`android/build.gradle` selects one adapter source set while retaining one
package and C/C++ core:

- `src/newarch`: `LuaModule extends NativeLuaSpec` and implements RN 0.83's
  `TurboModuleWithJSIBindings`; `LuaBindingsInstaller.cpp` returns a supported
  `BindingsInstallerHolder`.
- `src/oldarch`: `LuaModule extends ReactContextBaseJavaModule` and installs
  against the Catalyst runtime for RN 0.64.
- `src/main/.../LuaPackage.java`: `TurboReactPackage` supplies both Turbo and
  legacy package registration, following the `react-native-ble-cross` shape.

Both platforms install `SKRNNativeLuaNewInterpreter`; therefore
`luaInterpreter()` is not New-Architecture-only.

Async execution no longer sends a `jsi::Function` or runtime pointer through a
`CallInvoker`. Each interpreter owns at most one joinable worker. Native code
stores plain `ExecutionResult` data, and the TypeScript wrapper polls that data
to fulfill the preserved callback API or the new structured Promise API on the
JS thread. `cancel()` is observable by the Lua hook while the worker is active;
`destroy()` requests cancellation, joins, and then closes the state.

## RetroZero / Expo SDK 55 steps

1. Add the checkout/package to the RetroZero workspace (for a sibling checkout,
   the repository-root dependency can use `file:../react-native-lua`) and run the
   workspace package manager.
2. Import `luaInterpreter` only from `react-native-lua`; no Expo config plugin is
   required because this is an autolinked native module.
3. Regenerate native projects/pods as appropriate for the current Expo workflow.
   Codegen reads `package.json#codegenConfig`, generates `NativeLuaSpec`, and
   maps `SKNativeLua` to its Objective-C++ provider.
4. Use an Expo development/EAS build, not Expo Go.
5. Keep RetroZero's app deployment target separate from the pod's reusable iOS
   10.0 floor.

The included isolated Gradle fixture can validate the package against an
existing RN 0.83 installation without editing that host:

```sh
JAVA_HOME=/path/to/jdk17 \
REACT_NATIVE_NODE_MODULES_DIR=/absolute/path/to/RetroZero/node_modules \
REACT_NATIVE_GRADLE_PLUGIN_DIR=/absolute/path/to/RetroZero/node_modules/@react-native/gradle-plugin \
REACT_NATIVE_VERSION=0.83.10 \
GRADLE_CMD=/absolute/path/to/RetroZero/apps/mobile/android/gradlew \
bash scripts/test-modern-android.sh
```

## Security invariants

- `os.execute` is absent from `cpp/lua_src/loslib.c`; there is no compiled call
  to `system()`.
- JS-created interpreters do not open `os`, `io`, `package`, or `debug`.
- File execution and bytecode require explicit interpreter options.
- Deadline, capped allocator, bounded output, cancellation, and destruction are
  implemented by `LuaRuntime` and covered by `scripts/test-native-core.sh`.
- JavaScript callbacks and JSI runtime pointers never cross the Lua worker
  boundary.

## Linux validation performed

- `scripts/test-native-core.sh`: passed, including default-library isolation,
  stripped OS capabilities, deadline, allocator cap, bounded output, multiple
  independent states, active cancellation, file denial, and destruction.
- RN 0.83.10 Android New Architecture: generated Java spec, Java compilation,
  and CMake builds passed for arm64-v8a, armeabi-v7a, x86, and x86_64.
- RN 0.83.10 Android legacy source-set path: Java compilation and the same four
  CMake ABI builds passed.
- RN 0.83.10 iOS codegen: passed; generated `NativeLuaSpecJSI`, the expected
  Promise selector, and the `SKNativeLua` module-provider mapping.
- TypeScript 5.9 strict check of `src/**` against RN 0.83.10 types: passed.
- Direct C++20 syntax check of the JSI wrapper against RN 0.83.10 headers:
  passed.

Linux cannot run CocoaPods/Xcode or the RN 0.64 simulator example. The final
Mac pass should run `pod install`, build with New Architecture enabled at the
RetroZero iOS 15.1 target, and separately launch the retained RN 0.64.3 example
on the owner's Monterey toolchain. Ruby/CocoaPods was not available here.

## ScriptWeaver boundary

ScriptWeaver belongs in RetroZero as a higher-level adapter. It should own its
parser/UX, permission grants, a curated command namespace, and host-action
dispatch. The generic package exposes `SKRNLuaInterpreter::withState(...)` so a
native adapter can register narrow Lua C functions/tables before a script runs.

Recommended shape:

1. Translate ScriptWeaver syntax to ordinary Lua or drive a restricted Lua
   prelude.
2. Create an interpreter with conservative deadline/memory/output limits.
3. Inject only explicitly granted actions under a namespace such as `retro`.
4. Validate arguments and marshal requests to RetroZero services in the
   adapter; never expose arbitrary `NativeModules`, shell, dynamic loaders, or
   unrestricted filesystem/network access.
5. Return structured output/errors to ScriptWeaver UI while keeping the raw
   `luaInterpreter()` API available to trusted advanced packages.

No package was pushed or published during this work.
