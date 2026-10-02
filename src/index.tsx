import {Platform} from 'react-native';

import NativeLua from './NativeLua';

const LINKING_ERROR =
  `The package 'react-native-lua' does not appear to be linked.\n\n` +
  Platform.select({ios: "Run 'pod install' and rebuild the app.\n", default: ''}) +
  'A custom native build is required; Expo Go cannot load this package.';

type LuaFactory = (options?: LuaInterpreterOptions) => NativeLuaInterpreter;

declare global {
  // Installed by either the legacy bridge adapter or New Architecture JSI hook.
  // eslint-disable-next-line no-var
  var SKRNNativeLuaNewInterpreter: LuaFactory | undefined;
}

/** Per-interpreter resource and native capability options. */
export interface LuaInterpreterOptions {
  executionLimitMs?: number;
  memoryLimitBytes?: number;
  maxOutputBytes?: number;
  maxOutputLines?: number;
  allowFileSystem?: boolean;
  allowBytecode?: boolean;
  /** LuaSocket is available by default; set false for a network-free state. */
  allowNetwork?: boolean;
}

/** Lua value tags from lua.h. */
export enum LUA_TYPE {
  LUA_TNIL = 0,
  LUA_TBOOLEAN = 1,
  LUA_TLIGHTUSERDATA = 2,
  LUA_TNUMBER = 3,
  LUA_TSTRING = 4,
  LUA_TTABLE = 5,
  LUA_TFUNCTION = 6,
  LUA_TUSERDATA = 7,
  LUA_TTHREAD = 8,
}

/** Stable native result codes added by the bounded runtime wrapper. */
export enum LUA_ERROR_CODE {
  LUA_CRASHED_INTERPRETER = 999,
  LUA_DEADLINE_EXCEEDED = 1001,
  LUA_CANCELLED = 1002,
  LUA_DESTROYED = 1003,
  LUA_FILE_SYSTEM_DENIED = 1004,
}

export interface LuaResumeResult {
  result: number;
  nresults: number;
}

export interface LuaExecutionResult {
  code: number;
  luaStatus: number;
  reason:
    | 'ok'
    | 'deadline'
    | 'cancelled'
    | 'destroyed'
    | 'memory-limit'
    | 'file-system-denied'
    | 'syntax'
    | 'file'
    | 'runtime';
  error: string;
  durationMs: number;
  memoryUsedBytes: number;
  peakMemoryBytes: number;
  outputTruncated: boolean;
}

/** Detached interoperable snapshot, not a handle to an arbitrary Lua value.
 * Maps have null prototypes; object/array contents are not recursively frozen.
 */
export type LuaValue =
  | null
  | boolean
  | number
  | string
  | readonly LuaValue[]
  | {readonly [key: string]: LuaValue};

/** Shared bounds for one complete Lua-value transfer operation. */
export interface LuaValueLimits {
  /** Maximum table/object nesting. Default 32; range 0..64. */
  maxDepth?: number;
  /** Total table/object entries visited. Default 16384; max 262144. */
  maxEntries?: number;
  /** Total UTF-8 bytes in names, keys and string values. Default 1 MiB; max 16 MiB. */
  maxStringBytes?: number;
}

/** Read-only policy layered on top of the shared Lua-value bounds. */
export interface LuaReadOptions extends LuaValueLimits {
  /** Lua has no distinct empty-array type. Default 'object'. */
  emptyTables?: 'object' | 'array';
}

/** Native JSI HostObject for one isolated Lua 5.4 interpreter. */
interface NativeLuaInterpreter {
  dostring(source: string): number;
  dofile(path: string): number;
  executeStringResult(source: string): LuaExecutionResult;
  executeFileResult(path: string): LuaExecutionResult;
  startStringAsync(source: string): number;
  startFileAsync(path: string): number;
  takeAsyncResult(taskId: number): LuaExecutionResult | null;

  /** One protected native snapshot, without consuming/reordering stack entries.
   * Indices refer to the stack at snapshot entry; pseudo-indices are rejected.
   */
  readValues<Indices extends readonly number[]>(
    indices: Indices,
    options?: LuaReadOptions
  ): {readonly [K in keyof Indices]: LuaValue};
  /** Raw global lookup, ignoring _G.__index. Missing names return null.
   * At most 256 roots; rejects executing/destroyed interpreters.
   */
  readGlobals<Names extends readonly string[]>(
    names: Names,
    options?: LuaReadOptions
  ): {readonly [K in Names[number]]: LuaValue};
  readValue(index: number, options?: LuaReadOptions): LuaValue;
  readGlobal(name: string, options?: LuaReadOptions): LuaValue;

  /** Push detached data values onto this interpreter stack using one native transfer. */
  pushValue(value: LuaValue, limits?: LuaValueLimits): void;
  pushValues(values: readonly LuaValue[], limits?: LuaValueLimits): void;
  /** Raw global assignment, bypassing _G.__newindex. Bulk sets are transactional. */
  setGlobal(name: string, value: LuaValue, limits?: LuaValueLimits): void;
  setGlobals(values: Readonly<Record<string, LuaValue>>, limits?: LuaValueLimits): void;

  readonly printCount: number;
  getPrint(count?: number): string;
  getLatestError(): string;

