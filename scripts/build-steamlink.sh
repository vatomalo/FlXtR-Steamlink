#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
sdk=${STEAMLINK_SDK_PATH:-/opt/steamlink-sdk}
# Valve's environment setup references optional unset variables.
set +u
source "$sdk/setenv.sh"
set -u
mkdir -p build dist/steamlink/apps/greenlink
$CC -Os -std=c99 -Wall -Wextra -Werror $(pkg-config --cflags sdl2) src/shell.c -o build/greenlink-arm $(pkg-config --libs sdl2)
$STRIP build/greenlink-arm
app=dist/steamlink/apps/greenlink
cp build/greenlink-arm "$app/greenlink"
cp packaging/greenlink.sh packaging/toc.txt "$app/"
cp catalog.tsv "$app/catalog.tsv"
if [ -d assets ]; then cp -R assets "$app/"; fi
if [ -f build/greenlink-player-arm ]; then cp build/greenlink-player-arm "$app/greenlink-player"; fi
chmod +x "$app/greenlink" "$app/greenlink.sh"
tar -C dist/steamlink/apps -czf dist/greenlink.tgz greenlink
file build/greenlink-arm
wc -c build/greenlink-arm dist/greenlink.tgz
