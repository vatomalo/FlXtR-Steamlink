#!/usr/bin/env bash
# Pinned SDK build; optional core stays outside the legacy 32 MiB app package.
set -euo pipefail
if [[ ${1:-} != --inside ]]; then
    root=$(cd "$(dirname "$0")/.." && pwd)
    cache=${FLXTR_PSP_SOURCE:-/opt/dockers/storage/build-cache/steamlink/ppsspp}
    rev=7b4ddb426bbe9e287bb7f19b0cfaebb4ea0d41d8
    if [[ ! -d "$cache/.git" ]]; then
        git init "$cache";git -C "$cache" remote add origin https://github.com/libretro/ppsspp.git
        git -C "$cache" fetch --depth 1 origin "$rev";git -C "$cache" checkout --detach FETCH_HEAD
    fi
    [[ $(git -C "$cache" rev-parse HEAD) == "$rev" ]] || { echo 'Unexpected PSP source revision';exit 1; }
    git -C "$cache" submodule update --init --recursive --depth 1
    mkdir -p "$root/build/ppsspp"
    docker run --rm --cpus=3 --memory=4g --user "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "${STEAMLINK_SDK_PATH:-/opt/dockers/storage/toolchains/steamlink-sdk}:/opt/steamlink-sdk" \
        -v "$cache:/psp" -v "$root/build/ppsspp:/output" -v "$root/scripts/build-ppsspp-core.sh:/build.sh:ro" \
        flxtr-steamlink-builder:skyloft bash /build.sh --inside
    FLXTR_PSP_SOURCE="$cache" python3 "$root/scripts/package-ppsspp-core.py"
    exit
fi
sdk=/opt/steamlink-sdk
set +u
source "$sdk/setenv.sh"
set -u
AR=${CROSS_COMPILE}ar; NM=${CROSS_COMPILE}nm; RANLIB=${CROSS_COMPILE}ranlib; READELF=${CROSS_COMPILE}readelf
source=/psp
mkdir -p "$source/ffmpeg/build-steamlink" /output
cd "$source/ffmpeg/build-steamlink"
if [[ ! -f installed/lib/libavcodec.a ]]; then
 ../configure --prefix="$PWD/installed" --enable-cross-compile --target-os=linux --arch=arm --cpu=cortex-a9 \
  --cc="$CC" --cxx="$CXX" --ar="$AR" --nm="$NM" --ranlib="$RANLIB" --strip="$STRIP" \
  --enable-pic --disable-shared --enable-static --disable-programs --disable-doc --disable-network \
  --disable-avdevice --disable-avfilter --disable-postproc --disable-everything \
  --enable-decoder=h264,mpeg4,mpeg2video,mjpeg,mjpegb,aac,aac_latm,atrac3,atrac3p,mp3,pcm_s16le,pcm_s8 \
  --enable-demuxer=h264,m4v,mpegvideo,mpegps,mp3,avi,aac,pmp,oma,pcm_s16le,pcm_s8,wav \
  --enable-parser=h264,mpeg4video,mpegaudio,mpegvideo,aac \
  --enable-swscale --enable-swresample --disable-bzlib --disable-lzma --disable-iconv \
  --extra-cflags='-O2 -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -fPIC' \
  --extra-ldflags='-march=armv7-a -mfpu=neon -mfloat-abi=hard'
 make -j3
 make install
fi
ffmpeg=$PWD/installed
cd "$source/libretro"
# Steam Link uses EGL/GLES without X11. Keep optional Vulkan loader WSI-free.
python3 - <<'PY'
from pathlib import Path
p=Path('Makefile.common');s=p.read_text();s=s.replace('COREFLAGS += -DVK_USE_PLATFORM_XLIB_KHR','# FlXtR Steam Link: no X11 WSI');p.write_text(s)
p=Path('Makefile');s=p.read_text();needle='CXXFLAGS    +=';marker='OBJECTS := $(sort $(OBJECTS))';
if marker not in s:s=s.replace(needle,marker+'\n'+needle,1)
p.write_text(s)
p=Path('Makefile.common');s=p.read_text();marker='# FlXtR Steam Link GLES entrypoint loader'
if marker not in s:s+='\n'+marker+'\nSOURCES_C += $(NATIVEDIR)/gfx_es2/gl3stub.c\n'
s='\n'.join(line for line in s.splitlines() if not line.startswith('SOURCES_C += $(NATIVEDIR)/gfx_es2/gl3stub.c'))+'\nSOURCES_C += $(NATIVEDIR)/gfx_es2/gl3stub.c $(NATIVEDIR)/math/fast/fast_math.c $(NATIVEDIR)/math/fast/fast_matrix.c\n'
p.write_text(s)
PY
export LDFLAGS='-static-libstdc++ -static-libgcc'
make -j3 platform=imx6 CC="$CC" CXX="$CXX" AS="$CC" AR="$AR" \
 CPUFLAGS='-march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard -marm -DEGL_API_FB -DGLEW_EGL' \
 FFMPEGINCFLAGS="-I$ffmpeg/include" \
 FFMPEGLDFLAGS="-L$ffmpeg/lib -lavformat -lavcodec -lavutil -lswresample -lswscale" GL_LIB='-lGLESv2 -lEGL'
"$STRIP" ppsspp_libretro.so
cp ppsspp_libretro.so /output/
"$READELF" -h ppsspp_libretro.so | head -18
echo 'SDK PPSSPP core compiled.'