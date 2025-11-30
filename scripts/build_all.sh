#!/bin/bash

set -e

src="$(dirname "$0")/.."
build="$src/build"

cmake --build $build/gcc-dev
cmake --build $build/gcc-release

cmake --build $build/clang-dev
cmake --build $build/clang-release

cmake --build $build/msvc-dev
cmake --build $build/msvc-release
