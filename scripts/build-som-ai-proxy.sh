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
  src/som_ai_proxy.c -o build/som-ai/som_ai_proxy_libretro.so -ldl -lm
"$STRIP" build/som-ai/som_ai_proxy_libretro.so
file build/som-ai/som_ai_proxy_libretro.so
sha256sum build/som-ai/som_ai_proxy_libretro.so
echo "Staged only: build/som-ai/som_ai_proxy_libretro.so"
# The core makes its sword and hero-move art from the running ROM
# (src/som_art_gen.h): these packs are the reference (tools/test_som_art_gen.py)
# and a fallback. Only a pack with your own dagger picture needs installing.
# Never bundle or modify the ROM itself.
# Point SOM_ROM_PATH at your own Secret of Mana Turbo .smc/.sfc when building.
if [[ -n "${SOM_ROM_PATH:-}" ]]; then
  if [[ ! -f "$SOM_ROM_PATH" ]]; then
    echo "SOM_ROM_PATH not found: $SOM_ROM_PATH" >&2
    exit 1
  fi
  python3 tools/som_cartwheel.py "$SOM_ROM_PATH" \
    build/som-ai/som_cartwheel_frames.bin
  echo "Staged animation pack: build/som-ai/som_cartwheel_frames.bin"
  # Bushido sprite masks: a locally extracted SD3 dagger is optional.
  # Donor images and the ROM are never committed or bundled.
  dagger_args=()
  if [[ -n "${SOM_DAGGER_PNG:-}" ]]; then
    : "${SOM_DAGGER_HILT:?Set SOM_DAGGER_HILT=X,Y for the dagger PNG}"
    : "${SOM_DAGGER_TIP:?Set SOM_DAGGER_TIP=X,Y for the dagger PNG}"
    dagger_args=(--dagger-png "$SOM_DAGGER_PNG" --dagger-hilt "$SOM_DAGGER_HILT" --dagger-tip "$SOM_DAGGER_TIP")
  fi
  python3 tools/som_weapon_mask.py "$SOM_ROM_PATH" build/som-ai/som_weapon_masks.bin \
    --kusarigama "${dagger_args[@]}"
  echo "Staged Bushido and Kusarigama mask art: build/som-ai/som_weapon_masks.bin"
  : > build/som-ai/som_ai_weapon_masks_on
  if [[ ${#dagger_args[@]} -gt 0 ]]; then
    cp build/som-ai/som_weapon_masks.bin build/som-ai/som_weapon_masks_custom.bin
    echo "Your dagger: install build/som-ai/som_weapon_masks_custom.bin beside the proxy"
  fi
  python3 tools/som_weapon_preview.py "$SOM_ROM_PATH" \
    build/som-ai/som_weapon_masks.bin build/som-ai/som_weapon_preview.png
  echo "Review first: build/som-ai/som_weapon_preview.png (Bushido blades and Kusarigama)"
  echo "Missing frames listed in build/som-ai/som_weapon_preview.png.txt"
else
  echo "Ukemi/flying-kick animated pack not staged: set SOM_ROM_PATH to generate it"
fi
echo "DO NOT replace the existing RetroArch binary or SNES core."
