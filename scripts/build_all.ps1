$ErrorActionPreference = "Stop"

$build="$PSScriptRoot/../build"

cmake --build $build/win-msvc-dev
if ($LastExitCode -ne 0) { return }
cmake --build $build/win-msvc-release
if ($LastExitCode -ne 0) { return }

cmake --build $build/win-clang-dev
if ($LastExitCode -ne 0) { return }
cmake --build $build/win-clang-release
if ($LastExitCode -ne 0) { return }

$wsl_build=wsl -d ren-build wslpath -a $build.Replace('\', '\\')
if ($LastExitCode -ne 0) { return }

wsl -d ren-build cmake --build $wsl_build/linux-gcc-dev
if ($LastExitCode -ne 0) { return }
wsl -d ren-build cmake --build $wsl_build/linux-gcc-release
if ($LastExitCode -ne 0) { return }

wsl -d ren-build cmake --build $wsl_build/linux-clang-dev
if ($LastExitCode -ne 0) { return }
wsl -d ren-build cmake --build $wsl_build/linux-clang-release
if ($LastExitCode -ne 0) { return }
