#!/usr/bin/env bash
# Reproducible Steam Link cores; keep source archives beside published binaries.
set -euo pipefail
cd "$(dirname "$0")/.."
project=$PWD
target=${1:-all}
case "$target" in all|snes|snes-plus|mame) ;; *) echo 'Use all, snes, snes-plus or mame' >&2; exit 2;; esac
sdk=${STEAMLINK_SDK_PATH:-/opt/steamlink-sdk}
set +u
source "$sdk/setenv.sh"
set -u
cache=${FLXTR_CORE_CACHE:-/tmp/flxtr-cores}
mkdir -p "$cache" "$project/build/cores" "$project/dist/core-sources"
optimization=${FLXTR_CORE_OPTIMIZATION:--O3}
case "$optimization" in -O2|-O3|-Os) ;; *) echo 'Use -O2, -O3 or -Os for FLXTR_CORE_OPTIMIZATION' >&2; exit 2;; esac
flags="$optimization -fno-strict-aliasing -fwrapv -fomit-frame-pointer -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -marm"
checkout() {
    local name=$1 url=$2 rev=$3
    if [ ! -d "$cache/$name/.git" ]; then git clone "$url" "$cache/$name"; fi
    git -C "$cache/$name" cat-file -e "$rev^{commit}" || git -C "$cache/$name" fetch origin "$rev"
    [ -z "$(git -C "$cache/$name" status --porcelain)" ] || { echo "Local edits in $cache/$name" >&2; exit 1; }
    git -C "$cache/$name" checkout --detach "$rev"
    git -C "$cache/$name" archive --format=tar.gz --prefix="$name/" "$rev" > "$project/dist/core-sources/$name-$rev.tar.gz"
}
if [ "$target" != mame ]; then
    rev=a79dfe9047e7fec58808aefe48ad2bf499c7af11
    checkout snes9x2005 https://github.com/libretro/snes9x2005.git "$rev"
    cd "$cache/snes9x2005"
    apu=0;name=snes9x2005
    if [ "$target" = snes-plus ]; then apu=1;name=snes9x2005_plus;fi
    compile_flags="$flags -DNDEBUG -fPIC -DLOAD_FROM_MEMORY -DLAGFIX -I./source -I. -I./libretro-common/include"
    if [ "$apu" = 1 ]; then compile_flags="$compile_flags -DUSE_BLARGG_APU";fi
    make platform=unix USE_BLARGG_APU="$apu" LOAD_FROM_MEMORY=1 clean > "$cache/snes-clean.log" 2>&1
    make -j"${JOBS:-4}" platform=unix USE_BLARGG_APU="$apu" LOAD_FROM_MEMORY=1 CC="$CC" CXX="$CXX" \
        CFLAGS="$compile_flags" CXXFLAGS="$compile_flags -fno-exceptions -fno-rtti" \
        > "$cache/snes-build.log" 2>&1 || { tail -60 "$cache/snes-build.log"; exit 1; }
    "$STRIP" "${name}_libretro.so"
    cp "${name}_libretro.so" "$project/build/cores/${name}_flxtr_libretro.so"
    cp copyright "$project/build/cores/SNES9X-LICENSE.txt"
    printf 'source=https://github.com/libretro/snes9x2005\ncommit=%s\nUSE_BLARGG_APU=%s\nflags=%s\n' "$rev" "$apu" "$compile_flags" > "$project/build/cores/${name}-build.txt"
fi
if [ "$target" = all ] || [ "$target" = mame ]; then
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
