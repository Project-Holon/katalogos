#!/bin/sh
# Builds mod/smart_trainers.wasm from src/ with clang's wasm32 target. The
# C functions clang may call (memcpy, memset) come from the mod C library in
# src/libc (morphe's modsdk/v7/libc; its README says where each file comes
# from). CLANG names the compiler (default: clang); the CI uses the
# distribution's clang and lld.
set -e
cd "$(dirname "$0")"
"${CLANG:-clang}" --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin \
    -Wall -Wextra -Werror -Iinclude -Isrc \
    -Wl,--no-entry -Wl,--export-table -Wl,--strip-all \
    -o mod/smart_trainers.wasm src/st_mod.c src/st_core.c src/libc/*.c
echo "built mod/smart_trainers.wasm"
