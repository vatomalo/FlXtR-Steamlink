#!/usr/bin/env bash
# Reproducible Steam Link cores; keep source archives beside published binaries.
set -euo pipefail
cd "$(dirname "$0")/.."
project=$PWD
sdk=${STEAMLINK_SDK_PATH:-/opt/steamlink-sdk}
set +u
source "$sdk/setenv.sh"
set -u
cache=${FLXTR_CORE_CACHE:-/tmp/flxtr-cores}
mkdir -p "$cache" "$project/build/cores" "$project/dist/core-sources"
flags='-O3 -fno-strict-aliasing -fwrapv -fomit-frame-pointer -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -marm'
checkout() {
    local name=$1 url=$2 rev=$3
    if [ ! -d "$cache/$name/.git" ]; then git clone "$url" "$cache/$name"; fi
    git -C "$cache/$name" cat-file -e "$rev^{commit}" || git -C "$cache/$name" fetch origin "$rev"
    [ -z "$(git -C "$cache/$name" status --porcelain)" ] || { echo "Local edits in $cache/$name" >&2; exit 1; }
    git -C "$cache/$name" checkout --detach "$rev"
    git -C "$cache/$name" archive --format=tar.gz --prefix="$name/" "$rev" > "$project/dist/core-sources/$name-$rev.tar.gz"
}
if [ "${1:-all}" != mame ]; then
    rev=a79dfe9047e7fec58808aefe48ad2bf499c7af11
    checkout snes9x2005 https://github.com/libretro/snes9x2005.git "$rev"
    cd "$cache/snes9x2005"
    make platform=unix USE_BLARGG_APU=0 clean > "$cache/snes-clean.log" 2>&1
    make -j"${JOBS:-4}" platform=unix USE_BLARGG_APU=0 CC="$CC" CXX="$CXX" \
        CFLAGS="$flags -DNDEBUG -fPIC -DLOAD_FROM_MEMORY -DLAGFIX -I./source -I. -I./libretro-common/include" \
        > "$cache/snes-build.log" 2>&1 || { tail -60 "$cache/snes-build.log"; exit 1; }
    "$STRIP" snes9x2005_libretro.so
    cp snes9x2005_libretro.so "$project/build/cores/snes9x2005_flxtr_libretro.so"
    cp copyright "$project/build/cores/SNES9X-LICENSE.txt"
    printf 'source=https://github.com/libretro/snes9x2005\ncommit=%s\nflags=%s\n' "$rev" "$flags" > "$project/build/cores/snes9x2005-build.txt"
fi
if [ "${1:-all}" != snes ]; then
    rev=0353570ddd2e6d9d32bffc89278e1b37f5d61f8e
    checkout mame2003-plus https://github.com/libretro/mame2003-plus-libretro.git "$rev"
    cd "$cache/mame2003-plus"
    make platform=unix ARCH=arm ARM=1 HAVE_ARMv6=1 clean > "$cache/mame-clean.log" 2>&1
    make -j"${JOBS:-4}" platform=unix ARCH=arm ARM=1 HAVE_ARMv6=1 CC="$CC" CXX="$CXX" \
        PLATCFLAGS="$flags" > "$cache/mame-build.log" 2>&1 || { tail -60 "$cache/mame-build.log"; exit 1; }
    "$STRIP" mame2003_plus_libretro.so
    cp mame2003_plus_libretro.so "$project/build/cores/mame2003_plus_flxtr_libretro.so"
    cp LICENSE.md "$project/build/cores/MAME2003-PLUS-LICENSE.md"
    printf 'source=https://github.com/libretro/mame2003-plus-libretro\ncommit=%s\nflags=%s\n' "$rev" "$flags" > "$project/build/cores/mame2003-plus-build.txt"
fi
cd "$project/build/cores"
sha256sum *_flxtr_libretro.so > SHA256SUMS
file *_flxtr_libretro.so
