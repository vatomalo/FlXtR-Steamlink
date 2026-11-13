#!/usr/bin/env bash
# Standalone experimental AI core proxy. No writes to installed RetroArch.
set -euo pipefail
cd "$(dirname "$0")/.."
sdk=${STEAMLINK_SDK_PATH:-}
if [[ -z "$sdk" ]]; then
  # Docker builds mount the SDK at /opt/steamlink-sdk; SkyLoft keeps it in
  # the shared toolchain store used by build-skyloft-steamlink.sh.
  for candidate in /opt/steamlink-sdk /opt/dockers/storage/toolchains/steamlink-sdk; do
    if [[ -f "$candidate/setenv.sh" ]]; then sdk=$candidate; break; fi
  done
fi
if [[ -z "$sdk" || ! -f "$sdk/setenv.sh" ]]; then
  echo "Steam Link SDK not found. Set STEAMLINK_SDK_PATH to the folder with setenv.sh," >&2
  echo "or run scripts/build-skyloft-steamlink.sh once to download it." >&2
  exit 1
fi
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
