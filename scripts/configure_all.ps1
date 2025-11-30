$ErrorActionPreference = "Stop"
$build="$PSScriptRoot/../build"
$src="$PSScriptRoot/.."

Remove-Item $build
mkdir $build
$Env:CC="cl.exe"
$Env:CXX="cl.exe"
cmake -G Ninja -B $build/msvc-dev -S $src --preset dev
if($LastExitCode -ne 0) { return }
cmake -G Ninja -B $build/msvc-release -S $src --preset release
if($LastExitCode -ne 0) { return }

$toolchain_file="$src/cmake/toolchain-x86_64-pc-windows-clang.cmake"
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" -B $build/clang-dev -S $src --preset dev
if($LastExitCode -ne 0) { return }
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="$toolchain_file" -B $build/clang-release -S $src --preset release