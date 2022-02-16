#include "react-native-lua.h"
extern "C" {
#include "lua_src/lua.h"
#include "lua_src/lauxlib.h"
#include "lua_src/lualib.h"
}
#include <jsi/jsi.h>
#include "CPPNumericStringHashCompare.h"
#include <sstream>

#define EZ_JSI_HOST_FN_TEMPLATE(numArgs, capture) jsi::Function::createFromHostFunction\
(runtime, name, numArgs,\
[&](facebook::jsi::Runtime &runtime,\
const facebook::jsi::Value &thisValue,\
const facebook::jsi::Value *arguments,\
size_t count) -> jsi::Value \
{capture}) \

#define EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE(capture) EZ_JSI_HOST_FN_TEMPLATE(1, { \
if(count < 1) return jsi::Value::undefined(); \
capture \
})

namespace SKRNNativeLua {
using namespace facebook;

void install(facebook::jsi::Runtime &jsiRuntime) {
    using namespace jsi;
    auto newInterpreterFunction =
    jsi::Function::createFromHostFunction(
                                          jsiRuntime,
                                          PropNameID::forAscii(jsiRuntime, "SKRNNativeLuaNewInterpreter"),
                                          0,
                                          //                                          [&, invoker](Runtime &runtime, const Value &thisValue, const Value *arguments,
                                          [&](Runtime &runtime, const Value &thisValue, const Value *arguments,
                                                                size_t count) -> Value
                                          {
                                              jsi::Object object = jsi::Object::createFromHostObject(runtime, std::make_shared<SKRNLuaInterpreter>());
                                              return object;
                                          });
    jsiRuntime.global().setProperty(jsiRuntime, "SKRNNativeLuaNewInterpreter",
                                    std::move(newInterpreterFunction));

}
//void install(facebook::jsi::Runtime &jsiRuntime, std::shared_ptr<facebook::react::CallInvoker> invoker);
void cleanup(facebook::jsi::Runtime &jsiRuntime) {
    
}
int multiply(float a, float b) {
    return a * b;
}

// Inspired by answer to https://stackoverflow.com/a/4514193/4469172
int SKRNLuaInterpreter::staticLuaPrintHandler(lua_State *L) {
    SKRNLuaInterpreter *me = (SKRNLuaInterpreter *)lua_touserdata(L, lua_upvalueindex(1));
    int nargs = lua_gettop(L);
    std::stringstream outStr;
    for (int i=1; i <= nargs; i++) {
        // Mimicking what luaB_print does; add a \t if it's idx > 1
        if(i > 1) {
            outStr << "\t";
        }
        
        if (lua_isstring(L, i)) {
            /* Pop the next arg using lua_tostring(L, i) and do your print */
            const char *str = lua_tostring(L, i);
            outStr << str;
        }
        else {
            // Mimicking what luaB_print does; convert to string and just print
            size_t strSize = 0;
            const char *s = lua_tolstring(L, i, &strSize);
            outStr << std::string(s, strSize);
        }
    }
    me->luaPrintHandler(outStr.str());
    return 0;
}
void SKRNLuaInterpreter::luaPrintHandler(std::string str) {
    printOutput.push_back(str);
    if(printOutput.size() > maxPrintOutputCount) {
        printOutput.pop_front();
    }
}

void SKRNLuaInterpreter::createState() {
    _state = luaL_newstate();
    luaL_openlibs(_state);
    // Register class method in Lua https://stackoverflow.com/a/21326241/4469172
    lua_pushlightuserdata(_state, this);
    lua_pushcclosure(_state, &SKRNLuaInterpreter::staticLuaPrintHandler, 1);
    lua_setglobal(_state, "print");
}

void SKRNLuaInterpreter::closeStateIfNeeded() {
    if(_state != NULL) {
        lua_close(_state);
    }
}

/**
 * Loads and runs the given string. It is defined as the following macro:
 * (luaL_loadstring(L, str) || lua_pcall(L, 0, LUA_MULTRET, 0)).
 * @returns result code, LUA_OK if ok. (returns 0 if there are no errors or 1 in case of errors)
 */
int SKRNLuaInterpreter::doString(std::string str) {
    return luaL_dostring(_state, str.c_str());
}
int SKRNLuaInterpreter::doFile(std::string str) {
    // Nice string prefix checking using stl (https://stackoverflow.com/a/40441240/4469172)
    if (str.rfind("file:///", 0) == 0) { // pos=0 limits the search to the prefix
        str = str.substr(7); // Slice out beginning "file://"
    }
    return luaL_dofile(_state, str.c_str());
}
std::string SKRNLuaInterpreter::getLatestError() {
    return lua_tostring(_state, -1);
}

jsi::Value SKRNLuaInterpreter::get(jsi::Runtime &runtime, const jsi::PropNameID &name) {
    std::string methodName = name.utf8(runtime);
    long long methodSwitch = string_hash(methodName.c_str());
    switch(methodSwitch) {
        case "printCount"_sh: {
            return jsi::Value((int)printOutput.size());
        } break;
        case "getPrint"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1, {
                int numElems;
                if(count < 1) {
                    numElems = (int)printOutput.size();
                }
                else {
                    numElems = (int)arguments[0].asNumber();
                    if(numElems > printOutput.size()) {
                        numElems = (int)printOutput.size();
                    }
                }
                jsi::Array ret = jsi::Array(runtime, numElems);
                for(int i = 0; i < numElems; i++) {
                    ret.setValueAtIndex(runtime, i, jsi::String::createFromUtf8(runtime, printOutput[0]));
                    printOutput.pop_front();
                }
                return std::move(ret);
            });
        } break;
        // These methods listed from https://www.lua.org/manual/5.4/manual.html#lua_pop
        case "pop"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1,{
                if(count < 1) return jsi::Value::undefined();
                lua_pop(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        case "pushboolean"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1, {
                if(count < 1) return jsi::Value::undefined();
                lua_pushboolean(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
            // lua_pushcclosure
            // lua_pushcfunction
            // lua_pushfstring
        case "pushglobaltable"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(0, {
                lua_pushglobaltable(_state);
                return jsi::Value::undefined();
            });
        } break;
        case "pushinteger"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1,{
                if(count < 1) return jsi::Value::undefined();
                lua_pushinteger(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        case "pushnil"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(0,{
                lua_pushnil(_state);
                return jsi::Value::undefined();
            });
        } break;
        case "pushnumber"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(0,{
                if(count < 1) return jsi::Value::undefined();
                lua_pushnumber(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        case "pushstring"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1,{
                if(count < 1) return jsi::Value::undefined();
                lua_pushstring(_state, arguments[0].asString(runtime).utf8(runtime).c_str());
                return jsi::Value::undefined();
            });
        } break;
        case "pushthread"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1,{
                return jsi::Value(lua_pushthread(_state));
            });
        } break;
        case "pushvalue"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1,{
                if(count < 1) return jsi::Value::undefined();
                lua_pushvalue(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        case "rawequal"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(2,{
                if(count < 2) return jsi::Value::undefined();
                return jsi::Value(lua_rawequal(_state, arguments[0].asNumber(), arguments[1].asNumber()));
            });
        } break;
        case "rawget"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1,{
                if(count < 1) return jsi::Value::undefined();
                return jsi::Value(lua_rawget(_state, arguments[0].asNumber()));
            });
        } break;
        case "rawgeti"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(2,{
                if(count < 2) return jsi::Value::undefined();
                return jsi::Value(lua_rawgeti(_state, arguments[0].asNumber(), arguments[1].asNumber()));
            });
        } break;
            //rawgetp
        case "rawlen"_sh: {
            return EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE({
                return jsi::Value((double)lua_rawlen(_state, arguments[0].asNumber()));
            });
        } break;
        case "rawset"_sh: {
            return EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE({
                lua_rawset(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        case "rawseti"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(2,{
                if(count < 2) return jsi::Value::undefined();
                lua_rawseti(_state, arguments[0].asNumber(), arguments[1].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        // lua_rawsetp
            
            // skip to lua_remove
        case "remove"_sh: {
            return EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE({
                lua_remove(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        case "insert"_sh: {
            return EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE({
                lua_insert(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        case "replace"_sh: {
            return EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE({
                lua_replace(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        case "resetthread"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(0,{
                lua_resetthread(_state);
                return jsi::Value::undefined();
            });
        } break;
        case "resume"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(2,{
                if(count < 2) return jsi::Value::undefined();
                long ptrVal = arguments[0].asNumber();
                int nargs = arguments[1].asNumber();
                int nresults = 0;
                int result = lua_resume(_state, (lua_State *)ptrVal, nargs, &nresults);
                jsi::Object ret = jsi::Object(runtime);
                ret.setProperty(runtime, "result", result);
                ret.setProperty(runtime, "nresults", nresults);
                return ret;
            });
        } break;
        case "rotate"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(2,{
                if(count < 2) return jsi::Value::undefined();
                lua_rotate(_state, arguments[0].asNumber(), arguments[1].asNumber());
                return jsi::Value::undefined();
            });
        } break;
            //lua_setallocf
        case "setfield"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(2,{
                if(count < 2) return jsi::Value::undefined();
                lua_setfield(_state, arguments[0].asNumber(), arguments[1].asString(runtime).utf8(runtime).c_str());
                return jsi::Value::undefined();
            });
        } break;
        case "setglobal"_sh: {
            return EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE({
                lua_setglobal(_state, arguments[1].asString(runtime).utf8(runtime).c_str());
                return jsi::Value::undefined();
            });
        }break;
        case "seti"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(2,{
                if(count < 2) return jsi::Value::undefined();
                lua_seti(_state, arguments[0].asNumber(), arguments[1].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        case "setiuservalue"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(2,{
                if(count < 2) return jsi::Value::undefined();
                return jsi::Value(lua_setiuservalue(_state, arguments[0].asNumber(), arguments[1].asNumber()));
            });
        } break;
        case "setmetatable"_sh: {
            return EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE({
                return jsi::Value(lua_setmetatable(_state, arguments[0].asNumber()));
            });
        } break;
        case "settable"_sh: {
            return EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE({
                lua_settable(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        case "settop"_sh: {
            return EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE({
                lua_settop(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        case "gettop"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(0, {
                return jsi::Value(lua_gettop(_state));
            });
        } break;
            //lua_setwarnf
        case "status"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(0,{
                return jsi::Value(lua_status(_state));
            });
        } break;
        case "stringtonumber"_sh: {
            return EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE({
                size_t ret = lua_stringtonumber(_state, arguments[0].asString(runtime).utf8(runtime).c_str());
                return jsi::Value((int)(ret));
            });
        } break;
        case "gettable"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1,{
                if(count < 1) return jsi::Value::undefined();
                return jsi::Value(lua_gettable(_state, arguments[0].asNumber()));
            });
        } break;
            
        case "dostring"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1,{
                if(count < 1) return jsi::Value::undefined();
                std::string str = arguments[0].asString(runtime).utf8(runtime);
                return jsi::Value(doString(str));
            });
        } break;
        case "dofile"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1,{
                if(count < 1) return jsi::Value::undefined();
                std::string str = arguments[0].asString(runtime).utf8(runtime);
                int ret = doFile(str);
                return jsi::Value(ret);
            });
        } break;
        case "getglobal"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1, {
                if(count < 1) return jsi::Value::undefined();
                std::string str = arguments[0].asString(runtime).utf8(runtime);
                lua_getglobal(_state, str.c_str());
                return jsi::Value::undefined();
            });
        } break;
        case "getLatestError"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(0, {
                return jsi::String::createFromUtf8(runtime, getLatestError());
            });
        } break;
        case "var_asnumber"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1, {
                if(count < 1) return jsi::Value::undefined();
                std::string str = arguments[0].asString(runtime).utf8(runtime);
                lua_getglobal(_state, str.c_str());
                if(lua_isnumber(_state, -1)) {
                    //        lua_Number num = lua_tonumberx(_state, -1, NULL);
                    lua_Number num = lua_tonumber(_state, -1);
                    return jsi::Value(num);
                }
                return jsi::Value(0);
            });
        } break;
        case "toboolean"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1, {
                if(count < 1) return jsi::Value::undefined();
                return jsi::Value(lua_toboolean(_state, arguments[0].asNumber()));
            });
        } break;
            // tocfunction
        case "toclose"_sh: {
            return EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE({
                lua_toclose(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
        case "tointeger"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1, {
                if(count < 1) return jsi::Value::undefined();
                return jsi::Value((int)lua_tointeger(_state, arguments[0].asNumber()));
            });
        } break;
        case "tonumber"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1, {
                if(count < 1) return jsi::Value::undefined();
                return jsi::Value(lua_tonumber(_state, arguments[0].asNumber()));
            });
        } break;
        case "tostring"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1, {
                if(count < 1) return jsi::Value::undefined();
                lua_tostring(_state, arguments[0].asNumber());
                return jsi::Value::undefined();
            });
        } break;
//        case "topointer"_sh: {
//
//        } break;
        case "tothread"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1, {
                if(count < 1) return jsi::Value::undefined();
                lua_State *thread = lua_tothread(_state, arguments[0].asNumber());
                return jsi::Value((double)(long)(thread));
            });
        } break;
        case "type"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1, {
                int idx = count < 1 ? -1 : arguments[0].asNumber();
                return jsi::Value(lua_type(_state, idx));
            });
        } break;
        case "typename"_sh: {
            return EZ_JSI_HOST_FN_TEMPLATE(1, {
                if(count < 1) return jsi::Value::undefined();
                return jsi::String::createFromUtf8(runtime, lua_typename(_state, arguments[0].asNumber()));
            });
        } break;
        
            // skip to lua_yield
        case "yield"_sh: {
            return EZ_LUA_MANDATORY_SINGLE_ARGUMENT_TEMPLATE({
                return jsi::Value(lua_yield(_state, arguments[0].asNumber()));
            });
        } break;
            
            //        case "size"_sh: {
            //            return ObjectFromSKRNSize(runtime, size());
            //        } break;
    }
    return jsi::Value::undefined();
}

static std::vector<std::string> nativeLuaInterpreterKeys = {
    "pop",
    "pushboolean",
    "pushglobaltable",
    "pushinteger",
    "pushnil",
    "pushnumber",
    "pushstring",
    "pushthread",
    "pushvalue",
    "rawequal",
    "rawget",
    "rawgeti",
    "rawlen",
    "rawset",
    "rawseti",
    "remove",
    "insert",
    "replace",
    "resetthread",
    "resume",
    "rotate",
    "setfield",
    "setglobal",
    "seti",
    "setiuservalue",
    "setmetatable",
    "settable",
    "settop",
    "gettop",
    "status",
    "stringtonumber",
    "gettable",
    "dostring",
    "dofile",
    "getglobal",
    "getLatestError",
    "var_asnumber",
    "toboolean",
    "toclose",
    "tointeger",
    "tonumber",
    "tostring",
    "topointer",
    "tothread",
    "type",
    "typename",
    "yield"
};
std::vector<jsi::PropNameID> SKRNLuaInterpreter::getPropertyNames(jsi::Runtime& rt) {
    std::vector<jsi::PropNameID> ret;
    for(std::string key : nativeLuaInterpreterKeys) {
        ret.push_back(jsi::PropNameID::forUtf8(rt, key));
    }
    return ret;
}
}
