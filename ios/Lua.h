#import <Foundation/Foundation.h>

#ifdef RCT_NEW_ARCH_ENABLED
#import <NativeLuaSpec/NativeLuaSpec.h>
#import <ReactCommon/RCTTurboModuleWithJSIBindings.h>

/** New Architecture module backed by the generated NativeLua spec. */
@interface SKNativeLua : NSObject <NativeLuaSpec, RCTTurboModuleWithJSIBindings>
#else
#import <React/RCTBridgeModule.h>

/** Legacy bridge module used by the Monterey-compatible RN 0.73.6 example. */
@interface SKNativeLua : NSObject <RCTBridgeModule>
#endif

@end
