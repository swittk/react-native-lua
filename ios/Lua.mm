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

/** Waits for RCTCxxBridge to publish its runtime, then mutates it on the JS queue. */
- (void)installLegacyBindingsWhenReady
{
  RCTBridge *bridge = self.bridge;
  if (bridge == nil) {
    return;
  }

  RCTBridge *runtimeBridge = [bridge isKindOfClass:[RCTCxxBridge class]]
      ? bridge
      : bridge.batchedBridge;
  if (![runtimeBridge isKindOfClass:[RCTCxxBridge class]]) {
    return;
  }

  RCTCxxBridge *cxxBridge = (RCTCxxBridge *)runtimeBridge;
  if (cxxBridge.runtime == nullptr) {
    __weak SKNativeLua *weakSelf = self;
    dispatch_after(
        dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_MSEC)),
        dispatch_get_main_queue(), ^{
          [weakSelf installLegacyBindingsWhenReady];
        });
    return;
  }

  __weak SKNativeLua *weakSelf = self;
  [cxxBridge dispatchBlock:^{
    SKNativeLua *strongSelf = weakSelf;
    if (strongSelf == nil || strongSelf.bridge == nil) {
      return;
    }
    auto *runtime = reinterpret_cast<jsi::Runtime *>(cxxBridge.runtime);
    if (runtime != nullptr) {
      SKRNNativeLua::install(*runtime, nullptr);
    }
  } queue:RCTJSThread];
}

RCT_EXPORT_BLOCKING_SYNCHRONOUS_METHOD(installBindings)
{
  RCTBridge *bridge = self.bridge;
  RCTBridge *runtimeBridge = [bridge isKindOfClass:[RCTCxxBridge class]]
      ? bridge
      : bridge.batchedBridge;
  if (![runtimeBridge isKindOfClass:[RCTCxxBridge class]]) {
    return @NO;
  }

  RCTCxxBridge *cxxBridge = (RCTCxxBridge *)runtimeBridge;
  auto *runtime = reinterpret_cast<jsi::Runtime *>(cxxBridge.runtime);
  if (runtime == nullptr) {
    return @NO;
  }
  SKRNNativeLua::install(*runtime, nullptr);
  return @YES;
}

/** The bridge owns the runtime and destroys its globals during invalidation. */
- (void)invalidate
{
  _bridge = nil;
}

#endif

@end
