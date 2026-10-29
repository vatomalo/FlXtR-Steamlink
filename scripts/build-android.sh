#!/usr/bin/env bash
# Media app: Linux/WSL, JDK 17+ and Android SDK. No desktop launcher is bundled.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
export ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
: "${ANDROID_HOME:?Set ANDROID_HOME to your Android SDK directory}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
bash "$repo/scripts/prepare-android-resolver.sh"
runtime="$repo/android/build/media-runtime/runtime/licenses"
mkdir -p "$runtime"
cp "$repo/LICENSE" "$runtime/FlXtR.txt"
cp "$repo/android/build/runtime/runtime/licenses/WABT.txt" "$runtime/WABT.txt"
printf 'sdk.dir=%s\n' "$ANDROID_HOME" > "$repo/android/local.properties"
cd "$repo/android"
./gradlew --no-daemon assembleDebug testDebugUnitTest lintDebug
mkdir -p "$repo/dist"
cp app/build/outputs/apk/debug/app-debug.apk "$repo/dist/FlXtR-Android-preview.apk"
sha256sum "$repo/dist/FlXtR-Android-preview.apk"
