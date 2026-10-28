#!/bin/bash
source /opt/steamlink-sdk/setenv.sh
set -e
cd /src
mkdir -p build
$CC -Os -std=c99 -Wall -Wextra -Werror $(pkg-config --cflags sdl2) src/shell.c -o build/greenlink-dice-arm $(pkg-config --libs sdl2) -lSDL2_image -lcurl
$CC -Os -std=c99 -Wall -Wextra -Werror $(pkg-config --cflags sdl2) src/catalog.c -o build/greenlink-catalog-dice-arm -lcurl -ljson-c -lcrypto -lSDL2_image $(pkg-config --libs sdl2)
$STRIP build/greenlink-dice-arm build/greenlink-catalog-dice-arm
