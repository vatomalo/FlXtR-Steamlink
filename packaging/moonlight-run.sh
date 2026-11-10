#!/bin/sh
# Invoked only after FlXtR has released its graphics/audio devices.
cd "$(dirname "$0")" || exit 1
operation=${1:-}
shift || exit 2
bounded_test() {
    # Steam Link's BusyBox 1.24 requires -t; GNU timeout takes a positional duration.
    if timeout --help 2>&1 | grep -q '\[-t SECS\]'; then
        timeout -t 8 -s KILL moonlight/flxtr-audio-test "$@"
    else
        timeout -s KILL 8 moonlight/flxtr-audio-test "$@"
    fi
}
if [ "$operation" = list ]; then bounded_test list; exit "$?"; fi
audio=auto
if [ -r streaming-audio.txt ]; then IFS= read -r audio <streaming-audio.txt; fi
unset ML_AUDIO FLXTR_AUDIO_DEVICE
case "$audio" in
    auto|'') audio=auto ;;
    hdmi)
        # Firmware 3.x SLAudio also consults SDL's driver choice. A global ALSA
        # preference can otherwise open successfully while HDMI stays silent.
        export ML_AUDIO=slaudio SDL_AUDIO_DRIVER=marvell SDL_AUDIODRIVER=marvell ;;
    hdmi-sdl) export ML_AUDIO=sdl SDL_AUDIO_DRIVER=marvell SDL_AUDIODRIVER=marvell ;;
    sdl) export ML_AUDIO=sdl ;;
    device:*) export ML_AUDIO=sdl; FLXTR_AUDIO_DEVICE=${audio#device:}; export FLXTR_AUDIO_DEVICE ;;
    *) echo 'Invalid audio output preference' >&2; exit 2 ;;
esac
if [ "$operation" = test ]; then
    [ -x moonlight/flxtr-audio-test ] || { echo 'Audio test is not installed' >&2; exit 127; }
    case "$audio" in
        auto|hdmi) bounded_test hdmi ;;
        sdl|hdmi-sdl) bounded_test sdl ;;
        device:*) bounded_test sdl "$FLXTR_AUDIO_DEVICE" ;;
    esac
    exit "$?"
fi
[ "$operation" = stream ] && [ "$#" -eq 2 ] || { echo 'Usage: moonlight-run.sh stream HOST APP | test' >&2; exit 2; }
resolution=720
fps=60
bitrate=8000
if [ -r streaming.cfg ]; then
    while IFS='=' read -r key value; do
        case "$key:$value" in
            resolution:720|resolution:1080) resolution=$value ;;
            fps:30|fps:60) fps=$value ;;
            bitrate:5000|bitrate:8000|bitrate:12000|bitrate:20000|bitrate:30000|bitrate:40000) bitrate=$value ;;
        esac
    done <streaming.cfg
fi
if [ -x moonlight/moonlight ]; then
    binary=$PWD/moonlight/moonlight
else
    # Preserve existing installations until the bundled client arrives.
    [ "$audio" = auto ] || [ "$audio" = hdmi ] || {
        echo 'Selected output requires bundled FlXtR Moonlight' >&2; exit 1;
    }
    binary=/home/apps/moonlight/bin/moonlight
fi
export HOME=${FLXTR_MOONLIGHT_HOME:-/usr/local/moonlight}
export SDL_ENABLE_STEAM_CONTROLLERS=1 SENSOR_REPORT_RATE_LIMIT_HZ=0
echo "FlXtR stream: ${resolution}p/$fps, $bitrate Kbps, audio=$audio"
dimensions=1280x720
[ "$resolution" != 1080 ] || dimensions=1920x1080
quit=--no-quit-after
[ "${FLXTR_GAMEZ_MANAGED_SESSION:-0}" != 1 ] || quit=--quit-after
exec "$binary" stream "$1" "$2" --resolution "$dimensions" --fps "$fps" \
    --bitrate "$bitrate" --video-codec H.264 --video-decoder hardware \
    --audio-config stereo --no-hdr --display-mode fullscreen "$quit"
