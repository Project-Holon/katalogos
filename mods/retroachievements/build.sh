#!/bin/sh
# Builds mod/retroachievements.wasm with clang's wasm32 target: the mod
# (src/ra_mod.c), RetroAchievements' rcheevos (src/vendor/rcheevos, MIT;
# its public headers are in include/) and the mod C library (src/libc).
# CLANG names the compiler (default: clang); the CI uses the distribution's
# clang and lld.
set -e
cd "$(dirname "$0")"
"${CLANG:-clang}" --target=wasm32 -O2 -nostdlib -ffreestanding -fno-builtin \
    -Wall -Wextra -Werror -Iinclude -Isrc \
    -Wl,--no-entry -Wl,--export-table -Wl,--strip-all -Wl,-z,stack-size=65536 \
    -o mod/retroachievements.wasm \
    src/ra_mod.c src/ra_host.c src/libc/*.c src/vendor/rcheevos/*.c src/vendor/rcheevos/rcheevos/*.c \
    src/vendor/rcheevos/rapi/*.c src/vendor/rcheevos/rhash/md5.c
echo "built mod/retroachievements.wasm"
