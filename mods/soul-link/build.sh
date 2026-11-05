#!/bin/sh
# Builds mod/soul_link.wasm from src/ with clang's wasm32 target (no libc).
# CLANG names the compiler (default: clang); the CI uses the distribution's
# clang and lld.
set -e
cd "$(dirname "$0")"
"${CLANG:-clang}" --target=wasm32 -O2 -nostdlib -ffreestanding \
    -Wall -Wextra -Werror \
    -Wl,--no-entry -Wl,--export-table -Wl,--strip-all \
    -o mod/soul_link.wasm src/soul_link.c
echo "built mod/soul_link.wasm"
