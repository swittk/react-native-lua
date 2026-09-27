#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
node_modules_dir="${REACT_NATIVE_NODE_MODULES_DIR:-$repo_dir/node_modules}"
gradle_command="${GRADLE_CMD:-}"

if [[ -z "$gradle_command" || -z "${REACT_NATIVE_GRADLE_PLUGIN_DIR:-}" ]]; then
  echo "Set GRADLE_CMD to a modern gradlew and REACT_NATIVE_GRADLE_PLUGIN_DIR to @react-native/gradle-plugin." >&2
  exit 2
fi
if [[ ! -f "$node_modules_dir/react-native/package.json" ]]; then
  echo "React Native was not found under $node_modules_dir" >&2
  exit 2
fi

created_node_modules_link=0
if [[ ! -e "$repo_dir/node_modules" ]]; then
  ln -s "$node_modules_dir" "$repo_dir/node_modules"
  created_node_modules_link=1
fi
cleanup() {
  if [[ "$created_node_modules_link" == "1" ]]; then
    unlink "$repo_dir/node_modules"
  fi
}
trap cleanup EXIT

"$gradle_command" --console=plain -p "$repo_dir/scripts" \
  -PnewArchEnabled=true \
  :react-native-lua-validation:compileDebugJavaWithJavac \
  :react-native-lua-validation:externalNativeBuildDebug

"$gradle_command" --console=plain -p "$repo_dir/scripts" \
  -PnewArchEnabled=false \
  :react-native-lua-validation:compileDebugJavaWithJavac \
  :react-native-lua-validation:externalNativeBuildDebug
