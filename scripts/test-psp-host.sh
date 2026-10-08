#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
flags=(-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined
       -Ithird_party/tilefinch-media/include -Ithird_party/tilefinch-media/src)
ffmpeg -v error -f lavfi -i testsrc2=size=320x240:rate=24 \
    -f lavfi -i sine=frequency=440:sample_rate=48000 -t 2 \
    -c:v libx264 -profile:v baseline -c:a aac -ac 2 -movflags +faststart -y build/psp-fixture.mp4
cc "${flags[@]}" tests/psp_media_test.c psp/budget.c \
    third_party/tilefinch-media/src/media_mp4.c \
    third_party/tilefinch-media/src/media_h264_psp_compat.c -o build/psp-media-test
build/psp-media-test build/psp-fixture.mp4
cc "${flags[@]}" -Wno-deprecated-declarations tests/psp_http_test.c psp/http.c -lcurl -o build/psp-http-test
python3 tests/psp_http_test.py
