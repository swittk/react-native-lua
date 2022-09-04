#include <jni.h>
#include "react-native-lua.h"
#include <memory>
#include <ReactCommon/CallInvokerHolder.h>
extern "C"
JNIEXPORT jint JNICALL
Java_com_reactnativelua_LuaModule_nativeMultiply(JNIEnv *env, jclass type, jint a, jint b) {
    return SKRNNativeLua::multiply(a, b);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_reactnativelua_LuaModule_initialize(JNIEnv *env, jclass clazz, jlong jsi_runtime_pointer, jobject jsCallInvokerHolderImpl) {
    JavaVM *jvm;
    env->GetJavaVM(&jvm);
    // facebook::react::CallInvokerHolder *holder = (facebook::react::CallInvokerHolder *)jsCallInvokerHolder;
    // auto jsCallInvoker = holder->getCallInvoker();

    // This blob of code from https://gist.github.com/VolkerLieber/fd7f5b95d8ec0b64c61f689d4c8a2d76 thanks to @VolkerLieber
	jclass CallInvokerHolderImplClass = env->GetObjectClass(jsCallInvokerHolderImpl);  
	jfieldID mHybridDataField = env->GetFieldID(CallInvokerHolderImplClass, "mHybridData", "Lcom/facebook/jni/HybridData;");  
	jobject mHybridData = env->GetObjectField(jsCallInvokerHolderImpl, mHybridDataField);  
	jclass HybridDataClass = env->GetObjectClass(mHybridData);  
	jfieldID mDestructorField = env->GetFieldID(HybridDataClass, "mDestructor", "Lcom/facebook/jni/HybridData$Destructor;");  
	jobject mDestructor = env->GetObjectField(mHybridData, mDestructorField);  
	jclass DestructorClass = env->GetObjectClass(mDestructor);  
	jfieldID mNativePointerField = env->GetFieldID(DestructorClass, "mNativePointer", "J");  
	jlong mNativePointer = env->GetLongField(mDestructor, mNativePointerField);
	std::shared_ptr<facebook::react::CallInvoker> jsCallInvoker = ((facebook::react::CallInvokerHolder*)mNativePointer)->getCallInvoker();

    SKRNNativeLua::install(*reinterpret_cast<facebook::jsi::Runtime *>(jsi_runtime_pointer), jsCallInvoker);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_reactnativelua_LuaModule_cleanup(JNIEnv *env, jclass clazz, jlong jsi_runtime_pointer) {
    // Nothing to really do I think
}
