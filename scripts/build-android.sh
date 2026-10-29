#!/usr/bin/env bash
# Run on Linux/WSL with JDK 17+ and the Android SDK. No Steam Link SDK required.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
export ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
: "${ANDROID_HOME:?Set ANDROID_HOME to your Android SDK directory}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
deps="$repo/android/build/deps"
mkdir -p "$deps"
fetch() {
    local name=$1 url=$2
    if [[ ! -f "$deps/$name/.flxtr-ready" ]]; then
        local stage
        stage=$(mktemp -d "$deps/.download-XXXXXX")
        curl --fail --location --retry 3 "$url" -o "$stage/archive.tar.gz"
        tar xf "$stage/archive.tar.gz" -C "$stage" --strip-components=1
        rm "$stage/archive.tar.gz"
        # Leave failed downloads for diagnosis. Never replace a completed dependency.
        mv "$stage" "$deps/$name"
        touch "$deps/$name/.flxtr-ready"
    fi
}
fetch SDL2 https://github.com/libsdl-org/SDL/releases/download/release-2.32.0/SDL2-2.32.0.tar.gz
fetch SDL2_image https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.4/SDL2_image-2.8.4.tar.gz
fetch mbedtls https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v2.28.9.tar.gz
fetch curl https://curl.se/download/curl-8.12.1.tar.gz
python3 "$repo/scripts/prepare-android-sdl.py" "$deps/SDL2"
runtime="$repo/android/build/runtime/runtime"
mkdir -p "$runtime/assets" "$runtime/certs"
cp "$repo"/assets/*.bmp "$runtime/assets/"
cp "$repo/assets/phone.html" "$repo/assets/phone.js" "$runtime/assets/"
cp /etc/ssl/certs/ca-certificates.crt "$runtime/certs/cacert.pem"
# A real public Blender demo exercises Android playback without account setup.
printf 'BIG BUCK BUNNY\tANDROID PLAYBACK TEST / BLENDER FOUNDATION\tassets/demo-0.bmp\thttps://storage.googleapis.com/exoplayer-test-media-0/BigBuckBunny_320x180.mp4\n' > "$runtime/catalog.tsv"
printf 'sdk.dir=%s\n' "$ANDROID_HOME" > "$repo/android/local.properties"
mkdir -p "$runtime/licenses"
cp "$deps/SDL2/LICENSE.txt" "$runtime/licenses/SDL2.txt"
cp "$deps/SDL2_image/LICENSE.txt" "$runtime/licenses/SDL2_image.txt"
cp "$deps/mbedtls/LICENSE" "$runtime/licenses/mbedtls.txt"
cp "$deps/curl/COPYING" "$runtime/licenses/curl.txt"
cp "$repo/LICENSE" "$runtime/licenses/FlXtR.txt"
printf 'Big Buck Bunny / Blender Foundation / CC BY 3.0\nhttps://peach.blender.org/about/\n' > "$runtime/licenses/demo.txt"
cd "$repo/android"
./gradlew --no-daemon assembleDebug lintDebug
mkdir -p "$repo/dist"
cp app/build/outputs/apk/debug/app-debug.apk "$repo/dist/FlXtR-Android-preview.apk"
sha256sum "$repo/dist/FlXtR-Android-preview.apk"
