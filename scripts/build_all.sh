#!/bin/bash
set -e

src="$(dirname "$0")/.."
build="$src/build"

cmake --build $build/linux-gcc-dev
cmake --build $build/linux-gcc-release

cmake --build $build/linux-clang-dev
cmake --build $build/linux-clang-release

cmake --build $build/win-clang-dev
cmake --build $build/win-clang-release
