#!/bin/bash
set -e

sudo apt update -y
sudo apt upgrade -y

sudo apt install -y build-essential ccache cmake ninja-build clang llvm libnuma-dev

if ! command -v wslinfo >/dev/null 2>&1; then
  # For xwin and clang-cl
  sudo apt install -y rustup clang-tools
  sudo update-alternatives --install /usr/bin/clang-cl clang-cl /usr/bin/clang-cl-18 0
  rustup toolchain install stable
  $(dirname "$0")/download_msvc.sh
fi

# For SDL3
sudo apt install -y pkg-config gnome-desktop-testing libasound2-dev libpulse-dev \
libaudio-dev libfribidi-dev libjack-dev libsndio-dev libx11-dev libxext-dev \
libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
libxkbcommon-dev libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev \
libegl1-mesa-dev libdbus-1-dev libibus-1.0-dev libudev-dev libthai-dev \
libpipewire-0.3-dev libwayland-dev libdecor-0-dev liburing-dev
