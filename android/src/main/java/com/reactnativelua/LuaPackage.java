package com.reactnativelua;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

import com.facebook.react.TurboReactPackage;
import com.facebook.react.bridge.NativeModule;
import com.facebook.react.bridge.ReactApplicationContext;
import com.facebook.react.module.model.ReactModuleInfo;
import com.facebook.react.module.model.ReactModuleInfoProvider;
import com.facebook.react.uimanager.ViewManager;

import java.util.Collections;
import java.util.List;

/**
 * Registers SKNativeLua for both React Native registries. New Architecture
 * builds receive the generated-spec LuaModule source; legacy builds receive
 * the bridge LuaModule source selected by build.gradle.
 */
public final class LuaPackage extends TurboReactPackage {
  @Nullable
  @Override
  public NativeModule getModule(
      @NonNull String name,
      @NonNull ReactApplicationContext reactContext) {
    return LuaModule.NAME.equals(name) ? new LuaModule(reactContext) : null;
  }

  @NonNull
  @Override
  public ReactModuleInfoProvider getReactModuleInfoProvider() {
    return () -> Collections.singletonMap(
        LuaModule.NAME,
        new ReactModuleInfo(
            LuaModule.NAME,
            LuaModule.class.getName(),
            false,
            false,
            false,
            false,
            BuildConfig.IS_NEW_ARCHITECTURE_ENABLED));
  }

  @NonNull
  @Override
  public List<NativeModule> createNativeModules(
      @NonNull ReactApplicationContext reactContext) {
    return Collections.singletonList(new LuaModule(reactContext));
  }

  @NonNull
  @Override
  public List<ViewManager> createViewManagers(
      @NonNull ReactApplicationContext reactContext) {
    return Collections.emptyList();
  }
}
