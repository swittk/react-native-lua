#import <React/RCTBridgeModule.h>

#ifdef __cplusplus

#import "react-native-lua.h"

#endif

@interface SKNativeLua : NSObject <RCTBridgeModule>
@property (nonatomic, assign) BOOL setBridgeOnMainQueue;
@end
