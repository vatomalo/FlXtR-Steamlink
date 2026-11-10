#!/usr/bin/env bash
# Run in SkyLoft's Steam Link SDK container. All output stays on its extra disk.
set -euo pipefail
cd "$(dirname "$0")/.."
project=$PWD
rev=4113dd26be0907f2edf1805c7b9fd23766e17161
tree=${FLXTR_MOONLIGHT_SOURCE:-$project/build/moonlight-src}
sdk=${STEAMLINK_SDK_PATH:-/opt/steamlink-sdk}
mkdir -p "$project/build/moonlight" "$project/dist/core-sources"
if [[ ! -d "$tree/.git" ]]; then
    git init "$tree"
    git -C "$tree" remote add origin https://github.com/moonlight-stream/moonlight-qt.git
    git -C "$tree" fetch --depth 1 origin "$rev"
    git -C "$tree" checkout --detach FETCH_HEAD
fi
[[ $(git -C "$tree" rev-parse HEAD) == "$rev" ]] || { echo 'Unexpected Moonlight revision' >&2; exit 1; }
python3 scripts/patch-moonlight.py "$tree"
git -C "$tree" submodule update --init --recursive --depth 1
if [[ ! -f "$tree/libs/steamlink/lib/libopus.a" ]]; then
    (cd "$tree" && python3 setup-deps.py)
fi
set +u
source "$sdk/setenv.sh"
set -u
$CC -Os -std=c99 -Wall -Wextra -Werror $(pkg-config --cflags sdl2) \
    tools/moonlight_audio_probe.c -o "$project/build/moonlight/flxtr-audio-test" \
    $(pkg-config --libs sdl2) -lSLAudio -lm
mkdir -p "$tree/build/flxtr"
cd "$tree/build/flxtr"
qmake "$tree/moonlight-qt.pro" 'CONFIG+=release embedded' QMAKE_CFLAGS_ISYSTEM=
make -j"${JOBS:-3}" release
cp app/moonlight "$project/build/moonlight/moonlight"
$STRIP "$project/build/moonlight/moonlight" "$project/build/moonlight/flxtr-audio-test"
cp "$tree/LICENSE" "$project/build/moonlight/LICENSE"
printf 'upstream=%s\npatch=flxtr-audio-device-v2\n' "$rev" > "$project/build/moonlight/BUILD.txt"
# Corresponding source includes all checked-out submodules and our modification.
tar --exclude=.git --exclude=./build --exclude=./libs -czf \
    "$project/dist/core-sources/moonlight-flxtr-source.tar.gz" -C "$tree" .
cp "$project/scripts/build-moonlight.sh" "$project/scripts/patch-moonlight.py" "$project/build/moonlight/"
(cd "$project/build/moonlight" && sha256sum moonlight flxtr-audio-test > SHA256SUMS)
echo 'Bundled Moonlight and audio test built successfully.'