  pop(count: number): void;
  pushboolean(value: number): void;
  pushglobaltable(): void;
  pushinteger(value: number): void;
  pushnil(): void;
  pushnumber(value: number): void;
  pushstring(value: string): void;
  pushthread(): number;
  pushvalue(index: number): void;
  rawequal(first: number, second: number): number;
  rawget(index: number): number;
  rawgeti(index: number, key: number): number;
  rawlen(index: number): number;
  rawset(index: number): void;
  rawseti(index: number, key: number): void;
  remove(index: number): void;
  insert(index: number): void;
  replace(index: number): void;
  resetthread(): number;
  resume(threadHandle: number, argumentCount: number): LuaResumeResult;
  rotate(index: number, amount: number): void;
  setfield(index: number, key: string): void;
  setglobal(name: string): void;
  seti(index: number, key: number): void;
  setiuservalue(index: number, slot: number): number;
  setmetatable(index: number): number;
  settable(index: number): void;
  settop(index: number): void;
  gettop(): number;
  status(): number;
  stringtonumber(value: string): number;
  gettable(index: number): number;
  getglobal(name: string): void;
  var_asnumber(name: string): number;
  toboolean(index: number): number;
  toclose(index: number): void;
  tointeger(index: number): number;
  tonumber(index: number): number;
  tostring(index: number): string | null;
  topointer(index: number): number;
  /** Returns an opaque, registry-backed handle suitable for resume(). */
  tothread(index: number): number;
  type(index?: number): LUA_TYPE;
  typename(type: number): string;
  /** Direct yielding cannot safely cross a HostFunction boundary. */
  yield(resultCount: number): never;

  readonly valid: boolean;
  readonly executing: boolean;
  readonly executionLimit: number;
  setExecutionLimit(ms: number): void;
  readonly memoryLimitBytes: number;
  setMemoryLimitBytes(bytes: number): void;
  readonly memoryUsedBytes: number;
  readonly peakMemoryBytes: number;
  readonly maxOutputBytes: number;
  setMaxOutputBytes(bytes: number): void;
  readonly maxOutputLines: number;
  setMaxOutputLines(lines: number): void;
  readonly outputTruncated: boolean;
  cancel(): void;
  destroy(): void;
}

/** Public API: legacy callbacks plus structured Promise-based async execution. */
export interface LuaInterpreter extends NativeLuaInterpreter {
  dostringasync(source: string, callback: (code: number) => void): void;
  dofileasync(path: string, callback: (code: number) => void): void;
  executeStringAsync(source: string): Promise<LuaExecutionResult>;
  executeFileAsync(path: string): Promise<LuaExecutionResult>;
}

const ASYNC_POLL_INTERVAL_MS = 8;

function awaitTask(
  interpreter: NativeLuaInterpreter,
  taskId: number
): Promise<LuaExecutionResult> {
  return new Promise((resolve, reject) => {
    const poll = (): void => {
      try {
        const result = interpreter.takeAsyncResult(taskId);
        if (result) {
          resolve(result);
          return;
        }
        setTimeout(poll, ASYNC_POLL_INTERVAL_MS);
      } catch (error) {
        reject(error);
      }
    };
    setTimeout(poll, 0);
  });
}

/** Calls a small bridge method used by the example to prove module registration. */
export function multiply(a: number, b: number): Promise<number> {
  if (!NativeLua) {
    return Promise.reject(new Error(LINKING_ERROR));
  }
  return NativeLua.multiply(a, b);
}

/** Creates a real native HostObject on both legacy and New Architecture builds. */
export function luaInterpreter(options?: LuaInterpreterOptions): LuaInterpreter {
  // Referencing the module above triggers TurboModule construction and its JSI
  // binding installer. Legacy builds also expose a synchronous fallback so a
  // first call cannot race the eager JS-queue installation.
  if (NativeLua && typeof globalThis.SKRNNativeLuaNewInterpreter !== 'function') {
    const legacyBinding = NativeLua as unknown as {installBindings?: () => void};
    legacyBinding.installBindings?.();
  }
  if (!NativeLua || typeof globalThis.SKRNNativeLuaNewInterpreter !== 'function') {
    throw new Error(LINKING_ERROR);
  }
  const native = globalThis.SKRNNativeLuaNewInterpreter(options);
  return new Proxy(native as LuaInterpreter, {
    get(target, property, receiver) {
      if (property === 'executeStringAsync') {
        return (source: string): Promise<LuaExecutionResult> =>
          awaitTask(native, native.startStringAsync(source));
      }
      if (property === 'executeFileAsync') {
        return (path: string): Promise<LuaExecutionResult> =>
          awaitTask(native, native.startFileAsync(path));
      }
      if (property === 'dostringasync') {
        return (source: string, callback: (code: number) => void): void => {
          void awaitTask(native, native.startStringAsync(source)).then(
            result => callback(result.code),
            () => callback(LUA_ERROR_CODE.LUA_CRASHED_INTERPRETER)
          );
        };
      }
      if (property === 'dofileasync') {
        return (path: string, callback: (code: number) => void): void => {
          void awaitTask(native, native.startFileAsync(path)).then(
            result => callback(result.code),
            () => callback(LUA_ERROR_CODE.LUA_CRASHED_INTERPRETER)
          );
        };
      }
      return Reflect.get(target, property, receiver);
    },
  });
}
