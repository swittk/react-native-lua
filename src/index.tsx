import { NativeModules, Platform } from 'react-native';

const LINKING_ERROR =
  `The package 'react-native-lua' doesn't seem to be linked. Make sure: \n\n` +
  Platform.select({ ios: "- You have run 'pod install'\n", default: '' }) +
  '- You rebuilt the app after installing the package\n' +
  '- You are not using Expo managed workflow\n';

const Lua = NativeModules.SKNativeLua
  ? NativeModules.SKNativeLua
  : new Proxy(
    {},
    {
      get() {
        throw new Error(LINKING_ERROR);
      },
    }
  );

export function multiply(a: number, b: number): Promise<number> {
  return Lua.multiply(a, b);
}

/** This corresponds to the types LUA_TNIL, LUA_TBOOLEAN, etc.. defined in `lua.h` */
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

export interface LuaInterpreter {
  dostring(string: string): number,
  dofile(filepath: string): number,

  /** The number of printed strings */
  printCount: number;
  /** Pop printed strings out by the count value specified
   * If count is not specified, pops out all strings.
   */
  getPrint(count?: number): string[];

  pop(arg0: number): void,
  pushboolean(arg0: number): void,
  pushglobaltable(): void,
  pushinteger(arg0: number): void,
  pushnil(): void,
  pushnumber(): void,
  pushstring(arg0: string): void,
  pushthread(arg0: number): number,
  pushvalue(arg0: number): void,
  rawequal(arg0: number, arg1: number): number,
  rawget(arg0: number): number,
  rawgeti(arg0: number, arg1: number): number,
  rawlen(arg0: number): number,
  rawset(arg0: number): void,
  rawseti(arg0: number, arg1: number): void,
  remove(arg0: number): void,
  insert(arg0: number): void,
  replace(arg0: number): void,
  resetthread(): void,
  resume(arg0: number, arg1: number): unknown,
  rotate(arg0: number, arg1: number): void,
  setfield(arg0: number, arg1: string): void,
  setglobal(arg0: string): void,
  seti(arg0: number, arg1: number): void,
  setiuservalue(arg0: number, arg1: number): number,
  setmetatable(arg0: number): number,
  settable(arg0: number): void,
  settop(arg0: number): void,
  gettop(): number,
  status(): number,
  stringtonumber(arg0: string): number,
  gettable(arg0: number): number,
  dostring(arg0: number): number,
  dofile(arg0: number): number,
  getglobal(arg0: string): void,
  getLatestError(): string,
  var_asnumber(arg0: string): number,
  toboolean(arg0: number): number,
  toclose(arg0: number): number,
  tointeger(arg0: number): void,
  tonumber(arg0: number): number,
  tostring(arg0: number): number,
  topointer(arg0: number): void,
  tothread(arg0: number): number,
  type(arg0: number): LUA_TYPE,
  typename(arg0: number): string,
  yield(): number
}

export function luaInterpreter(): LuaInterpreter {
  return (global as any).SKRNNativeLuaNewInterpreter();
}