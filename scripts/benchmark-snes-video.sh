#!/bin/sh
# Steam Link RetroArch Snes9x 2005 bottleneck benchmark.
# Usage: sh benchmark-snes-video.sh /absolute/path/to/game.smc [frames]
# Close FlXtR and RetroArch before running.
set -eu
rom=${1:-}
frames=${2:-1200}
case "$rom" in /*) ;; *) echo "Use an absolute ROM path" >&2; exit 2;; esac
case "$frames" in ''|*[!0-9]*) echo "Frame count must be numeric" >&2; exit 2;; esac
[ "$frames" -ge 120 ] && [ "$frames" -le 3600 ] || { echo "Use 120..3600 frames" >&2; exit 2; }
[ -r "$rom" ] || { echo "ROM not readable: $rom" >&2; exit 2; }
home=${FLXTR_RETROARCH_HOME:-/home/apps/retroarch}
runtime="$home/retroarch.exec"
config="$home/.home/.config/retroarch/retroarch.cfg"
core="$home/cores/snes9x2005_libretro.so"
[ -r "$core" ] || core="$home/.home/.config/retroarch/cores/snes9x2005_libretro.so"
[ -x "$runtime" ] && [ -r "$config" ] && [ -r "$core" ] || { echo "RetroArch runtime, config or SNES9x 2005 core missing" >&2; exit 2; }
tmp=$(mktemp -d /tmp/flxtr-video-bench.XXXXXX) || exit 2
trap 'rm -rf "$tmp"' 0 1 2 15
echo "Snes9x 2005 / $frames frames / same ROM for every run"
echo "Comparative end-to-end throughput, including startup and shutdown."
echo "PROFILE                  ELAPSED    THROUGHPUT"
for profile in baseline threaded no_audio threaded_no_audio; do
    threaded=false
    audio=true
    case "$profile" in
        threaded) threaded=true ;;
        no_audio) audio=false ;;
        threaded_no_audio) threaded=true; audio=false ;;
    esac
    cat > "$tmp/$profile.cfg" <<EOF
config_save_on_exit = "false"
video_vsync = "false"
audio_sync = "false"
video_threaded = "$threaded"
audio_enable = "$audio"
video_shader_enable = "false"
video_smooth = "false"
EOF
    start=$(awk '{printf "%.3f", $1}' /proc/uptime)
    if (cd "$home" && HOME="$home/.home" "$runtime" --config "$config" --appendconfig "$tmp/$profile.cfg" --max-frames "$frames" --libretro "$core" "$rom") >"$tmp/$profile.log" 2>&1; then
        finish=$(awk '{printf "%.3f", $1}' /proc/uptime)
        awk -v n="$profile" -v frames="$frames" -v t0="$start" -v t1="$finish" 'BEGIN {
            dt=t1-t0; if(dt<=0)dt=.001;
            printf "%-23s %7.2f s %8.2f FPS\n",n,dt,frames/dt
        }'
    else
        echo "$profile: failed; final log lines:"
        tail -10 "$tmp/$profile.log"
    fi
done
echo "If threaded wins, test actual gameplay for judder and latency."
echo "If disabling audio wins, investigate the RetroArch audio driver."
echo "No permanent configuration settings were changed."
