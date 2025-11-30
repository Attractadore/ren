#!/bin/bash
set -e
cargo install xwin --version 0.6.7 --locked
src=$(dirname "$0")/..
msvc="$src/msvc"
rm -rf $msvc
~/.cargo/bin/xwin --accept-license --cache-dir $src/.cache/xwin splat --include-debug-libs --include-debug-symbols --preserve-ms-arch-notation --output $msvc
