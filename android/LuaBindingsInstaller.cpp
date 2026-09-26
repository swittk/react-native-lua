#include "react-native-lua.h"

#include <ReactCommon/BindingsInstallerHolder.h>
#include <fbjni/fbjni.h>

namespace facebook::react {

/** JNI peer that supplies RN 0.83's supported New Architecture JSI hook. */
class LuaModuleJSIBindings final : public jni::JavaClass<LuaModuleJSIBindings> {
 public:
  static constexpr const char* kJavaDescriptor =
      "Lcom/reactnativelua/LuaModule;";

  static void registerNatives() {
    javaClassLocal()->registerNatives({
        makeNativeMethod("getBindingsInstaller", getBindingsInstaller),
    });
  }

 private:
  static jni::local_ref<BindingsInstallerHolder::javaobject>
  getBindingsInstaller(jni::alias_ref<LuaModuleJSIBindings>) {
    return BindingsInstallerHolder::newObjectCxxArgs(
        [](jsi::Runtime& runtime, const std::shared_ptr<CallInvoker>& callInvoker) {
          SKRNNativeLua::install(runtime, callInvoker);
        });
  }
};

} // namespace facebook::react

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  return facebook::jni::initialize(vm, [] {
    facebook::react::LuaModuleJSIBindings::registerNatives();
  });
}
