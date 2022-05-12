#ifndef SKRNNATIVELUA_H
#define SKRNNATIVELUA_H

#include <memory>
#include <jsi/jsi.h>
#include <deque>
extern "C" {
#include <setjmp.h>
}
#include <mutex>
#include <map>
#include <queue>
#include <thread>

//#include "luaSrc/lua.h"

namespace facebook {
namespace jsi {
class Runtime;
}
namespace react {
class CallInvoker;
using RuntimeExecutor =
    std::function<void(std::function<void(jsi::Runtime &runtime)> &&callback)>;
}
}
struct lua_State;

namespace SKRNNativeLua {
  int multiply(float a, float b);

std::string getLuaLibDirPath();
void setLuaLibDirPath(std::string path);

class SKRNLuaMTHelper {
public:
    std::mutex mutex;
    
    std::map<int, void *>conveniencePointers;
//    int hid;
//    SKRNLuaMTHelper() {
//        count++;
//        hid = count;
//        printf("alloc mthelper %d", hid);
//    }
//    SKRNLuaMTHelper (const SKRNLuaMTHelper &c) : mutex(), hid(count++)
//    {
//        printf("copy mthelper %d", hid);
//    }
//    ~SKRNLuaMTHelper() {
//        printf("dealloc mthelper %d", hid);
//    }
//    void bark() {
//        printf("woof %d", hid);
//    }
};
SKRNLuaMTHelper *mtHelperForLuaState(lua_State *L);

// Heavy, heavy thanks to this answer for this solution. https://stackoverflow.com/a/48816876/4469172
class AsyncThreadQueuer {
public:
    std::mutex m_mutex;
    std::queue<std::function<void()>>m_queue;
    std::condition_variable m_condition;
    std::atomic<bool> m_exitCondition;
    int m_numJobs = 0;
    
    // called by main thread
    void AddJob(std::function<void()> f)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push(std::move(f));
            ++m_numJobs;
        }
        m_condition.notify_one();  // It's good style to call notify_one when not holding the lock.
    }

    void worker_main()
    {
        while(!m_exitCondition)
            doJob();
        printf("terminated worker thread");
    }
    void signalTerminateJobs() {
        m_exitCondition = true;
        m_condition.notify_one();
    }
private:
    void doJob()
    {
        std::function<void()> f;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            while (m_numJobs == 0 && !m_exitCondition)
                m_condition.wait(lock);

            if (m_exitCondition)
                return;
            f = std::move(m_queue.front());
            m_queue.pop();
            --m_numJobs;
        }
        f();
    }
};

class SKRNLuaInterpreter : public facebook::jsi::HostObject {
public:
    jmp_buf place;
    lua_State *_state;
    
    bool valid = true;
    bool shouldTerminate = false;
    long long executionLimitMilliseconds = 10000;
    
    facebook::react::RuntimeExecutor executor;
    std::mutex printOutputMutex;
    std::deque<std::string> printOutput;
    int maxPrintOutputCount = 1000;
    
    std::atomic<bool> executing;
    
    std::thread asyncProcessingThread;
    AsyncThreadQueuer asyncThreadQueuer;
        
#pragma mark - LifeCycle Methods
    SKRNLuaInterpreter(facebook::react::RuntimeExecutor _executor) : executor(_executor) {
        executing = false;
        // TODO: Uncomment this when finally implementing do___async functions with asyncProcessingThread
        asyncProcessingThread = std::thread([&]() {
            asyncThreadQueuer.worker_main();
        });
        printf("\nallocated addresss %lld", (long long)this);
        createState();
    }
    ~SKRNLuaInterpreter() {
        terminate();
        printf("attempting to join for instance %lld", (long long)this);
        asyncProcessingThread.join();
        printf("\ndeallocate addresss %lld", (long long)this);
        closeStateIfNeeded();
    }
    void terminate() {
        shouldTerminate = true;
        asyncThreadQueuer.signalTerminateJobs();
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

//void install_withexecutor(facebook::jsi::Runtime &jsiRuntime, facebook::react::RuntimeExecutor executor);
void install(facebook::jsi::Runtime &jsiRuntime, facebook::react::RuntimeExecutor executor);
//void install(facebook::jsi::Runtime &jsiRuntime, std::shared_ptr<facebook::react::CallInvoker> invoker);
void cleanup(facebook::jsi::Runtime &jsiRuntime);
}

#endif /* SKRNNATIVELUA_H */
