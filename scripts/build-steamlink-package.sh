#!/usr/bin/env bash
# Run in regular WSL/Linux. All ARM compilation stays in a new disposable container.
set -euo pipefail
cd "$(dirname "$0")/.."
image=${FLXTR_STEAMLINK_IMAGE:-steamlink-sdk:latest}
command -v docker >/dev/null || { echo "Docker is required." >&2; exit 1; }
docker image inspect "$image" >/dev/null
if [[ ! -f build/music-runtime.tgz ]] || ! tar -tzf build/music-runtime.tgz | grep "yt_dlp_ejs/__init__.py" >/dev/null; then
    bash scripts/build-music-runtime.sh
fi
docker run --rm --mount "type=bind,src=$PWD,dst=/src" \
    --mount type=volume,src=flxtr-steamlink-build-cache,dst=/var/cache/flxtr-steamlink \
    --env FLXTR_SOURCE_DIR=/src --env GREENLINK_BUILD_CACHE=/var/cache/flxtr-steamlink \
    --env FLXTR_DISABLE_AUTO_UPDATE=1 \
    --env GIT_CONFIG_COUNT=1 --env GIT_CONFIG_KEY_0=safe.directory --env GIT_CONFIG_VALUE_0=/src \
    --entrypoint /bin/bash "$image" /src/scripts/autocompile --steamlink --no-update
printf '\nSteam Link package: %s/dist/greenlink.tgz\n' "$PWD"
