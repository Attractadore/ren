$ErrorActionPreference = "Stop"

$build="$PSScriptRoot/../build"

cmake --build $build/msvc-dev
if ($LastExitCode -ne 0) { return }
cmake --build $build/msvc-release
if ($LastExitCode -ne 0) { return }

cmake --build $build/clang-dev
if ($LastExitCode -ne 0) { return }
cmake --build $build/clang-release
if ($LastExitCode -ne 0) { return }
