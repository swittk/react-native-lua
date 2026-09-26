package com.reactnativelua;

import androidx.annotation.NonNull;

import com.facebook.proguard.annotations.DoNotStrip;
import com.facebook.react.bridge.Promise;
import com.facebook.react.bridge.ReactApplicationContext;
import com.facebook.react.module.annotations.ReactModule;
import com.facebook.react.turbomodule.core.interfaces.BindingsInstallerHolder;
import com.facebook.react.turbomodule.core.interfaces.TurboModuleWithJSIBindings;

/** RN 0.83 TurboModule implementation backed by generated NativeLuaSpec. */
@DoNotStrip
@ReactModule(name = LuaModule.NAME)
public final class LuaModule extends NativeLuaSpec implements TurboModuleWithJSIBindings {
  public static final String NAME = "SKNativeLua";

  static {
    System.loadLibrary("SKRNNativeLua");
  }

  public LuaModule(ReactApplicationContext reactContext) {
    super(reactContext);
  }

  private static native double nativeMultiply(double a, double b);

  /** Lets RN install the shared HostObject factory with its runtime and CallInvoker. */
  @DoNotStrip
  @Override
  public native BindingsInstallerHolder getBindingsInstaller();

  @NonNull
  @Override
  public String getName() {
    return NAME;
  }

  /** Promise method declared by the generated spec and used as a link smoke test. */
  @Override
  public void multiply(double a, double b, Promise promise) {
    promise.resolve(nativeMultiply(a, b));
  }
}
