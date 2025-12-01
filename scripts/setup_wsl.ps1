$ErrorActionPreference = "Stop"

wsl --install -d Ubuntu-24.04 --name ren-build --no-launch
# Success or already exists.
if ($LastExitCode -ne 0 -and $LastExitCode -ne -1) { return }

$setup=wsl -d ren-build wslpath -a "$PSScriptRoot\setup_ubuntu_24_04.sh".Replace("\", "\\")
if ($LastExitCode -ne 0) { return }

wsl -d ren-build $setup
if ($LastExitCode -ne 0) { return }
