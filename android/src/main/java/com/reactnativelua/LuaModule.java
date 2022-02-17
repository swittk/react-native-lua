package com.reactnativelua;

import androidx.annotation.NonNull;

import com.facebook.react.bridge.JavaScriptContextHolder;
import com.facebook.react.bridge.Promise;
import com.facebook.react.bridge.ReactApplicationContext;
import com.facebook.react.bridge.ReactContextBaseJavaModule;
import com.facebook.react.bridge.ReactMethod;
import com.facebook.react.module.annotations.ReactModule;
import com.facebook.react.turbomodule.core.CallInvokerHolderImpl;

@ReactModule(name = LuaModule.NAME)
public class LuaModule extends ReactContextBaseJavaModule {
    public static final String NAME = "SKNativeLua";

    static {
        try {
            // Used to load the 'native-lib' library on application startup.
            System.loadLibrary("SKRNNativeLua");
        } catch (Exception ignored) {
        }
    }

  private final ReactApplicationContext reactContext;

    public LuaModule(ReactApplicationContext reactContext) {
      super(reactContext);
      this.reactContext = reactContext;
    }

    public static native int nativeMultiply(int a, int b);

  private static native void initialize(long jsiRuntimePointer, CallInvokerHolderImpl jsCallInvokerHolder);

  private static native void cleanup(long jsiRuntimePointer);

    @Override
    @NonNull
    public String getName() {
        return NAME;
    }

    // Example method
    // See https://reactnative.dev/docs/native-modules-android
    @ReactMethod
    public void multiply(int a, int b, Promise promise) {
        promise.resolve(nativeMultiply(a, b));
    }

  // This method is called automatically (defined in BaseJavaModule.java)
  // "called on the appropriate method when a life cycle event occurs.
  @Override
  public void initialize() {
    ReactApplicationContext context = this.reactContext;
    JavaScriptContextHolder jsContext = context.getJavaScriptContextHolder();
    CallInvokerHolderImpl holder = (CallInvokerHolderImpl) context.getCatalystInstance().getJSCallInvokerHolder();
    LuaModule.initialize(jsContext.get(), holder);
  }

  // This method is called automatically (defined in BaseJavaModule.java)
  // "called on the appropriate method when a life cycle event occurs.
  // This method is equivalent to Objective-C's 'invalidate'
  @Override
  public void onCatalystInstanceDestroy() {
    LuaModule.cleanup(this.reactContext.getJavaScriptContextHolder().get());
    // FlexibleHttpModule.cleanup(this.getReactApplicationContext());
  }
}
