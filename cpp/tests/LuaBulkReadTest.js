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
for(const fn of [()=>other.readGlobal('a'),()=>other.readGlobals([]),()=>other.readValue(1),()=>other.readValues([])])
  rejects(fn,/destroyed/);
lua.destroy();
__log('BULK_READ_ASSERTIONS '+assertions);
