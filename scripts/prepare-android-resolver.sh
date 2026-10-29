#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
cache=${ANDROID_RESOLVER_CACHE:-$repo/android/build/resolver-cache}
mkdir -p "$cache/wabt-host"
# A matching host tool avoids rebuilding WABT; the existing preparer verifies
# the pinned WABT archive, auxiliary header and response-decoder module hashes.
if command -v wasm2c >/dev/null && [[ $(wasm2c --version) == 1.0.36 ]]; then
    ln -sf "$(command -v wasm2c)" "$cache/wabt-host/wasm2c"
fi
GREENLINK_BUILD_CACHE="$cache" bash "$repo/scripts/build-resolver.sh" --prepare-only
cp "$cache/wabt-1.0.36/wasm2c/"*.inc "$cache/wabt-1.0.36/wasm2c/"*.h "$repo/build/resolver/"
cp "$cache/wabt-1.0.36/wasm2c/wasm-rt-impl.c" "$cache/wabt-1.0.36/wasm2c/wasm-rt-mem-impl.c" "$cache/wabt-1.0.36/wasm2c/wasm-rt-exceptions-impl.c" "$repo/build/resolver/"
mkdir -p "$repo/android/build/runtime/runtime/licenses"
cp "$cache/wabt-1.0.36/LICENSE" "$repo/android/build/runtime/runtime/licenses/WABT.txt"
