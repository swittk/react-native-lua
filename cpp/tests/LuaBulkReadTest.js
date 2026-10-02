// Executed by a real Hermes runtime and the production library HostObject.
let assertions = 0;
function check(ok, message) { assertions++; if (!ok) throw new Error(message); }
function equal(a,b,message) { check(JSON.stringify(a) === JSON.stringify(b), message); }
function rejects(fn, pattern) {
  let error;
  try { fn(); } catch (e) { error=e; }
  check(error && pattern.test(String(error)), 'Expected rejection ' + pattern + ', got ' + error);
}
const lua = SKRNNativeLuaNewInterpreter({allowNetwork: false});
const other = SKRNNativeLuaNewInterpreter({allowNetwork: false});
function run(target, source) {
  const r=target.executeStringResult(source);
  check(r.code === 0, r.error || 'Lua code failed');
}
run(lua, 'a={x=1, enabled=false, items={4,5,6}}; empty={}; co=coroutine.create(function() while true do a.x=a.x+1; coroutine.yield() end end)');
run(other, 'a=42');
let result = lua.readGlobals(['a','missing']);
check(result.a.x === 1 && result.a.enabled === false, 'maps and primitives');
equal(result.a.items, [4,5,6], 'dense arrays');
check(result.missing === null, 'missing global is null');
check(Object.getPrototypeOf(result) === null && Object.getPrototypeOf(result.a) === null, 'null prototype maps');
check(other.readGlobal('a') === 42, 'independent state');
check(!Array.isArray(lua.readGlobal('empty')), 'default empty map');
equal(lua.readGlobal('empty', {emptyTables:'array'}), [], 'explicit empty array');
run(lua, String.raw`special={['__proto__']={polluted=true}, constructor=5, ['x\0y']='a\0b'}`);
const special=lua.readGlobal('special');
check(special.__proto__.polluted === true && special.constructor === 5 && !({}).polluted, 'special keys are data');
check(special['x\0y'] === 'a\0b', 'embedded null string/key');
run(lua, "_G['__proto__']=7");
check(lua.readGlobals(['__proto__']).__proto__ === 7, 'special global names are data');
run(lua, String.raw`_G['x\0y']=8`);
check(lua.readGlobals(['x\0y'])['x\0y'] === 8, 'embedded-null global name');

lua.pushnumber(8); lua.pushnil(); lua.getglobal('a');
const before=lua.gettop();
equal(lua.readValues([-3,2]),[8,null], 'absolute/relative stack values');
check(lua.readValue(-1).x === 1 && lua.gettop() === before, 'singular read leaves stack intact');
for (const bad of [0,100,-100,0.5,NaN,Infinity,-1001000]) {
  rejects(()=>lua.readValues([bad]), /index|indices/);
  check(lua.gettop() === before, 'invalid index stack unchanged');
}
rejects(()=>lua.readGlobals(['a',1]), /strings/);
rejects(()=>lua.readGlobals(['\ud800']), /UTF-8/);
rejects(()=>lua.readGlobals(['\udfff']), /UTF-8/);
rejects(()=>lua.readGlobals('a'), /array/);
rejects(()=>lua.readValues(new Array(1)), /indices/);
rejects(()=>lua.readValues(Array(257).fill(1)), /256/);
for (const bad of [{maxDepth:-1},{maxDepth:65},{maxDepth:NaN},{maxEntries:1.1},
                    {maxStringBytes:Infinity},{emptyTables:'guess'},null,[]]) {
  rejects(()=>lua.readGlobal('a',bad), /option|emptyTables/);
}
run(lua, 'bad={}; bad.self=bad; nested={a={b=1}}; unsupported=function()end');
rejects(()=>lua.readGlobal('bad'), /cyclic/);
rejects(()=>lua.readGlobal('nested',{maxDepth:1}), /maxDepth/);
rejects(()=>lua.readGlobal('unsupported'), /transferable/);
rejects(()=>lua.readGlobals(['a','a'],{maxEntries:7}), /maxEntries/);
check(!lua.executing, 'error releases bulk execution gate');
check(lua.readGlobal('a').x === 1, 'state usable after malformed snapshot');
run(lua, 'setmetatable(_G,{__index=function()error("global trap")end}); t=setmetatable({v=2},{__pairs=function()error("pairs trap")end,__len=function()error("len trap")end,__index=function()error("index trap")end})');
check(lua.readGlobal('absent') === null && lua.readGlobal('t').v === 2, 'raw snapshot invokes no Lua metamethod');
run(lua, 'setmetatable(_G,nil)');

