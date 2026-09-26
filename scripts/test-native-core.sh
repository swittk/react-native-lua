#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT

lua_sources=()
while IFS= read -r source; do
  lua_sources+=("$source")
done < <(
  find "$repo_dir/cpp/lua_src" -maxdepth 1 -name '*.c' \
    ! -name 'lua.c' ! -name 'luac.c' -print | sort
)

luasocket_sources=()
while IFS= read -r source; do
  luasocket_sources+=("$source")
done < <(
  find "$repo_dir/cpp/lua_luasocket" -name '*.c' ! -name 'serial.c' -print | sort
)

c_feature_flags=(-D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L)
if [[ "$(uname -s)" == "Darwin" ]]; then
  c_feature_flags=(-D_DARWIN_C_SOURCE)
fi

(
  cd "$build_dir"
  cc -std=c11 -O2 "${c_feature_flags[@]}" \
    -I"$repo_dir/cpp" \
    -I"$repo_dir/cpp/lua_src" \
    -I"$repo_dir/cpp/lua_luasocket" \
    -I"$repo_dir/cpp/lua_luasocket/luasocket_lua_amalgamation" \
    -c "${lua_sources[@]}" "${luasocket_sources[@]}"
)

link_args=(-lm)
if [[ "$(uname -s)" != "Darwin" ]]; then
  link_args+=(-ldl)
fi

c++ -std=c++17 -O2 -pthread \
  -I"$repo_dir/cpp" -I"$repo_dir/cpp/lua_src" \
  -I"$repo_dir/cpp/lua_luasocket" \
  "$repo_dir/cpp/LuaRuntime.cpp" "$repo_dir/cpp/LuaSocket.cpp" \
  "$repo_dir/cpp/tests/LuaRuntimeTest.cpp" \
  "$build_dir"/*.o "${link_args[@]}" -o "$build_dir/lua-runtime-test"

"$build_dir/lua-runtime-test"
