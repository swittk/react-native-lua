import {NativeModules, TurboModuleRegistry} from 'react-native';
import type {TurboModule} from 'react-native';

/** Native registration contract shared by legacy NativeModules and TurboModules. */
export interface Spec extends TurboModule {
  multiply(a: number, b: number): Promise<number>;
}

const NativeLua =
  TurboModuleRegistry.get<Spec>('SKNativeLua') ||
  (NativeModules.SKNativeLua as Spec | undefined);

export default NativeLua;
