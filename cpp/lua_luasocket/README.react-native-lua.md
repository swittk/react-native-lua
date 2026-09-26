# Bundled LuaSocket

This directory was recovered from react-native-lua commit `0059f00` and the
`origin/luasocket` branch. It contains LuaSocket 3.0-rc1 C sources plus the
3.0-rc2-era Lua modules embedded by that branch. LuaSocket is MIT licensed;
the source headers retain the upstream Diego Nehab copyright notices.

The resurrection port keeps the modules compiled into each native binary,
updates the embedded loaders for Lua 5.4, limits module lookup to in-memory
preloads, accounts large datagram receive buffers through Lua's allocator, and
integrates blocking waits with react-native-lua cancellation/deadline hooks.
