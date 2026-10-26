#!/usr/bin/env bash
# Regular WSL/Linux; disposable Ubuntu 22.04 container supplies FFmpeg 4.x.
set -euo pipefail
cd "$(dirname "$0")/.."
image=${FLXTR_TIMING_TEST_IMAGE:-flxtr-desktop-builder:local}
command -v docker >/dev/null
docker image inspect "$image" >/dev/null
mkdir -p build/timing-tests
git show 487b94014d01ff0575669637e0e4e21272603b39:src/player.c >build/timing-tests/serialized-player.c
command -v ffmpeg >/dev/null
python3 tests/steamlink_timing_fixture.py build/timing-tests
ffmpeg -v error -y -f lavfi -i testsrc2=size=320x180:rate=30 -f lavfi -i sine=frequency=440:sample_rate=44100 -t 8 -c:v libx264 -preset fast -g 30 -bf 2 -c:a aac -movflags +faststart build/timing-tests/video.mp4
ffmpeg -v error -y -i build/timing-tests/video.mp4 -i build/timing-tests/captions.srt -map 0:v -map 0:a -map 1 -c copy -c:s srt build/timing-tests/captions.mkv
ffmpeg -v error -y -i build/timing-tests/video.mp4 -c copy -output_ts_offset 5 build/timing-tests/offset-external.mkv
ffmpeg -v error -y -f lavfi -i testsrc2=size=1280x720:rate=10 -f lavfi -i sine=frequency=440:sample_rate=44100 -t 22 -c:v libx264 -preset ultrafast -g 20 -bf 0 -c:a aac -output_ts_offset 5 -f mpegts build/timing-tests/seek-offset.ts
docker run --rm --user "$(id -u):$(id -g)" --mount "type=bind,src=$PWD,dst=/src" "$image" bash -c '
set -e
flags="$(pkg-config --cflags sdl2 libavformat libavcodec libavutil libswresample)"
libs="$(pkg-config --libs sdl2 libavformat libavcodec libavutil libswresample)"
cc -std=c99 -Wall -Wextra -Werror -DFLXTR_TEST_AUDIO_TRACE -Itests/steamlink_stub -Isrc $flags src/player.c tests/steamlink_video_stub.c -o build/timing-tests/audio-master $libs -ldl -lm -lpthread -Wl,--wrap=SDL_QueueAudio
cc -std=c99 -Wall -Wextra -Werror -DFLXTR_TEST_AUDIO_TRACE -Itests/steamlink_stub -Isrc $flags build/timing-tests/serialized-player.c tests/steamlink_video_stub.c -o build/timing-tests/serialized $libs -ldl -lm -lpthread -Wl,--wrap=SDL_QueueAudio
python3 tests/steamlink_timing_integration_test.py build/timing-tests/audio-master build/timing-tests/serialized build/timing-tests/video.mp4
python3 tests/steamlink_buffer_integration_test.py build/timing-tests/audio-master build/timing-tests/seek-offset.ts
python3 tests/steamlink_timing_seek_test.py build/timing-tests/audio-master build/timing-tests/seek-offset.ts
python3 tests/steamlink_subtitle_clock_test.py build/timing-tests/audio-master build/timing-tests/captions.mkv build/timing-tests/offset-external.mkv
FLXTR_TEST_DECODE_LEAD_MS=1550 FLXTR_TEST_SUBTITLE_DELAY_MS=250 python3 tests/steamlink_subtitle_clock_test.py build/timing-tests/audio-master build/timing-tests/captions.mkv build/timing-tests/offset-external.mkv
python3 tests/steamlink_decode_lead_test.py build/timing-tests/audio-master build/timing-tests/video.mp4
'
