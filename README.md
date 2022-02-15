# react-native-lua

Native Lua in React Native
Heavily based on React Native JSI 😅.
Working in both iOS and Android.

Lots of inspiration from [ObjC-Lua](https://github.com/PedestrianSean/ObjC-Lua) and [ilua](https://github.com/profburke/ilua).

The Lua source version (as of February 2022) is `5.4.4`.
- Minimally modified, simply prevented MakeFile from being detected and commented out `os_execute` from `loslib.c` to prevent calls to `system(cmd)` from occuring (unavailable on iOS).

## Installation

```sh
npm install react-native-lua
```

#### iOS
```
cd ios && pod install
```
#### Android
You need NDK installed. That's it.

## Usage

Methods can be seen in the types.

Almost all methods are simply the same as known C api methods of the pattern `lua_${methodName}`, sans the `lua_` prefix.

```js
import { luaInterpreter } from "react-native-lua";
// Create a new interpreter
const interp = luaInterpreter();
// this is equivalent to lua_dostring
interp.dostring(`a = 2
b = a ^ 2
a = b * 20
    `);
// The rest is up to you!
```

## Contributing

See the [contributing guide](CONTRIBUTING.md) to learn how to contribute to the repository and the development workflow.

## License

MIT
