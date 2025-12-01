$ErrorActionPreference = "Stop"

$build="$PSScriptRoot/../build"
$src="$PSScriptRoot/.."

if (Test-Path $build) {
  Remove-Item $build
}
mkdir $build

$env:CC="cl.exe"
$env:CXX="cl.exe"
cmake -B $build/win-msvc-dev -S $src --preset dev
if ($LastExitCode -ne 0) { return }
cmake -B $build/win-msvc-release -S $src --preset release
if ($LastExitCode -ne 0) { return }

$toolchain="$src/cmake/toolchain-x86_64-pc-windows-clang.cmake"
cmake -DCMAKE_TOOLCHAIN_FILE="$toolchain" -B $build/win-clang-dev -S $src --preset dev
if ($LastExitCode -ne 0) { return }
cmake -DCMAKE_TOOLCHAIN_FILE="$toolchain" -B $build/win-clang-release -S $src --preset release
if ($LastExitCode -ne 0) { return }

$wsl_build=wsl -d ren-build wslpath -a $build.Replace('\', '\\')
if ($LastExitCode -ne 0) { return }
$wsl_src=wsl -d ren-build wslpath -a $src.Replace('\', '\\')
if ($LastExitCode -ne 0) { return }

wsl -d ren-build CC=gcc CXX=g++ cmake --preset dev -B $wsl_build/linux-gcc-dev -S $wsl_src
if ($LastExitCode -ne 0) { return }
wsl -d ren-build CC=gcc CXX=g++ cmake --preset release -B $wsl_build/linux-gcc-release -S $wsl_src
if ($LastExitCode -ne 0) { return }

wsl -d ren-build CC=clang CXX=clang++ cmake --preset dev -B $wsl_build/linux-clang-dev -S $wsl_src
if ($LastExitCode -ne 0) { return }
wsl -d ren-build CC=clang CXX=clang++ cmake --preset release -B $wsl_build/linux-clang-release -S $wsl_src
if ($LastExitCode -ne 0) { return }
