#!/bin/sh
# Builds mod/smart_trainers.wasm from src/ with clang's wasm32 target (no
# libc; src/st_libc.c has the memcpy and memset clang may call). CLANG names
# the compiler (default: clang); the CI uses the distribution's clang and lld.
set -e
cd "$(dirname "$0")"
"${CLANG:-clang}" --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin \
    -Wall -Wextra -Werror \
    -Wl,--no-entry -Wl,--export-table -Wl,--strip-all \
    -o mod/smart_trainers.wasm src/st_mod.c src/st_core.c src/st_libc.c
echo "built mod/smart_trainers.wasm"
