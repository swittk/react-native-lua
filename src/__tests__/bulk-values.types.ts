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

const payload: LuaValue = {message: 'hello', values: [1, true, null]};
lua.pushValue(payload);
lua.pushValues([payload, 3, 'x'] as const, {maxDepth: 8});
lua.setGlobal('output', payload);
lua.setGlobals({graphics: payload, status: 'ready'}, {maxEntries: 100});
// @ts-expect-error undefined is outside LuaValue
lua.pushValue(undefined);
// @ts-expect-error pushValues requires transferable LuaValue roots
lua.pushValues([()=>1]);
// @ts-expect-error setGlobal names are strings
lua.setGlobal(123, payload);
// @ts-expect-error setGlobals values must be LuaValue
lua.setGlobals({bad: Symbol('x')});
// @ts-expect-error emptyTables is a read-only policy, not a push/set limit
lua.pushValue(payload, {emptyTables: 'array'});
