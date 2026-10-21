#!/usr/bin/env bash
# Side-by-side frontend. Never installs over the user's RetroArch.
set -euo pipefail
cd "$(dirname "$0")/.."
project=$PWD
sdk=${STEAMLINK_SDK_PATH:-/opt/steamlink-sdk}
set +u
source "$sdk/setenv.sh"
set -u
rev=9750719074041e0f1949cf9535d1e04945f6213b
source_dir="$project/build/retroarch-audio-src"
git_source() { git -c safe.directory="$source_dir" -C "$source_dir" "$@"; }
if [ ! -d "$source_dir/.git" ]; then
    git clone --depth 1 --branch v1.7.6 https://github.com/libretro/RetroArch.git "$source_dir"
fi
[ "$(git_source rev-parse HEAD)" = "$rev" ] || { echo 'Unexpected RetroArch revision' >&2; exit 1; }
# This is a dedicated disposable build tree. Refuse unexpected user edits.
changes=$(git_source diff --name-only)
[ -z "$changes" ] || [ "$changes" = audio/drivers/sdl_audio.c ] || { echo 'Unexpected source edits' >&2; exit 1; }
cp retroarch/steamlink_sdl_audio_driver.c "$source_dir/audio/drivers/sdl_audio.c"
cp src/steamlink_sdl_audio.[ch] "$source_dir/audio/drivers/"
cd "$source_dir"
export OS=Linux
# RetroArch quotes CC as a single executable; SDK CC contains extra arguments.
export FLXTR_RA_CC="${CROSS}gcc" FLXTR_RA_CXX="${CROSS}g++"
printf '#!/bin/sh\nexec "$FLXTR_RA_CC" --sysroot="$MARVELL_ROOTFS" "$@"\n' > .flxtr-cc
printf '#!/bin/sh\nexec "$FLXTR_RA_CXX" --sysroot="$MARVELL_ROOTFS" "$@"\n' > .flxtr-cxx
chmod +x .flxtr-cc .flxtr-cxx
export CC="$PWD/.flxtr-cc" CXX="$PWD/.flxtr-cxx" CPP="${CROSS}cpp"
export CFLAGS="--sysroot=$MARVELL_ROOTFS -Os -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -DEGL_API_FB=1"
export CXXFLAGS="$CFLAGS"
export LDFLAGS="--sysroot=$MARVELL_ROOTFS ${MARVELL_RPATH_LINK_OPTIONS:-} -lEGL -lpthread"
export INCLUDE_DIRS="-I$MARVELL_ROOTFS/usr/include/SDL2 -I$MARVELL_ROOTFS/usr/include/EGL -I$MARVELL_ROOTFS/usr/include/freetype2"
export LIBRARY_DIRS="-L$MARVELL_ROOTFS/usr/lib -L$MARVELL_ROOTFS/lib"
export PKG_CONF_PATH=pkg-config
./configure --host="$SOC_BUILD" --disable-threads --disable-alsa --disable-pulse \
    --enable-neon --disable-shaderpipeline --enable-opengles --enable-sdl2 \
    --disable-qt --disable-x11 --disable-wayland
make -j"${JOBS:-4}"
mkdir -p "$project/build/retroarch-audio" "$project/dist/core-sources"
cp retroarch "$project/build/retroarch-audio/retroarch.exec"
"$STRIP" "$project/build/retroarch-audio/retroarch.exec"
cp COPYING "$project/build/retroarch-audio/COPYING"
printf 'RetroArch=%s\nadapter=retroarch/steamlink_sdl_audio_driver.c\nHardware validation=PENDING\n' "$rev" > "$project/build/retroarch-audio/BUILD.txt"
# Source and adapter are included together for GPL source correspondence.
git_source archive --prefix=retroarch/ "$rev" > "$project/dist/core-sources/retroarch-audio.tar"
staging="$project/build/retroarch-audio-source"
mkdir -p "$staging/retroarch/audio/drivers"
cp audio/drivers/sdl_audio.c audio/drivers/steamlink_sdl_audio.[ch] "$staging/retroarch/audio/drivers/"
tar -rf "$project/dist/core-sources/retroarch-audio.tar" -C "$staging" retroarch/audio/drivers
gzip -f "$project/dist/core-sources/retroarch-audio.tar"
