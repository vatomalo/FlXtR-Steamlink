#!/usr/bin/env bash
# Standalone experimental AI core proxy. No writes to installed RetroArch.
set -euo pipefail
cd "$(dirname "$0")/.."
sdk=${STEAMLINK_SDK_PATH:-/opt/steamlink-sdk}
set +u
source "$sdk/setenv.sh"
set -u
mkdir -p build/som-ai
$CC -Os -std=c99 -Wall -Wextra -Werror -fPIC -shared \
  src/som_ai_proxy.c -o build/som-ai/som_ai_proxy_libretro.so -ldl
"$STRIP" build/som-ai/som_ai_proxy_libretro.so
file build/som-ai/som_ai_proxy_libretro.so
sha256sum build/som-ai/som_ai_proxy_libretro.so
echo "Staged only: build/som-ai/som_ai_proxy_libretro.so"
echo "DO NOT replace the existing RetroArch binary or SNES core."
