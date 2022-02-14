#include "react-native-lua.h"
#include "luaSrc/lua.h"
#include "luaSrc/lauxlib.h"
#include "luaSrc/lualib.h"
#include "jsi.h"
namespace SKRNNativeLua {
int multiply(float a, float b) {
    return a * b;
}

class LuaInterpreter : facebook::jsi::HostObject {
    lua_State *_state;
    
    
#pragma mark - LifeCycle Methods
    LuaInterpreter() {
        createState();
    }
    ~LuaInterpreter() {
        closeStateIfNeeded();
    }
    void createState() {
        _state = luaL_newstate();
        luaL_openlibs(_state);
    }
    
    void closeStateIfNeeded() {
        if(_state != NULL) {
            lua_close(_state);
        }
    }
#pragma mark - Interaction Methods
    /**
     Interprets the string into the lua state.
     @returns result code, LUA_OK if ok.
     */
    int parse(std::string str) {
        return luaL_dostring(_state, str.c_str());
    }
    
    
    
#pragma mark - Helper Methods
    
};
}