// onFrame-style persistent coroutines share one state, sequential reads follow turns.
lua.getglobal('co'); const handle=lua.tothread(-1); lua.pop(1);
for (let i=0;i<80;i++) {
  check(lua.resume(handle,0).result === 1, 'coroutine yielded');
  const pair=lua.readGlobals(['a','missing']);
  check(pair.a.x === 2+i && pair.missing === null, 'completed turn snapshot');
  check(other.readGlobal('a') === 42, 'second interpreter unaffected');
}
check(result.a.x === 1, 'old snapshot detached from subsequent turns');

// Busy reads must fail without waiting or touching an executing state.
let task=lua.startStringAsync('local n=0;while true do n=n+1 end');
rejects(()=>lua.readGlobal('a'), /executing/);
rejects(()=>lua.readValues([]), /executing/);
check(other.readGlobal('a') === 42, 'idle interpreter can be read while another is busy');
lua.cancel();
function wait(target,id) {
  for(let i=0;i<200;i++) { const r=target.takeAsyncResult(id); if(r) return r; __sleep(2); }
  throw new Error('native worker timed out');
}
check(wait(lua,task).reason === 'cancelled', 'worker cancellation');
check(lua.readGlobal('a').x === 81, 'read after worker ends');

// Parsing JS options can reenter before the snapshot. The gate must be checked AFTER it.
task=0;
rejects(()=>lua.readGlobals(['a'],{get maxDepth(){task=lua.startStringAsync('while true do end');return 32;}}), /executing/);
lua.cancel(); wait(lua,task);
// Native snapshot is already detached before output object creation calls JS.
const originalCreate=Object.create;
let reentered=false;
Object.create=function(proto){
  if(!reentered){ reentered=true; run(lua,'a.x=999; t.v=888'); }
  return originalCreate(proto);
};
try {
  const pair=lua.readGlobals(['a','t']);
  check(pair.a.x === 81 && pair.t.v === 2, 'all roots captured before output construction can reenter');
}
finally { Object.create=originalCreate; }
check(lua.readGlobal('a').x === 999, 'reentrant mutation happens after snapshot');


// Bulk host -> Lua transfer uses Lua terminology: push stack values, set globals.
const pushTop=lua.gettop();
lua.pushValues([12.5,{hello:'world',items:[1,true,'x'],['__proto__']:{safe:true}},null]);
check(lua.gettop()===pushTop+3,'pushValues leaves roots in order');
const pushed=lua.readValues([-3,-2,-1]);
check(pushed[0]===12.5 && pushed[1].hello==='world','pushValues roundtrip');
equal(pushed[1].items,[1,true,'x'],'nested pushed array');
check(pushed[1].__proto__.safe===true && !({}).safe,'push special keys are data');
check(pushed[2]===null,'root null pushes nil');
lua.settop(pushTop);
lua.pushValue({single:9});
check(lua.readValue(-1).single===9,'pushValue convenience');
lua.pop(1);

run(lua,'existing_set=10; removed_set=11; set_trap_calls=0; setmetatable(_G,{__newindex=function(t,k,v)set_trap_calls=set_trap_calls+1;rawset(t,k,v)end})');
lua.setGlobals({existing_set:20,fresh_set:{ok:true},removed_set:null});
check(lua.readGlobal('existing_set')===20 && lua.readGlobal('fresh_set').ok===true,'setGlobals assigns values');
check(lua.readGlobal('removed_set')===null,'setGlobal null clears global');
check(lua.readGlobal('set_trap_calls')===0,'setGlobals bypasses __newindex');
lua.setGlobal('single_set',{value:42});
check(lua.readGlobal('single_set').value===42,'setGlobal convenience');
lua.setGlobal('x\0set','nul');
check(lua.readGlobal('x\0set')==='nul','embedded-null global name set');
run(lua,'setmetatable(_G,nil)');

