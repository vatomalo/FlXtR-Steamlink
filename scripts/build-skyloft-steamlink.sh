#!/usr/bin/env bash
set -euo pipefail
storage=/opt/dockers/storage
project=$storage/projects/FlXtR
repo=$(cd "$(dirname "$0")/.." && pwd)
mountpoint -q "$storage"
mkdir -p "$storage/toolchains/steamlink-sdk" "$storage/build-cache/steamlink" "$storage/build-home/steamlink"
if [[ ${FLXTR_BUILD_LOCK_HELD:-0} != 1 ]]; then
    exec 6>"$storage/build-home/flxtr-build.lock"
    flock -n 6 || { echo 'Another FlXtR build is running.' >&2; exit 1; }
fi
exec 9>"$storage/build-home/steamlink/build.lock"
flock -n 9 || { echo 'A Steam Link build is already running.' >&2; exit 1; }
docker build --progress=plain -f "$project/Dockerfile.steamlink" -t flxtr-steamlink-builder:skyloft "$project"
sdk=$storage/toolchains/steamlink-sdk
if [[ ! -f "$sdk/setenv.sh" ]]; then
    docker run --rm --user "$(id -u):$(id -g)" -v "$sdk:/sdk" \
        flxtr-steamlink-builder:skyloft bash -c 'set -e; git init /sdk; git -C /sdk remote add origin https://github.com/ValveSoftware/steamlink-sdk.git; git -C /sdk fetch --depth 1 origin 62b4d098d1472c3534dd098ca2a0e0e10712f1c6; git -C /sdk checkout --detach FETCH_HEAD'
fi
cd "$repo"
if [[ ! -f build/music-runtime.tgz ]]; then bash scripts/build-music-runtime.sh; fi
rm -f dist/steamlink/apps/greenlink/.no-auto-update
docker run --rm --name flxtr-steamlink-build --cpus=3 --memory=5g \
    --user "$(id -u):$(id -g)" -e HOME=/build-home -e JOBS=3 \
    -e FLXTR_SOURCE_DIR=/src -e GREENLINK_BUILD_CACHE=/var/cache/flxtr-steamlink \
    -e FLXTR_DISABLE_AUTO_UPDATE=0 \
    -v "$repo:/src" -v "$sdk:/opt/steamlink-sdk" \
    -v "$storage/build-cache/steamlink:/var/cache/flxtr-steamlink" \
    -v "$storage/build-home/steamlink:/build-home" \
    flxtr-steamlink-builder:skyloft bash scripts/autocompile --steamlink --no-update
echo 'Complete Steam Link package built on SkyLoft.'
