# Bundled LuaSocket

This directory was recovered from react-native-lua commit `0059f00` and the
`origin/luasocket` branch. It contains LuaSocket 3.0-rc1 C sources plus the
3.0-rc2-era Lua modules embedded by that branch. LuaSocket is MIT licensed;
the source headers retain the upstream Diego Nehab copyright notices.

The resurrection port keeps the modules compiled into each native binary,
updates the embedded loaders for Lua 5.4, limits module lookup to in-memory
preloads, accounts large datagram receive buffers through Lua's allocator, and
integrates blocking waits with react-native-lua cancellation/deadline hooks.

`serial.c` is retained only as part of the recovered upstream source snapshot.
It is deliberately excluded from Android, iOS, and native-test builds: exposing
serial devices would cross react-native-lua's filesystem boundary and is not
part of the documented LuaSocket networking surface. TCP, UDP, DNS, and Unix
domain sockets remain available when networking is enabled.
