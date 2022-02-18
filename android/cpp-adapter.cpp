#include <jni.h>
#include "react-native-lua.h"
#include <memory>
#include <ReactCommon/CallInvokerHolder.h>
#include <react/jni/CxxModuleWrapper.h>
#include <react/jni/JavaScriptExecutorHolder.h>
#include <react/jni/JCallback.h>
#include <fbjni/fbjni.h>
#include <cxxreact/CxxNativeModule.h>
#ifdef __ANDROID__
#include <android/log.h>
#define printf(...) __android_log_print(ANDROID_LOG_DEBUG, "TAG", __VA_ARGS__);
#endif

extern "C"
JNIEXPORT jint JNICALL
Java_com_reactnativelua_LuaModule_nativeMultiply(JNIEnv *env, jclass type, jint a, jint b) {
    return SKRNNativeLua::multiply(a, b);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_reactnativelua_LuaModule_initialize(JNIEnv *env, jclass clazz, jlong jsi_runtime_pointer,
                                             jobject jsCallInvokerHolder) {
    facebook::react::CallInvokerHolder *holder = (facebook::react::CallInvokerHolder *)jsCallInvokerHolder;
//    printf("bout to invoke");
//    holder->getCallInvoker()->invokeAsync([]{
//        printf("test invoking");
//    });
//    printf("invoked");
//    facebook::jni::JCxxCallbackImpl::jhybridobject
//    facebook::jni::jhybridobject;
//    facebook::react::CallInvokerHolder::javaobject j = (facebook::react::CallInvokerHolder::javaobject)jsCallInvokerHolder;
//    facebook::jni::alias_ref<facebook::react::CallInvokerHolder::javaobject> invoker = jsCallInvokerHolder;
//    facebook::jni::alias_ref<facebook::react::CallInvokerHolder::javaobject> invokerWhat = (facebook::jni::alias_ref<facebook::react::CallInvokerHolder::javaobject>)jsCallInvokerHolder;
    JavaVM *jvm;
    env->GetJavaVM(&jvm);
//    jclass cls = env->GetObjectClass(jsCallInvokerHolder);
//    jfieldID field = env->GetFieldID(cls, "mHybridData", "Lcom/facebook/jni/HybridData;");
//    env->GetObjectField()
//    facebook::react::CallInvokerHolder *holder = jsCallInvokerHolder->cthis()->getCallInvoker();
//    auto jsCallInvoker = holder ->getCallInvoker();

    SKRNNativeLua::install(*reinterpret_cast<facebook::jsi::Runtime *>(jsi_runtime_pointer), holder->getCallInvoker());
}
extern "C"
JNIEXPORT void JNICALL
Java_com_reactnativelua_LuaModule_cleanup(JNIEnv *env, jclass clazz, jlong jsi_runtime_pointer) {
    // Nothing to really do I think
}
