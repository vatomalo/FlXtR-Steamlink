#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
sdk=${STEAMLINK_SDK_PATH:-/opt/steamlink-sdk}
# Valve's environment setup references optional unset variables.
set +u
source "$sdk/setenv.sh"
set -u
mkdir -p build dist/steamlink/apps/greenlink
version=$(git rev-parse HEAD)
$CC -Os -std=c99 -Wall -Wextra -Werror -DFLXTR_VERSION=\"$version\" $(pkg-config --cflags sdl2) src/shell.c -o build/greenlink-arm $(pkg-config --libs sdl2) -lSDL2_image -lcurl
$STRIP build/greenlink-arm
$CC -Os -std=c99 -Wall -Wextra -Werror -DFLXTR_VERSION=\"$version\" $(pkg-config --cflags sdl2) src/catalog.c -o build/greenlink-catalog-arm -lcurl -ljson-c -lcrypto -lSDL2_image $(pkg-config --libs sdl2)
$STRIP build/greenlink-catalog-arm
app=dist/steamlink/apps/greenlink
if [ ! -f build/cacert.pem ]; then
    wget -q https://curl.se/ca/cacert-2026-09-25.pem -O build/cacert.pem
fi
echo 'a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505  build/cacert.pem' | sha256sum -c -
mkdir -p "$app/certs"
cp build/cacert.pem "$app/certs/cacert.pem"
cp packaging/CERTIFICATES.txt "$app/certs/NOTICE.txt"
cp build/greenlink-arm "$app/greenlink"
cp build/greenlink-catalog-arm "$app/greenlink-catalog"
cp packaging/greenlink.sh packaging/update.sh packaging/controller-idle.sh packaging/toc.txt "$app/"
cp catalog.tsv "$app/catalog.tsv"
# RetroArch 1.7.x predates the firmware's SDL2-to-SDL3 compatibility layer.
# Pin Valve's native SDL2 build and load it only in the RetroArch child.
sdl=build/retroarch-libs/libSDL2-2.0.so.0
mkdir -p build/retroarch-libs "$app/retroarch-libs"
if [ ! -f "$sdl" ]; then
    wget -q --tries=3 --timeout=120 \
        https://raw.githubusercontent.com/ValveSoftware/steamlink-sdk/62b4d098d1472c3534dd098ca2a0e0e10712f1c6/rootfs/usr/lib/libSDL2-2.0.so.0.3100.0 -O "$sdl"
fi
echo "c9d19c7ae28243b3739e2f14a4f19cdd48646bed063b26607d25b19dbc148932  $sdl" | sha256sum -c -
cp "$sdl" "$app/retroarch-libs/"
cp packaging/SDL2-LICENSE.txt "$app/retroarch-libs/"

if [ -d assets ]; then cp -R assets "$app/"; fi
if [ -f build/greenlink-player-arm ]; then cp build/greenlink-player-arm "$app/greenlink-player"; fi
if [ -f build/greenlink-resolver-arm ]; then
    cp build/greenlink-resolver-arm "$app/greenlink-resolver"
    cp build/resolver/WABT-LICENSE.txt "$app/WABT-LICENSE.txt"
fi
# Optional side-by-side cores from scripts/build-cores.sh. No system core is replaced.
for core in snes9x2005_flxtr_libretro.so mame2003_plus_flxtr_libretro.so; do
    if [ -f "build/cores/$core" ]; then
        mkdir -p "$app/cores"
        cp "build/cores/$core" "$app/cores/"
    fi
done
if [ -d "$app/cores" ]; then
    for notice in SNES9X-LICENSE.txt MAME2003-PLUS-LICENSE.md snes9x2005-build.txt mame2003-plus-build.txt; do
        cp "build/cores/$notice" "$app/cores/"
    done
fi
if [ -f build/greenlink-audio-arm ]; then cp build/greenlink-audio-arm "$app/greenlink-audio"; fi
if [ -f build/music-runtime.tgz ]; then
    mkdir -p "$app/music-runtime" "$app/tools" "$app/scripts"
    tar -xzf build/music-runtime.tgz -C "$app/music-runtime"
    cp tools/music.py "$app/tools/"
    cp scripts/music-runtime.sh scripts/music-qjs.sh "$app/scripts/"
    chmod +x "$app/scripts/music-runtime.sh" "$app/scripts/music-qjs.sh"
    printf '1\n' > "$app/assets/music-required-v1"
fi
if [ -f build/player-menu-v1 ]; then cp build/player-menu-v1 "$app/player-menu-v1"; fi
if [ "${FLXTR_DISABLE_AUTO_UPDATE:-0}" = 1 ]; then
    printf 'Experimental manual test package; enable updates in Settings if desired.\n' > "$app/.no-auto-update"
fi
cp docs/DESKTOP-CONTROLS.md "$app/CONTROLS.md"
cp docs/STEAMLINK-PLAYBACK.md "$app/PLAYBACK.md"
chmod +x "$app/greenlink" "$app/greenlink.sh"
tar -C dist/steamlink/apps -czf dist/greenlink.tgz greenlink
file build/greenlink-arm
wc -c build/greenlink-arm dist/greenlink.tgz

{ printf "version=%s\n" "$version"; printf "sha256=%s\n" "$(sha256sum build/greenlink-arm | cut -d " " -f1)"; } > dist/shell-manifest.txt
