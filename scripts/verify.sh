#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if command -v cmake >/dev/null 2>&1; then
  cmake_bin="$(command -v cmake)"
else
  cmake_bin="$PWD/.build/tools/bin/cmake"
fi
ctest_bin="$(dirname "$cmake_bin")/ctest"
configure_args=()
if [[ -x "$PWD/.build/tools/bin/ninja" ]]; then
  configure_args+=("-DCMAKE_MAKE_PROGRAM=$PWD/.build/tools/bin/ninja")
fi
build_dir="$PWD/.build/core"
if [[ "${1:-}" == "--example" ]]; then
  build_dir="$PWD/.build/xlab"
  configure_args+=("-DXMAX_BUILD_EXAMPLE=ON")
  qt_prefix="${QT_ROOT_DIR:-${QTDIR:-$PWD/.build/qt/6.8.3/macos}}"
  configure_args+=("-DCMAKE_PREFIX_PATH=$qt_prefix")
fi
"$cmake_bin" -S . -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON "${configure_args[@]}"
"$cmake_bin" --build "$build_dir" --parallel 6
"$ctest_bin" --test-dir "$build_dir" --output-on-failure
