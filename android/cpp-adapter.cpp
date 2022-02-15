#include <jni.h>
#include "react-native-lua.h"
#include <memory>

extern "C"
JNIEXPORT jint JNICALL
Java_com_reactnativelua_LuaModule_nativeMultiply(JNIEnv *env, jclass type, jint a, jint b) {
    return SKRNNativeLua::multiply(a, b);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_reactnativelua_LuaModule_initialize(JNIEnv *env, jclass clazz, jlong jsi_runtime_pointer) {
    JavaVM *jvm;
    env->GetJavaVM(&jvm);
    SKRNNativeLua::install(*reinterpret_cast<facebook::jsi::Runtime *>(jsi_runtime_pointer));
}
extern "C"
JNIEXPORT void JNICALL
Java_com_reactnativelua_LuaModule_cleanup(JNIEnv *env, jclass clazz, jlong jsi_runtime_pointer) {
    // Nothing to really do I think
}
