# Bounded bulk readers

## API

| Method | Input | Output |
| --- | --- | --- |
| `readValues(indices, options?)` | Actual stack indices; positive or negative | Array/tuple in the requested order |
| `readGlobals(names, options?)` | Literal names in the registry global environment | Null-prototype map of requested names |
| `readValue(index, options?)` | One actual stack index | One `LuaValue` |
| `readGlobal(name, options?)` | One literal global name | One `LuaValue` |

Each call is synchronous and performs one protected native snapshot. `readValues`
resolves all indices against the stack at snapshot entry, before temporary pushes.
It neither consumes nor reorders entries. Zero, out-of-range and pseudo-indices
(including the registry index) are rejected. Missing global names return `null`;
invalid stack indices are errors, not missing values. Names are literal strings,
not dotted paths; embedded zero bytes in valid UTF-8 names are preserved.

The TypeScript type describes an interoperable data subset, not every Lua type
and not your application schema. Tuple length and literal global-name keys can
be inferred from `as const` inputs. Narrow/validate values before treating them
as graphics commands, database rows, device data, etc.

## Conversion and limits

Maps contain only own data properties and have null prototypes. Use
`Object.keys`/`Object.hasOwn` (or `Object.prototype.hasOwnProperty.call` on older
engines), not inherited object methods. Arrays retain normal array behavior.
Snapshots are mutable JS data at runtime, but exposed as readonly in TypeScript;
no deep freezing is performed. Changes never write back to Lua. Alias identity
is deliberately not preserved, including duplicate requested roots.

Dense integer keys `1..n` produce arrays; string-only keys produce maps. All raw
keys are inspected, not just `rawlen`, so mixed or sparse tables cannot silently
lose entries. Empty tables follow `emptyTables`, uniformly throughout the call.
Metatables are ignored: no `__index`, `__pairs`, `__len`, `__eq` or conversion
metamethod is invoked by the reader. Functions, threads, userdata, lightuserdata
and cycles are not part of `LuaValue`. Invalid UTF-8 strings/keys and non-finite
numbers fail. Lua integer-subtype values outside ±(2^53−1) fail; floating-point
values remain finite JS doubles. This API is not a binary-string transport.

`maxDepth` counts nested tables (root table depth 1). `maxEntries` counts total
table entries traversed across all roots, counting shared tables each time they
are copied. `maxStringBytes` counts aggregate UTF-8 bytes in requested global
names, copied keys and strings. Budgets are shared across a plural call, so a
caller cannot multiply limits by passing many roots. At most 256 roots are allowed.
The fixed recursion/entry/byte ceilings bound both the native temporary snapshot
and output construction; they do not represent an exact JS heap-size limit.

## Concurrency and failures

The HostObject claims its existing execution gate for the entire native snapshot.
Reads reject while that interpreter is asynchronously executing, and after
destruction. No automatic waiting, flushing, batch, channel or thread is added.
Different interpreters own independent snapshots and budgets. A suspended Lua
coroutine may be read while the interpreter is idle; the caller decides which
yields represent a completed application turn. Reading is not a transaction over
external effects, and it does not consume an outbox.

The existing application scheduler still must serialize an `execute → read`
pair. The bulk API prevents concurrent native state access; it cannot prevent an
application from scheduling a newer turn before asking for an older turn's output.
Raw `lua_State*` integrations continue to require external ownership/serialization.

JavaScript argument/options accessors run before claiming the Lua execution gate.
The gate is rechecked after argument parsing. JavaScript output construction runs
after the stack has been restored and the native snapshot detached. Consequently
even reentrant JS cannot mutate the snapshot being converted. C++ conversion
errors and protected Lua allocation errors restore the stack and release the gate;
partial results are never returned. No JSI handles cross a native worker thread.

## Verification

`npm run test:native` covers real vendored Lua traversal, malformed inputs, shared
budgets, allocation failure, recovery, and parallel reads of independent states.
Use `RNLUA_SANITIZE=1 npm run test:native` for Address/UndefinedBehavior sanitizers.

`scripts/test-jsi-android.sh` runs the actual HostObject in Hermes without building
or installing an app, starting Metro, or changing the device's UI. Supply an
explicit `ANDROID_SERIAL`, `ANDROID_NDK_HOME`, and matching cached release AARs
via `RNLUA_REACT_AAR`, `RNLUA_HERMES_AAR`, and `RNLUA_FBJNI_AAR`. It tests plural and
singular methods, stack restoration, coroutine turns, busy/independent workers,
JS reentrancy, prototype keys and an equivalent-payload extraction benchmark.
For React Native versions requiring C++20, set `RNLUA_CXX_STANDARD=20`.
Temporary native binaries/libraries under `/data/local/tmp` are removed on exit.
This benchmark measures extraction only, not application frame rate or GPU work.
