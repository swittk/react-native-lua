#include <jni.h>
#include "react-native-lua.h"
#include <memory>
#include <CallInvokerHolder.h>
extern "C"
JNIEXPORT jint JNICALL
Java_com_reactnativelua_LuaModule_nativeMultiply(JNIEnv *env, jclass type, jint a, jint b) {
    return SKRNNativeLua::multiply(a, b);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_reactnativelua_LuaModule_initialize(JNIEnv *env, jclass clazz, jlong jsi_runtime_pointer, jobject jsCallInvokerHolder) {
    JavaVM *jvm;
    env->GetJavaVM(&jvm);
    facebook::react::CallInvokerHolder *holder = (facebook::react::CallInvokerHolder *)jsCallInvokerHolder;
    auto jsCallInvoker = holder->getCallInvoker();

    SKRNNativeLua::install(*reinterpret_cast<facebook::jsi::Runtime *>(jsi_runtime_pointer), jsCallInvoker);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_reactnativelua_LuaModule_cleanup(JNIEnv *env, jclass clazz, jlong jsi_runtime_pointer) {
    // Nothing to really do I think
}
