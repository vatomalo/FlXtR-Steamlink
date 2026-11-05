#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
storage=/opt/dockers/storage
mountpoint -q "$storage" || { echo 'Docker storage disk is not mounted.' >&2; exit 1; }
mkdir -p "$storage/build-home/android" "$storage/build-logs"
exec 9>"$storage/build-home/android/build.lock"
flock -n 9 || { echo 'An Android build is already running.' >&2; exit 1; }
key="$storage/.secrets/android-signing.keystore"
test -r "$key" || { echo 'Original Android signing key is missing.' >&2; exit 1; }
if ! docker image inspect flxtr-android-builder:skyloft >/dev/null 2>&1; then
    docker build --progress=plain -f "$repo/Dockerfile.android" -t flxtr-android-builder:skyloft "$repo"
fi
if ! docker image inspect flxtr-embedded-android-builder:skyloft >/dev/null 2>&1; then
    docker build --progress=plain -f "$repo/Dockerfile.embedded-android" -t flxtr-embedded-android-builder:skyloft "$repo"
fi
version=${FLXTR_ANDROID_VERSION_CODE:-}
if [[ -z "$version" ]]; then
    current=$(curl --fail --silent --show-error --location 'https://github.com/vatomalo/FlXtR-Steamlink/releases/download/android-latest/android.json' | python3 -c 'import json,sys; print(json.load(sys.stdin)["versionCode"])')
    version=$((current + 1))
fi
docker run --rm --name flxtr-android-build --cpus=3 --memory=5g \
    --user "$(id -u):$(id -g)" \
    -e HOME=/build-home -e GRADLE_USER_HOME=/build-home/.gradle \
    -e GOPATH=/build-home/go -e GOCACHE=/build-home/.cache/go-build \
    -e FLXTR_ANDROID_RELEASE=1 -e FLXTR_ANDROID_KEYSTORE=/signing/android.keystore \
    -e "FLXTR_ANDROID_VERSION_CODE=$version" \
    -e "FLXTR_ANDROID_VERSION_NAME=0.10.28-skyloft.$version" \
    -v "$repo:/src" -v "$storage/build-home/android:/build-home" \
    -v "$key:/signing/android.keystore:ro" flxtr-embedded-android-builder:skyloft
echo "Signed APK built on SkyLoft: $repo/dist/FlXtR-Android-preview.apk"
echo 'Publishing to the update channel is a separate step after verification.'
