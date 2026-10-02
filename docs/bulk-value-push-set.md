# Bounded Lua value push/set

## API

| Method | Effect |
| --- | --- |
| `pushValue(value, limits?)` | Push one transferable value onto the Lua stack |
| `pushValues(values, limits?)` | Push several transferable values in input order |
| `setGlobal(name, value, limits?)` | Raw-set one global |
| `setGlobals(values, limits?)` | Raw-set several globals as one staged batch |

The names deliberately follow Lua's own terminology: stack values are **pushed**;
globals are **set**. These are host/library-author APIs. They do not change Lua
script syntax or require user-authored batching.

Each call performs one JSI HostFunction entry. JavaScript input is fully snapshotted
into detached native `LuaValue` data before the interpreter execution gate is
claimed. JavaScript getters/proxies may therefore run before Lua is touched. The
gate is checked again after input parsing; if a getter starts that interpreter's
worker, the push/set call rejects without touching Lua.

## Transferable values

Accepted input is the same bounded data-oriented subset used by the bulk readers:

- `null` -> Lua nil
- booleans
- finite JavaScript numbers
- valid UTF-8 strings
- JavaScript arrays
- objects with enumerable string keys

Functions, `undefined`, symbols, non-finite numbers, cycles, sparse arrays, and
other non-data values are rejected. Object keys and strings must round-trip through
UTF-8 without loss. Special names such as `__proto__`, `constructor`, and embedded
zero bytes are treated as ordinary data when they are actual own enumerable keys.

`null` follows normal Lua nil semantics. At a root it pushes nil or clears a global.
Inside a table it removes/omits that key; Lua tables cannot retain a stored nil
element, so a structure containing nested nulls is not necessarily read-back
shape-identical.

The shared limits are `maxDepth`, `maxEntries`, and `maxStringBytes`. They apply
across the whole plural call, not separately to each root. At most 256 pushed
roots/globals are accepted. `emptyTables` is only a read policy and is not a
push/set option because JavaScript already distinguishes arrays from objects.

## Stack and global semantics

`pushValue`/`pushValues` are stack-transactional. The native implementation records
the original Lua stack top before materialization. Any C++ conversion failure or
protected Lua allocation failure restores that exact top. On success, roots remain
on the stack in input order.

`setGlobal`/`setGlobals` do not leave temporary values on the Lua stack. They use
the registry global environment plus raw assignment, bypassing `_G.__newindex`.
The complete batch is staged before the first global mutation: names, prior values,
and all non-nil new values are materialized first. If assignment itself fails,
already-attempted names are restored from staged prior values before the call
returns an error.

As with bulk reads, these methods do not add waiting, threads, channels, implicit
flushes, or a new scheduler. A call rejects while that interpreter is asynchronously
executing or after destruction. Different interpreters remain independent.

## Typical host usage

```ts
const lua = luaInterpreter();

lua.pushValues([
  {ok: true, value: 42},
  'resume-token',
]);

const thread = /* previously retained coroutine handle */;
lua.resume(thread, 2);

lua.setGlobals({
  status: 'ready',
  response: {ok: true},
});
```

This is especially useful when a host needs to resume a Lua coroutine with a
structured response: it can push the response directly instead of generating a
Lua source literal and compiling/executing an assignment chunk.

## Verification

Native tests cover stack order, nested arrays/maps, special keys, raw-global
semantics, allocation-failure rollback, limits, recovery, and independent
interpreters. The Hermes/JSI suite additionally covers invalid JavaScript input,
cycles, sparse arrays, busy/destroyed interpreters, JS getter reentry, plural
transaction behavior, and round trips through the public HostObject.
