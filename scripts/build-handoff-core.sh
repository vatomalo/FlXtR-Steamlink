#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
project=$PWD
set +u
source "${STEAMLINK_SDK_PATH:-/opt/steamlink-sdk}/setenv.sh"
set -u
revision=77f2567d718b8973d02caa2daa8dce4953faf811
cache=${GREENLINK_BUILD_CACHE:-/var/cache/flxtr-steamlink}/handoff
source_dir="$cache/gambatte"
mkdir -p "$cache" build/cores dist/core-sources
if [ ! -d "$source_dir/.git" ]; then git clone https://github.com/libretro/gambatte-libretro.git "$source_dir"; fi
git -C "$source_dir" checkout --detach "$revision"
python3 - "$source_dir" "$cache/CMakeLists.txt" <<'PY'
import sys
from pathlib import Path
root=Path(sys.argv[1]);sources=[]
for line in (root/'Makefile.common').read_text().splitlines():
    value=line.strip().rstrip('\\').strip()
    if value.startswith('$(') and value.endswith(('.c','.cpp')) and 'net_serial' not in value:
        sources.append(value.replace('$(CORE_DIR)',str(root/'libgambatte/src')).replace('$(LIBRETRO_COMM_DIR)',str(root/'libgambatte/libretro-common')))
Path(sys.argv[2]).write_text('cmake_minimum_required(VERSION 3.5)\nproject(HandoffCore C CXX)\nset(CMAKE_POSITION_INDEPENDENT_CODE ON)\nadd_library(gambatte_handoff_libretro SHARED\n'+ '\n'.join(sources)+')\ntarget_include_directories(gambatte_handoff_libretro PRIVATE '+ ' '.join(str(root/p) for p in ['libgambatte/src','libgambatte/include','common','common/resample','libgambatte/libretro','libgambatte/libretro-common/include']) +')\ntarget_compile_definitions(gambatte_handoff_libretro PRIVATE __LIBRETRO__ HAVE_STDINT_H HAVE_INTTYPES_H CC_RESAMPLER_NO_HIGHPASS VIDEO_RGB565 VFS_FRONTEND)\nset_target_properties(gambatte_handoff_libretro PROPERTIES PREFIX "" CXX_STANDARD 11)\ntarget_link_libraries(gambatte_handoff_libretro m)\n')
PY
cmake -S "$cache" -B "$cache/build" -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_C_FLAGS='-O2 -fno-strict-aliasing -march=armv7-a -mfpu=neon -mfloat-abi=hard' -DCMAKE_CXX_FLAGS='-O2 -fno-strict-aliasing -march=armv7-a -mfpu=neon -mfloat-abi=hard'
cmake --build "$cache/build" -- -j4
cp "$cache/build/gambatte_handoff_libretro.so" build/cores/
$STRIP build/cores/gambatte_handoff_libretro.so
nes_revision=e0f3086a8104f8fbbe49a2615b10401504a9533f
nes_dir="$cache/fceumm"
if [ ! -d "$nes_dir/.git" ]; then git clone https://github.com/libretro/libretro-fceumm.git "$nes_dir"; fi
git -C "$nes_dir" checkout --detach "$nes_revision"
git -C "$nes_dir" submodule update --init --recursive
mkdir -p "$cache/nes-cmake"
printf 'cmake_minimum_required(VERSION 3.5)\nproject(NesHandoff C)\nset(CMAKE_POSITION_INDEPENDENT_CODE ON)\ninclude(core.cmake)\n' > "$cache/nes-cmake/CMakeLists.txt"
python3 scripts/fceumm-cmake.py "$nes_dir" "$cache/nes-cmake/core.cmake"
cmake -S "$cache/nes-cmake" -B "$cache/nes-build" -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_C_FLAGS='-O2 -fno-strict-aliasing -march=armv7-a -mfpu=neon -mfloat-abi=hard'
cmake --build "$cache/nes-build" -- -j3
cp "$cache/nes-build/libflxtr_fceumm.so" build/cores/fceumm_handoff_libretro.so
$STRIP build/cores/fceumm_handoff_libretro.so
cp "$nes_dir/Copying" build/cores/FCEUMM-COPYING.txt
git -C "$nes_dir" archive --format=tar.gz --prefix=fceumm/ "$nes_revision" > dist/core-sources/fceumm-handoff-source.tar.gz
python3 - <<'PY'
import hashlib,json
from pathlib import Path
core=Path('build/cores/fceumm_handoff_libretro.so')
Path('build/cores/fceumm-handoff.json').write_text(json.dumps(dict(source='fceumm:e0f3086a8104f8fbbe49a2615b10401504a9533f',sha256=hashlib.sha256(core.read_bytes()).hexdigest()),indent=2)+'\n')
PY
cp "$source_dir/COPYING" build/cores/GAMBATTE-COPYING.txt
git -C "$source_dir" archive --format=tar.gz --prefix=gambatte/ "$revision" > dist/core-sources/gambatte-handoff-source.tar.gz
python3 - <<'PY'
import hashlib,json
from pathlib import Path
core=Path('build/cores/gambatte_handoff_libretro.so')
Path('build/cores/gambatte-handoff.json').write_text(json.dumps(dict(source='gambatte:77f2567d718b8973d02caa2daa8dce4953faf811',sha256=hashlib.sha256(core.read_bytes()).hexdigest()),indent=2)+'\n')
PY
$CC -Os -std=gnu99 -Wall -Wextra -Werror $(pkg-config --cflags sdl2) src/handoff_game.c -o build/greenlink-handoff-game-arm $(pkg-config --libs sdl2) -ljson-c -ldl
$STRIP build/greenlink-handoff-game-arm