lua.setGlobals({integer_semantics:3, negative_zero:-0, fractional_semantics:3.5,
                unsafe_integral_semantics:9007199254740992});
run(lua, "integer_kind=math.type(integer_semantics); integer_text=tostring(integer_semantics); " +
         "negative_zero_kind=math.type(negative_zero); negative_zero_is_negative=(1/negative_zero)==-math.huge; " +
         "fractional_kind=math.type(fractional_semantics); " +
         "unsafe_integral_kind=math.type(unsafe_integral_semantics)");
check(lua.readGlobal('integer_kind')==='integer' && lua.readGlobal('integer_text')==='3',
      'safe integral JS numbers become Lua integers');
check(lua.readGlobal('negative_zero_kind')==='float' && lua.readGlobal('negative_zero_is_negative')===true,
      'negative zero remains a signed Lua float');
check(lua.readGlobal('fractional_kind')==='float' && lua.readGlobal('unsafe_integral_kind')==='float',
      'fractional and unsafe integral JS numbers remain Lua floats');

// JS input is snapshotted completely before Lua mutation.
run(lua,'tx_a=1;tx_b=2');
const badBatch={tx_a:99,tx_b:()=>3};
rejects(()=>lua.setGlobals(badBatch),/function|transferable/);
check(lua.readGlobal('tx_a')===1 && lua.readGlobal('tx_b')===2,'invalid setGlobals mutates nothing');
const cyclicInput={}; cyclicInput.self=cyclicInput;
rejects(()=>lua.pushValue(cyclicInput),/cyclic/);
rejects(()=>lua.pushValue(undefined),/transferable/);
rejects(()=>lua.pushValue(NaN),/finite/);
rejects(()=>lua.pushValue(Infinity),/finite/);
function ExampleInstance(){this.x=1;}
for (const bad of [new Map([['x',1]]), new Set([1]), new Date(0), /x/,
                   new ArrayBuffer(4), new Uint8Array([1,2]),
                   new ExampleInstance()]) {
  rejects(()=>lua.pushValue(bad),/plain objects/);
}
const nullProto=Object.create(null); nullProto.x=7;
lua.pushValue(nullProto);
check(lua.readValue(-1).x===7,'null-prototype object is transferable');
lua.pop(1);
rejects(()=>lua.setGlobals(new Map([['x',1]])),/plain objects/);
const symbolInput={x:1}; symbolInput[Symbol('hidden')]=2;
rejects(()=>lua.pushValue(symbolInput),/symbol/);
const symbolGlobals={x:1}; symbolGlobals[Symbol('hidden')]=2;
rejects(()=>lua.setGlobals(symbolGlobals),/symbol/);
const sparseInput=[]; sparseInput.length=2; sparseInput[1]=4;
rejects(()=>lua.pushValue(sparseInput),/sparse|undefined/);
const decoratedArray=[1,2]; decoratedArray.extra=3;
rejects(()=>lua.pushValue(decoratedArray),/extra enumerable|dense indexed/);
const symbolArray=[1]; symbolArray[Symbol('hidden')]=2;
rejects(()=>lua.pushValue(symbolArray),/symbol/);
rejects(()=>lua.pushValues(Array(257).fill(1)),/256/);
rejects(()=>lua.pushValue({x:{y:1}},{maxDepth:1}),/maxDepth/);
rejects(()=>lua.pushValue({x:1},{maxEntries:0}),/maxEntries/);
rejects(()=>lua.pushValue({x:1},{emptyTables:'array'}),/read-only/);
check(lua.gettop()===pushTop,'failed pushes restore stack');

