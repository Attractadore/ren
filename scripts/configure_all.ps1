$ErrorActionPreference = "Stop"

$build="$PSScriptRoot/../build"
$src="$PSScriptRoot/.."

if (Test-Path $build) {
  Remove-Item $build
}
mkdir $build

$Env:CC="cl.exe"
$Env:CXX="cl.exe"
cmake -B $build/msvc-dev -S $src --preset dev
if ($LastExitCode -ne 0) { return }
cmake -B $build/msvc-release -S $src --preset release
if ($LastExitCode -ne 0) { return }

$toolchain="$src/cmake/toolchain-x86_64-pc-windows-clang.cmake"
cmake -DCMAKE_TOOLCHAIN_FILE="$toolchain" -B $build/clang-dev -S $src --preset dev
if ($LastExitCode -ne 0) { return }
cmake -DCMAKE_TOOLCHAIN_FILE="$toolchain" -B $build/clang-release -S $src --preset release
if ($LastExitCode -ne 0) { return }
