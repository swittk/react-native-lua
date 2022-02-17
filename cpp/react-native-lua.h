#ifndef SKRNNATIVELUA_H
#define SKRNNATIVELUA_H

#include <memory>
#include <jsi/jsi.h>
#include <deque>
extern "C" {
#include <setjmp.h>
}
#include <mutex>
//#include "luaSrc/lua.h"

namespace facebook {
namespace jsi {
class Runtime;
}
namespace react {
class CallInvoker;
}
}
struct lua_State;

namespace SKRNNativeLua {
  int multiply(float a, float b);

class SKRNLuaInterpreter : public facebook::jsi::HostObject {
public:
    jmp_buf place;
    lua_State *_state;
    
    bool valid = true;
    bool shouldTerminate = false;
    long long executionLimitMilliseconds = 10000;
    
    std::shared_ptr<facebook::react::CallInvoker> callInvoker;
    std::deque<std::string> printOutput;
    int maxPrintOutputCount = 100;
    
#pragma mark - LifeCycle Methods
    SKRNLuaInterpreter() {
        createState();
    }
    ~SKRNLuaInterpreter() {
        closeStateIfNeeded();
    }
    void createState();
    void closeStateIfNeeded();
#pragma mark - Interaction Methods
    /**
     * Loads and runs the given string. It is defined as the following macro:
     * (luaL_loadstring(L, str) || lua_pcall(L, 0, LUA_MULTRET, 0)).
     * @returns result code, LUA_OK if ok. (returns 0 if there are no errors or 1 in case of errors)
     */
    int doString(std::string str);
    int doFile(std::string filePath);
    std::string getLatestError();
    static int staticLuaPrintHandler(lua_State *L);
    void luaPrintHandler(std::string str);
#pragma mark - Helper Methods
    
    
#pragma mark - JSI Compliance methods
    facebook::jsi::Value get(facebook::jsi::Runtime &runtime, const facebook::jsi::PropNameID &name);
    std::vector<facebook::jsi::PropNameID> getPropertyNames(facebook::jsi::Runtime& rt);
};

void install(facebook::jsi::Runtime &jsiRuntime, std::shared_ptr<facebook::react::CallInvoker> invoker);
//void install(facebook::jsi::Runtime &jsiRuntime, std::shared_ptr<facebook::react::CallInvoker> invoker);
void cleanup(facebook::jsi::Runtime &jsiRuntime);
}

#endif /* SKRNNATIVELUA_H */
