import type {LuaInterpreter, LuaValue} from '../index';

declare const lua: LuaInterpreter;
const pair = lua.readValues([1, -1] as const);
const checkedPair: readonly [LuaValue, LuaValue] = pair;
const snapshot = lua.readGlobals(['graphics', 'ui'] as const);
const checkedGraphics: LuaValue = snapshot.graphics;
// @ts-expect-error only requested global names are inferred
snapshot.notRequested;
// @ts-expect-error a generic LuaValue does not promise an application-specific number
const notANumber: number = snapshot.graphics;
// @ts-expect-error indices must be numbers
lua.readValues(['name']);
// @ts-expect-error names must be strings
lua.readGlobals([1]);
// @ts-expect-error invalid empty-table policy
lua.readValue(-1, {emptyTables: 'auto'});
lua.readGlobal('output', {maxDepth: 12, maxEntries: 4096, maxStringBytes: 65536});
void checkedPair;
void checkedGraphics;
void notANumber;