// Getters/proxies run before the Lua gate; a reentrant worker makes the transfer reject.
run(lua,'reentrant_set=5');
task=0;
const reentrantInput={get value(){task=lua.startStringAsync('while true do end');return 7;}};
rejects(()=>lua.setGlobals({reentrant_set:reentrantInput}),/executing/);
lua.cancel(); wait(lua,task);
check(lua.readGlobal('reentrant_set')===5,'busy reentrant set mutates nothing');

// Busy push/set reject immediately; an independent interpreter remains usable.
task=lua.startStringAsync('while true do end');
rejects(()=>lua.pushValue(1),/executing/);
rejects(()=>lua.pushValues([]),/executing/);
rejects(()=>lua.setGlobal('busy_set',1),/executing/);
rejects(()=>lua.setGlobals({busy_set:1}),/executing/);
other.setGlobal('writer_other',77);
check(other.readGlobal('writer_other')===77,'idle second interpreter can set while another is busy');
lua.cancel(); wait(lua,task);

run(lua, `frame={version=1,revision=1,width=320,height=420,background='#071126',commands={}}
for i=1,20 do frame.commands[i]={id='brick-'..i,type='rect',x=i,y=3,width=44,height=18,radius=3,
paint={style='fill',color='#FB7185'}} end`);
// Compare fully built data trees, not an unused native result.
function field(index,key,read) {
  lua.pushstring(key); lua.rawget(index);
  try { return read(); } finally { lua.pop(1); }
}
function str(){return lua.tostring(-1);} function num(){return lua.tonumber(-1);}
function fineRead() {
  lua.getglobal('frame'); const top=lua.gettop();
  try {
    const frame={};
    for(const key of ['version','revision','width','height']) frame[key]=field(top,key,num);
    frame.background=field(top,'background',str);
    frame.commands=field(top,'commands',()=>{
      const commands=[], arr=lua.gettop(), count=lua.rawlen(arr);
      for(let i=1;i<=count;i++) {
        lua.rawgeti(arr,i); const at=lua.gettop(), cmd={};
        try {
          for(const key of ['id','type']) cmd[key]=field(at,key,str);
          for(const key of ['x','y','width','height','radius']) cmd[key]=field(at,key,num);
          cmd.paint=field(at,'paint',()=>{
            const paint=lua.gettop();
            return {style:field(paint,'style',str),color:field(paint,'color',str)};
          });
          commands.push(cmd);
        } finally {lua.pop(1);}
      }
      return commands;
    }); return frame;
  } finally {lua.pop(1);}
}
function canonical(v) {
  if(Array.isArray(v)) return v.map(canonical);
  if(v && typeof v==='object') {
    const out={}; for(const k of Object.keys(v).sort()) out[k]=canonical(v[k]); return out;
  } return v;
}
equal(canonical(fineRead()),canonical(lua.readGlobal('frame')), 'bulk and per-field trees agree');
function bench(fn) {
  for(let i=0;i<30;i++) fn();
  const times=[];
  for(let i=0;i<300;i++){const start=__now(); const f=fn(); check(f.commands.length===20,'bench used data');times.push(__now()-start);}
  times.sort((a,b)=>a-b);
  return {n:times.length,mean:times.reduce((a,b)=>a+b,0)/times.length,p50:times[150],p95:times[285]};
}
// Alternate order to reduce warmup/order bias. No display/GPU involved.
__log('BULK_READ_BENCH '+JSON.stringify({bulk:bench(()=>lua.readGlobal('frame')),fine:bench(fineRead),
  fineRepeat:bench(fineRead),bulkRepeat:bench(()=>lua.readGlobal('frame'))}));

other.destroy();
for(const fn of [()=>other.readGlobal('a'),()=>other.readGlobals([]),()=>other.readValue(1),()=>other.readValues([]),
  ()=>other.pushValue(1),()=>other.pushValues([]),()=>other.setGlobal('x',1),()=>other.setGlobals({x:1})])
  rejects(fn,/destroyed/);
lua.destroy();
__log('BULK_VALUE_ASSERTIONS '+assertions);
