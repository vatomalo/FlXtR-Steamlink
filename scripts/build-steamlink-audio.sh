#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
sdk=${STEAMLINK_SDK_PATH:-/opt/steamlink-sdk}
set +u
source "$sdk/setenv.sh"
set -u
mkdir -p build
$CC -Os -std=c99 -Wall -Wextra -Werror $(pkg-config --cflags sdl2) -Isrc \
    src/steamlink_sdl_audio.c tools/test_steamlink_audio.c \
    -o build/test-steamlink-audio $(pkg-config --libs sdl2) -lm
$STRIP build/test-steamlink-audio
$CC -Os -std=c99 -Wall -Wextra -Werror -shared -fPIC tools/steamlink_audio_test_core.c \
    -o build/steamlink_audio_test_libretro.so -lm
$STRIP build/steamlink_audio_test_libretro.so
