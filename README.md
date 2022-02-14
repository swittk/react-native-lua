# react-native-lua

Native Lua in React Native (WIP)
Heavily based on React Native JSI 😅.

Lots of inspiration from [ObjC-Lua](https://github.com/PedestrianSean/ObjC-Lua) and [ilua](https://github.com/profburke/ilua).

The Lua source version (as of February 2022) is `5.4.4`.
- Minimally modified, simply prevented MakeFile from being detected and commented out `os_execute` from `loslib.c` to prevent calls to `system(cmd)` from occuring (unavailable on iOS).

## Installation

```sh
npm install react-native-lua
```

## Usage

```js
import { multiply } from "react-native-lua";

// ...

const result = await multiply(3, 7);
```

## Contributing

See the [contributing guide](CONTRIBUTING.md) to learn how to contribute to the repository and the development workflow.

## License

MIT
