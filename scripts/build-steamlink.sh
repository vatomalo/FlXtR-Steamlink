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
$CC -Os -std=c99 -Wall -Wextra -Werror $(pkg-config --cflags sdl2) src/catalog.c -o build/greenlink-catalog-arm -lcurl -ljson-c -lcrypto -lSDL2_image $(pkg-config --libs sdl2)
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
if [ -f build/player-menu-v1 ]; then cp build/player-menu-v1 "$app/player-menu-v1"; fi
chmod +x "$app/greenlink" "$app/greenlink.sh"
tar -C dist/steamlink/apps -czf dist/greenlink.tgz greenlink
file build/greenlink-arm
wc -c build/greenlink-arm dist/greenlink.tgz

{ printf "version=%s\n" "$version"; printf "sha256=%s\n" "$(sha256sum build/greenlink-arm | cut -d " " -f1)"; } > dist/shell-manifest.txt
