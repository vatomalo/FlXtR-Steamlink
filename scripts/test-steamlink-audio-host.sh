#!/usr/bin/env bash
# Run after build-retroarch-audio.sh has prepared the pinned frontend source.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
flags=(-std=c99 -Wall -Wextra -Werror)
cc "${flags[@]}" $(pkg-config --cflags sdl2) -Isrc src/steamlink_sdl_audio.c \
    tests/steamlink_sdl_audio_test.c -o build/steamlink-sdl-audio-test $(pkg-config --libs sdl2)
SDL_AUDIODRIVER=dummy build/steamlink-sdl-audio-test
cc "${flags[@]}" $(pkg-config --cflags sdl2) -Ibuild/retroarch-audio-src/libretro-common/include \
    tests/retroarch_audio_adapter_test.c -o build/retroarch-audio-adapter-test $(pkg-config --libs sdl2)
SDL_AUDIODRIVER=dummy build/retroarch-audio-adapter-test
cc "${flags[@]}" -shared -fPIC tools/steamlink_audio_test_core.c \
    -o build/steamlink-audio-test-core-host.so -lm
python3 tests/steamlink_audio_core_test.py
cc -Os "${flags[@]}" src/catalog.c -o build/greenlink-catalog \
    $(pkg-config --cflags --libs sdl2 SDL2_image libcurl json-c openssl)
python3 tests/queued_audio_launch_test.py
