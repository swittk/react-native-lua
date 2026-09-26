#include "react-native-lua.h"

#include <jni.h>
#include <memory>

#include <ReactCommon/CallInvokerHolder.h>
#include <fbjni/fbjni.h>

extern "C" JNIEXPORT jdouble JNICALL
Java_com_reactnativelua_LuaModule_nativeMultiply(
    JNIEnv*, jclass, jdouble a, jdouble b) {
  return SKRNNativeLua::multiply(a, b);
}

extern "C" JNIEXPORT void JNICALL
Java_com_reactnativelua_LuaModule_installLegacy(
    JNIEnv* env,
    jclass,
    jlong runtimePointer,
    jobject callInvokerHolderObject) {
  (void)env;
  auto holder = facebook::jni::alias_ref<
      facebook::react::CallInvokerHolder::javaobject>(
      reinterpret_cast<facebook::react::CallInvokerHolder::javaobject>(
          callInvokerHolderObject));
  SKRNNativeLua::install(
      *reinterpret_cast<facebook::jsi::Runtime*>(runtimePointer),
      holder->cthis()->getCallInvoker());
}

extern "C" JNIEXPORT void JNICALL
Java_com_reactnativelua_LuaModule_cleanupLegacy(
    JNIEnv*, jclass, jlong runtimePointer) {
  if (runtimePointer != 0) {
    SKRNNativeLua::cleanup(
        *reinterpret_cast<facebook::jsi::Runtime*>(runtimePointer));
  }
}
