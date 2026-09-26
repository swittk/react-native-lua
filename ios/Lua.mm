#import "Lua.h"

#import "react-native-lua.h"
#import <React/RCTUtils.h>

#ifdef RCT_NEW_ARCH_ENABLED
#import <ReactCommon/RCTTurboModule.h>
#else
#import <React/RCTBridge+Private.h>
#endif

using namespace facebook;

@implementation SKNativeLua

RCT_EXPORT_MODULE()

+ (BOOL)requiresMainQueueSetup
{
  // Legacy installation reads the bridge runtime and must run with bridge setup.
  return YES;
}

RCT_EXPORT_METHOD(multiply:(double)a
                  b:(double)b
                  resolve:(RCTPromiseResolveBlock)resolve
                  reject:(RCTPromiseRejectBlock)reject)
{
  resolve(@(SKRNNativeLua::multiply(a, b)));
}

#ifdef RCT_NEW_ARCH_ENABLED

/** Supplies the generated TurboModule implementation for RN's module registry. */
- (std::shared_ptr<react::TurboModule>)getTurboModule:
    (const react::ObjCTurboModule::InitParams &)params
{
  return std::make_shared<react::NativeLuaSpecJSI>(params);
}

/** Installs the shared HostObject factory through the supported New Architecture hook. */
- (void)installJSIBindingsWithRuntime:(jsi::Runtime &)runtime
                          callInvoker:(const std::shared_ptr<react::CallInvoker> &)callInvoker
{
  SKRNNativeLua::install(runtime, callInvoker);
}

#else

@synthesize bridge = _bridge;

/** Installs the same HostObject factory through the RN 0.73 legacy bridge. */
- (void)setBridge:(RCTBridge *)bridge
{
  _bridge = bridge;
  [self installLegacyBindingsWhenReady];
}

/** Waits for RCTCxxBridge to publish its JSI runtime during bridge startup/reload. */
- (void)installLegacyBindingsWhenReady
{
  RCTCxxBridge *cxxBridge = (RCTCxxBridge *)self.bridge;
  if (cxxBridge.runtime == nullptr) {
    __weak SKNativeLua *weakSelf = self;
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_MSEC)),
        dispatch_get_main_queue(), ^{
          [weakSelf installLegacyBindingsWhenReady];
        });
    return;
  }

  auto *runtime = reinterpret_cast<jsi::Runtime *>(cxxBridge.runtime);
  SKRNNativeLua::install(*runtime, nullptr);
}

/** Removes the legacy runtime global before the bridge tears down. */
- (void)invalidate
{
  RCTCxxBridge *cxxBridge = (RCTCxxBridge *)self.bridge;
  if (cxxBridge.runtime != nullptr) {
    auto *runtime = reinterpret_cast<jsi::Runtime *>(cxxBridge.runtime);
    SKRNNativeLua::cleanup(*runtime);
  }
}

#endif

@end
