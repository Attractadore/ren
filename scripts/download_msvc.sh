#!/bin/bash
set -e
cargo install xwin --version 0.6.7 --locked
msvc=$(dirname "$0")/../msvc
rm -rf $msvc
xwin --accept-license splat --include-debug-libs --include-debug-symbols --preserve-ms-arch-notation --output $msvc
