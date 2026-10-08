#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
project=$PWD
cache=${GREENLINK_BUILD_CACHE:-/tmp/greenlink-build}
sdk=${STEAMLINK_SDK_PATH:-/opt/steamlink-sdk}
mkdir -p "$cache" build/resolver
archive="$cache/wabt-1.0.36.tar.gz"
if [ ! -f "$archive" ]; then
    wget -q https://github.com/WebAssembly/wabt/archive/refs/tags/1.0.36.tar.gz -O "$archive"
fi
echo "e07ceeecfc682c12157ff2738b8a4633d7d19da18c1ecf16daae700397ecce2c  $archive" | sha256sum -c -
if [ ! -d "$cache/wabt-1.0.36" ]; then tar -xf "$archive" -C "$cache"; fi
pico="$cache/wabt-1.0.36/third_party/picosha2/picosha2.h"
if [ ! -f "$pico" ]; then
    mkdir -p "$(dirname "$pico")"
    wget -q https://raw.githubusercontent.com/okdshin/PicoSHA2/27fcf6979298949e8a462e16d09a0351c18fcaf2/picosha2.h -O "$pico"
fi
echo "8f183eaae529cd9d6a3d4843c7559e2a3e3d68b6caaa223e7c24c3c899b3d988  $pico" | sha256sum -c -
# WABT runs on the build host, not on the Steam Link. SDK environment variables
# may cause CMake to produce an ARM utility, which cannot execute in x86 Docker.
# Keep this host build separate from the cross-compiled resolver below.
host_wasm2c="$cache/wabt-host/wasm2c"
if [ -x "$host_wasm2c" ] && ! "$host_wasm2c" --version >/dev/null 2>&1; then
    echo "Discarding incompatible cached host wasm2c" >&2
    rm -rf "$cache/wabt-host"
fi
if [ ! -x "$host_wasm2c" ]; then
    (
        unset CC CXX AR AS LD STRIP CROSS CROSS_COMPILE CFLAGS CXXFLAGS LDFLAGS
        unset CMAKE_TOOLCHAIN_FILE CMAKE_C_COMPILER CMAKE_CXX_COMPILER
        unset CMAKE_GENERATOR CMAKE_PROJECT_TOP_LEVEL_INCLUDES CMAKE_PROJECT_INCLUDE CMAKE_PROJECT_INCLUDE_BEFORE
        cmake -S "$cache/wabt-1.0.36" -B "$cache/wabt-host" \
            -U CMAKE_TOOLCHAIN_FILE -U CMAKE_C_COMPILER -U CMAKE_CXX_COMPILER \
            -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE:FILEPATH= \
            -DCMAKE_C_COMPILER:FILEPATH=/usr/bin/cc -DCMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++ \
            >"$cache/wabt-cmake.log" 2>&1
        cmake --build "$cache/wabt-host" --target wasm2c -j"${JOBS:-4}" \
            >"$cache/wabt-make.log" 2>&1 || { tail -50 "$cache/wabt-make.log"; exit 1; }
    )
fi
"$host_wasm2c" --version >/dev/null
wasm="$cache/img_data-6942482f.wasm"
if [ ! -f "$wasm" ]; then
    wget -q https://plsdontscrapemelove.flixer.gd/assets/wasm/img_data_bg.wasm -O "$wasm"
fi
echo "6942482ff310cee739b250cb9eeee7bc373121fb1580c598405899ca61b2e4e5  $wasm" | sha256sum -c -
"$cache/wabt-host/wasm2c" "$wasm" -o build/resolver/img_data.c --module-name img_data
python3 tools/resolver_imports.py build/resolver/img_data.h build/resolver/resolver_imports.inc
output=build/greenlink-resolver-arm
if [ "${FLXTR_BUILD_PLATFORM:-steamlink}" = linux ]; then
    CC=cc
    STRIP=strip
    output=build/greenlink-resolver-linux
else
    set +u
    source "$sdk/setenv.sh"
    set -u
fi
runtime="$cache/wabt-1.0.36/wasm2c"
flags=(-Os -std=c11 -D_POSIX_C_SOURCE=200809L -DNDEBUG -DWASM_RT_USE_MMAP=0 -DWASM_RT_MEMCHECK_BOUNDS_CHECK=1 -Ibuild/resolver -Isrc -I"$runtime")
$CC "${flags[@]}" -c build/resolver/img_data.c -o build/resolver/module.o
$CC "${flags[@]}" -Wall -Wextra -Werror -c src/resolver_bridge.c -o build/resolver/bridge.o
$CC "${flags[@]}" -c "$runtime/wasm-rt-impl.c" -o build/resolver/runtime.o
$CC "${flags[@]}" -c "$runtime/wasm-rt-mem-impl.c" -o build/resolver/memory.o
$CC "${flags[@]}" -Wall -Wextra -Werror -c src/resolver.c -o build/resolver/client.o
$CC build/resolver/{module,bridge,runtime,memory,client}.o -o "$output" -lcurl -ljson-c -lcrypto -lm -lpthread
$STRIP "$output"
cp "$cache/wabt-1.0.36/LICENSE" build/resolver/WABT-LICENSE.txt
if command -v file >/dev/null; then file "$output"; fi
wc -c "$output"
