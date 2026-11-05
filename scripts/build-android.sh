#!/usr/bin/env bash
# Media app: Linux/WSL, JDK 17+ and Android SDK. No desktop launcher is bundled.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
export ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
: "${ANDROID_HOME:?Set ANDROID_HOME to your Android SDK directory}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
bash "$repo/scripts/prepare-android-resolver.sh"
python3 "$repo/scripts/prepare-android-games.py"
python3 "$repo/scripts/prepare-console-cores.py"
bash "$repo/scripts/prepare-android-tailnet.sh"
runtime="$repo/android/build/media-runtime/runtime/licenses"
mkdir -p "$runtime"
cp "$repo/LICENSE" "$runtime/FlXtR.txt"
cp "$repo/android/build/runtime/runtime/licenses/WABT.txt" "$runtime/WABT.txt"
printf 'sdk.dir=%s\n' "$ANDROID_HOME" > "$repo/android/local.properties"
cd "$repo/android"
version_args=()
if [[ -n "${FLXTR_ANDROID_VERSION_CODE:-}" ]]; then version_args+=("-PflxtrVersionCode=$FLXTR_ANDROID_VERSION_CODE"); fi
if [[ -n "${FLXTR_ANDROID_VERSION_NAME:-}" ]]; then version_args+=("-PflxtrVersionName=$FLXTR_ANDROID_VERSION_NAME"); fi
./gradlew --no-daemon "${version_args[@]}" assembleDebug testDebugUnitTest lintDebug
mkdir -p "$repo/dist"
if [[ "${FLXTR_ANDROID_RELEASE:-0}" == 1 ]]; then
    ./gradlew --no-daemon "${version_args[@]}" assembleRelease
    "$ANDROID_HOME/build-tools/35.0.0/apksigner" sign --ks "${FLXTR_ANDROID_KEYSTORE:-$HOME/.android/debug.keystore}" --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android --out "$repo/dist/FlXtR-Android-preview.apk" app/build/outputs/apk/release/app-release-unsigned.apk
    "$ANDROID_HOME/build-tools/35.0.0/apksigner" verify "$repo/dist/FlXtR-Android-preview.apk"
else
    cp app/build/outputs/apk/debug/app-debug.apk "$repo/dist/FlXtR-Android-preview.apk"
fi
sha256sum "$repo/dist/FlXtR-Android-preview.apk"
