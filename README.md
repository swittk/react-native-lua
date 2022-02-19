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
// Or asynchronously! Each Lua Interpreter spawns its own thread when executing async code, so this doesn't block any other processes.
interp.dostringasync(`i = 0
while(i < 8)
do
  print(i)
  sleep(100)
  i = i + 1
end`)

// The rest is up to you!
```

#### The interpreter in action
![the-interpreter-in-action](/docs/images/example-coroutine-async-demonstration.gif)

## Execution limits

Since Lua is a scripting language, it wouldn't be nice if our code suddenly got stuck in a forever loop and blocks the whole program.

This library handles this condition by leveraging Lua runtime's `lua_sethook` function (which allows us to monitor the code execution every N commands).

The property `executionLimit` defines the number of milliseconds the script should run until it is terminated. Default value is 10000 (10 seconds). You can set this value by calling `setExecutionLimit(ms)`

## Future plans
- Make async `dostringasync` and `dofileasync` work on Android
    - iOS `dostringasync` is working perfectly well, however, in Android it is only possible to use `dostring` since CallInvoker crashes immediately when InvokeAsync() is called. Any help in getting this working would be very much appreciated.

# Interesting read on people doing JSI development on React Native

- JSI module issues : https://github.com/facebook/react-native/issues/28128


## Contributing

See the [contributing guide](CONTRIBUTING.md) to learn how to contribute to the repository and the development workflow.

## License

MIT


---
##### Tip Jar

If you appreciate my work, help buy me some soda 🥤 via the following routes.

<img src="https://upload.wikimedia.org/wikipedia/commons/5/56/Stellar_Symbol.png" alt="Stellar" height="32"/>

```
Stellar Lumens (XLM) : 
GCVKPZQUDXWVNPIIMF3FXR6KWAOHTEWPZZM2AQE4J3TXR6ZDHXQHP5BQ
```

<img src="https://upload.wikimedia.org/wikipedia/commons/1/19/Coin-ada-big.svg" alt="Cardano" height="32">

```
Cardano (ADA) : 
addr1q9datt8urnyuc2059tquh59sva0pja7jqg4nfhnje7xcy6zpndeesglqkxhjvcgdu820flcecjzunwp6qen4yr92gm6smssug8
```
