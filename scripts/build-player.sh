#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
project=$PWD
sdk=${STEAMLINK_SDK_PATH:-/opt/steamlink-sdk}
set +u
source "$sdk/setenv.sh"
set -u
cache=${GREENLINK_BUILD_CACHE:-/tmp/greenlink-build}
mkdir -p "$cache" "$project/build"
archive="$cache/ffmpeg-4.4.5.tar.xz"
if [ ! -f "$archive" ]; then
    wget -q https://ffmpeg.org/releases/ffmpeg-4.4.5.tar.xz -O "$archive"
fi
echo "f9514e0d3515aee5a271283df71636e1d1ff7274b15853bcd84e144be416ab07  $archive" | sha256sum -c -
if [ ! -d "$cache/ffmpeg-4.4.5" ]; then tar -C "$cache" -xf "$archive"; fi
cd "$cache/ffmpeg-4.4.5"
if [ ! -f "$cache/install/lib/libavformat.a" ]; then
    ./configure --prefix="$cache/install" --enable-cross-compile --cross-prefix="$CROSS" \
        --arch=arm --cpu=cortex-a9 --target-os=linux --sysroot="$MARVELL_ROOTFS" \
        --cc="$CC" --cxx="$CXX" --pkg-config=pkg-config \
        --disable-everything --disable-autodetect --disable-programs --disable-doc \
        --disable-debug --disable-shared --enable-static --enable-small --enable-network \
        --enable-gnutls --enable-avformat --enable-avcodec --enable-swresample \
        --enable-protocol=file,http,https,tcp,tls,crypto \
        --enable-demuxer=mov,mpegts,hls,aac,matroska \
        --enable-decoder=h264,aac,mp3,ac3,eac3,pcm_s16le \
        --enable-parser=h264,aac,mpegaudio,ac3 --enable-bsf=h264_mp4toannexb
    make -j"${JOBS:-4}" >"$cache/ffmpeg-make.log" 2>&1 || { tail -60 "$cache/ffmpeg-make.log"; exit 1; }
    make install >"$cache/ffmpeg-install.log" 2>&1
fi
cd "$project"
$CC -Os -std=c99 -Wall -Wextra -Werror -I"$cache/install/include" \
    $(pkg-config --cflags sdl2) -c src/player.c -o build/player-arm.o
$CC build/player-arm.o -o build/greenlink-player-arm \
    -L"$cache/install/lib" -lavformat -lavcodec -lswresample -lavutil \
    -lSLVideo $(pkg-config --libs sdl2 gnutls) -lm -lpthread -ldl -lz
$STRIP build/greenlink-player-arm
file build/greenlink-player-arm
wc -c build/greenlink-player-arm
