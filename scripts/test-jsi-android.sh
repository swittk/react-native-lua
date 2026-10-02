#!/usr/bin/env bash
# Real Hermes/JSI HostObject tests, without an APK/Metro or any UI/app changes.
set -euo pipefail
: "${ANDROID_SERIAL:?Set an explicit dedicated test device/emulator serial}"
: "${ANDROID_NDK_HOME:?Set the installed NDK directory}"
: "${RNLUA_REACT_AAR:?Set a cached react-android release AAR}"
: "${RNLUA_HERMES_AAR:?Set its matching hermes-android release AAR}"
: "${RNLUA_FBJNI_AAR:?Set the matching fbjni AAR}"
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d)"
remote="/data/local/tmp/rnlua-bulk-jsi-$$"
cleanup() {
  adb -s "$ANDROID_SERIAL" shell rm -rf "$remote" >/dev/null 2>&1 || true
  rm -rf "$build_dir"
}
trap cleanup EXIT
adb -s "$ANDROID_SERIAL" get-state
abi="$(adb -s "$ANDROID_SERIAL" shell getprop ro.product.cpu.abi | tr -d '\r')"
case "$abi" in
  x86_64) triple=x86_64-linux-android; api=26 ;;
  arm64-v8a) triple=aarch64-linux-android; api=26 ;;
  *) echo "Test runner supports x86_64 and arm64-v8a only" >&2; exit 2 ;;
esac
cxx_standard="${RNLUA_CXX_STANDARD:-17}"
case "$cxx_standard" in 17|20) ;; *) echo "RNLUA_CXX_STANDARD must be 17 or 20" >&2; exit 2;; esac
case "$(uname -s)" in
  Linux) host_tag=linux-x86_64 ;;
  Darwin) host_tag=darwin-x86_64 ;;
  *) echo "Unsupported host OS for Android NDK toolchain" >&2; exit 2 ;;
esac
toolchain="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$host_tag"
cc="$toolchain/bin/${triple}${api}-clang"
cxx="$toolchain/bin/${triple}${api}-clang++"
mkdir -p "$build_dir/react" "$build_dir/hermes" "$build_dir/libs" "$build_dir/obj"
unzip -q "$RNLUA_REACT_AAR" "prefab/modules/jsi/include/*" "jni/$abi/*.so" -d "$build_dir/react"
unzip -q "$RNLUA_HERMES_AAR" "prefab/modules/libhermes/include/*" "jni/$abi/libhermes.so" -d "$build_dir/hermes"
cp "$build_dir/react/jni/$abi/"*.so "$build_dir/libs/"
cp "$build_dir/hermes/jni/$abi/libhermes.so" "$build_dir/libs/"
unzip -p "$RNLUA_FBJNI_AAR" "jni/$abi/libfbjni.so" > "$build_dir/libs/libfbjni.so"
cp "$toolchain/sysroot/usr/lib/$triple/libc++_shared.so" "$build_dir/libs/"
includes=(-I"$repo_dir/cpp" -I"$repo_dir/cpp/lua_src" -I"$repo_dir/cpp/lua_luasocket"
  -I"$repo_dir/cpp/lua_luasocket/luasocket_lua_amalgamation")
lua_sources=()
while IFS= read -r f; do lua_sources+=("$f"); done < <(
  find "$repo_dir/cpp/lua_src" -maxdepth 1 -name '*.c' ! -name lua.c ! -name luac.c | sort
  find "$repo_dir/cpp/lua_luasocket" -name '*.c' ! -name serial.c | sort
)
(cd "$build_dir/obj"; "$cc" -O2 -std=c11 -DLUA_USE_ANDROID=1 "${includes[@]}" -c "${lua_sources[@]}")
"$cxx" -O2 -std=c++"$cxx_standard" -fexceptions -frtti -DLUA_USE_ANDROID=1 "${includes[@]}" \
  -I"$build_dir/react/prefab/modules/jsi/include" \
  -I"$build_dir/hermes/prefab/modules/libhermes/include" \
  "$repo_dir/cpp/LuaRuntime.cpp" "$repo_dir/cpp/LuaSocket.cpp" \
  "$repo_dir/cpp/LuaValueReader.cpp" "$repo_dir/cpp/LuaValueJsi.cpp" \
  "$repo_dir/cpp/LuaValueInputJsi.cpp" "$repo_dir/cpp/LuaValueWriter.cpp" \
  "$repo_dir/cpp/react-native-lua.cpp" "$repo_dir/cpp/tests/LuaBulkJsiTest.cpp" \
  "$build_dir/obj"/*.o -L"$build_dir/libs" -lhermes -ljsi -llog -ldl -lm \
  -o "$build_dir/bulk-jsi-test"
adb -s "$ANDROID_SERIAL" shell mkdir -p "$remote"
adb -s "$ANDROID_SERIAL" push "$build_dir/libs/." "$remote/" >/dev/null
adb -s "$ANDROID_SERIAL" push "$build_dir/bulk-jsi-test" "$repo_dir/cpp/tests/LuaBulkReadTest.js" "$remote/" >/dev/null
adb -s "$ANDROID_SERIAL" shell "chmod 700 '$remote/bulk-jsi-test'; LD_LIBRARY_PATH='$remote' '$remote/bulk-jsi-test' '$remote/LuaBulkReadTest.js'"
