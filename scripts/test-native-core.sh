#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

mapfile -t lua_sources < <(
  find "$repo_dir/cpp/lua_src" -maxdepth 1 -name '*.c' \
    ! -name 'lua.c' ! -name 'luac.c' -print | sort
)
mapfile -t luasocket_sources < <(
  find "$repo_dir/cpp/lua_luasocket" -name '*.c' -print | sort
)

(
  cd "$build_dir"
  cc -std=c11 -O2 -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L \
    -I"$repo_dir/cpp" \
    -I"$repo_dir/cpp/lua_src" \
    -I"$repo_dir/cpp/lua_luasocket" \
    -I"$repo_dir/cpp/lua_luasocket/luasocket_lua_amalgamation" \
    -c "${lua_sources[@]}" "${luasocket_sources[@]}"
)
c++ -std=c++17 -O2 -pthread \
  -I"$repo_dir/cpp" -I"$repo_dir/cpp/lua_src" \
  -I"$repo_dir/cpp/lua_luasocket" \
  "$repo_dir/cpp/LuaRuntime.cpp" "$repo_dir/cpp/LuaSocket.cpp" \
  "$repo_dir/cpp/tests/LuaRuntimeTest.cpp" \
  "$build_dir"/*.o -lm -ldl -o "$build_dir/lua-runtime-test"
"$build_dir/lua-runtime-test"
