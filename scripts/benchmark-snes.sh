#!/bin/sh
# Run on the Steam Link while FlXtR/RetroArch are not using the display.
# Usage: sh scripts/benchmark-snes.sh /absolute/path/to/game.sfc [frames]
set -eu
rom=${1:-}
frames=${2:-1200}
case "$rom" in /*) ;; *) echo "Use an absolute ROM path" >&2; exit 2;; esac
case "$frames" in *[!0-9]*|'') echo "Frames must be numeric" >&2; exit 2;; esac
[ "$frames" -ge 120 ] && [ "$frames" -le 3600 ] || { echo "Frames must be 120..3600" >&2; exit 2; }
[ -r "$rom" ] || { echo "ROM is not readable" >&2; exit 2; }
home=${FLXTR_RETROARCH_HOME:-/home/apps/retroarch}
runtime="$home/retroarch.exec"
config="$home/.home/.config/retroarch/retroarch.cfg"
[ -x "$runtime" ] && [ -r "$config" ] || { echo "RetroArch binary/config missing" >&2; exit 2; }
tmp=$(mktemp -d /tmp/flxtr-snes-bench.XXXXXX)
trap 'rm -rf "$tmp"' 0 1 2 15
# Uncap rendering, retaining the same settings for each core.
cat > "$tmp/benchmark.cfg" <<'EOF'
config_save_on_exit = "false"
video_vsync = "false"
audio_sync = "false"
video_threaded = "false"
EOF
echo "Comparing $frames frames per core, VSync off, threaded video off"
for name in snes9x2002 snes9x2002_flxtr snes9x2005; do
    core="$home/cores/${name}_libretro.so"
    [ -r "$core" ] || core="$home/.home/.config/retroarch/cores/${name}_libretro.so"
    if [ ! -r "$core" ]; then echo "$name: unavailable"; continue; fi
    start=$(awk '{printf "%.3f", $1}' /proc/uptime)
    if (cd "$home" && HOME="$home/.home" "$runtime" --config "$config" --appendconfig "$tmp/benchmark.cfg" --max-frames "$frames" --libretro "$core" "$rom") >"$tmp/$name.log" 2>&1; then
        finish=$(awk '{printf "%.3f", $1}' /proc/uptime)
        awk -v n="$name" -v frames="$frames" -v start="$start" -v finish="$finish" 'BEGIN { t=finish-start; if(t<=0) t=.001; printf "%-22s %7.2f s %7.2f FPS (%0.1f%% of 60 FPS)\n",n,t,frames/t,frames/t/60*100 }'
    else
        echo "$name: benchmark failed; final log lines:"
        tail -8 "$tmp/$name.log"
    fi
done
echo "Results are comparative throughput, not necessarily in-game perceived FPS."
