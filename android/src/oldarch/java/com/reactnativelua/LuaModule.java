package com.reactnativelua;

import androidx.annotation.NonNull;

import com.facebook.react.bridge.JavaScriptContextHolder;
import com.facebook.react.bridge.Promise;
import com.facebook.react.bridge.ReactApplicationContext;
import com.facebook.react.bridge.ReactContextBaseJavaModule;
import com.facebook.react.bridge.ReactMethod;
import com.facebook.react.module.annotations.ReactModule;
import com.facebook.react.turbomodule.core.CallInvokerHolderImpl;

/** Legacy bridge module used by the RN 0.73.6 Monterey-compatible example. */
@ReactModule(name = LuaModule.NAME)
public final class LuaModule extends ReactContextBaseJavaModule {
  public static final String NAME = "SKNativeLua";

  static {
    System.loadLibrary("SKRNNativeLua");
  }

  private final ReactApplicationContext reactContext;

  public LuaModule(ReactApplicationContext reactContext) {
    super(reactContext);
    this.reactContext = reactContext;
  }

  private static native double nativeMultiply(double a, double b);

  private static native void installLegacy(
      long runtimePointer,
      CallInvokerHolderImpl callInvokerHolder);

  private static native void cleanupLegacy(long runtimePointer);

  @NonNull
  @Override
  public String getName() {
    return NAME;
  }

  /** Bridge smoke-test method shared with the generated New Architecture spec. */
  @ReactMethod
  public void multiply(double a, double b, Promise promise) {
    promise.resolve(nativeMultiply(a, b));
  }

  /** Installs the shared HostObject factory once the legacy Catalyst runtime exists. */
  @SuppressWarnings("deprecation")
  @Override
  public void initialize() {
    super.initialize();
    JavaScriptContextHolder jsContext = reactContext.getJavaScriptContextHolder();
    CallInvokerHolderImpl holder =
        (CallInvokerHolderImpl) reactContext.getCatalystInstance().getJSCallInvokerHolder();
    if (jsContext.get() != 0 && holder != null) {
      installLegacy(jsContext.get(), holder);
    }
  }

  /** Removes the legacy global before Catalyst destroys its runtime. */
  @SuppressWarnings("deprecation")
  @Override
  public void onCatalystInstanceDestroy() {
    JavaScriptContextHolder jsContext = reactContext.getJavaScriptContextHolder();
    if (jsContext.get() != 0) {
      cleanupLegacy(jsContext.get());
    }
    super.onCatalystInstanceDestroy();
  }
}
