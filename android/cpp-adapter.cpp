#include <jni.h>
#include "react-native-lua.h"
#include <memory>
#include <ReactCommon/CallInvokerHolder.h>
#include <ReactCommon/RuntimeExecutor.h>
extern "C"
JNIEXPORT jint JNICALL
Java_com_reactnativelua_LuaModule_nativeMultiply(JNIEnv *env, jclass type, jint a, jint b) {
    return SKRNNativeLua::multiply(a, b);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_reactnativelua_LuaModule_initialize(JNIEnv *env, jclass clazz, jlong jsi_runtime_pointer, jobject runtimeExecutor) {
    JavaVM *jvm;
    env->GetJavaVM(&jvm);

    // I seriously doubt this is the way since it's a JNI HybridClass of sorts, but I really have no idea how to do this.
    facebook::react::RuntimeExecutor executor = *(facebook::react::RuntimeExecutor *)runtimeExecutor;
//    facebook::react::CallInvokerHolder *holder = (facebook::react::CallInvokerHolder *)jsCallInvokerHolder;
//    auto jsCallInvoker = holder->getCallInvoker();
//    SKRNNativeLua::install(*reinterpret_cast<facebook::jsi::Runtime *>(jsi_runtime_pointer), jsCallInvoker);
    SKRNNativeLua::install(*reinterpret_cast<facebook::jsi::Runtime *>(jsi_runtime_pointer), executor);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_reactnativelua_LuaModule_cleanup(JNIEnv *env, jclass clazz, jlong jsi_runtime_pointer) {
    // Nothing to really do I think
}
