#!/bin/bash
set -e

src="$(dirname "$0")/.."
build="$src/build"

rm -rf $build

CC=gcc CXX=g++ cmake --preset dev -B $build/linux-gcc-dev -S $src
CC=gcc CXX=g++ cmake --preset release -B $build/linux-gcc-release -S $src

CC=clang CXX=clang++ cmake --preset dev -B $build/linux-clang-dev -S $src
CC=clang CXX=clang++ cmake --preset release -B $build/linux-clang-release -S $src

toolchain="$src/cmake/toolchain-x86_64-pc-windows-clang.cmake"
cmake -DCMAKE_TOOLCHAIN_FILE=$toolchain --preset dev -B $build/win-clang-dev -S $src
cmake -DCMAKE_TOOLCHAIN_FILE=$toolchain --preset release -B $build/win-clang-release -S $src
