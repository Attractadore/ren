#!/bin/bash
set -e

src="$(dirname "$0")/.."
build="$src/build"

cmake --build $build/gcc-dev
cmake --build $build/gcc-release

cmake --build $build/clang-dev
cmake --build $build/clang-release

if [ -d "$src/msvc-dev" ]; then
  cmake --build $build/msvc-dev
fi
if [ -d "$src/msvc-release" ]; then
  cmake --build $build/msvc-release
fi
