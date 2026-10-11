#!/bin/sh
# Benchmark display-resolution overhead on Steam Link, Snes9x 2005.
# Usage: sh benchmark-snes-resolution.sh /absolute/path/to/game.smc [frames]
# Close FlXtR and RetroArch first. Requires a working video display.
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
[ -x "$runtime" ] && [ -r "$config" ] && [ -r "$core" ] || { echo "Missing RetroArch, configuration or core" >&2; exit 2; }
tmp=$(mktemp -d /tmp/flxtr-resolution.XXXXXX) || exit 2
trap 'rm -rf "$tmp"' 0 1 2 15
echo "Snes9x 2005, $frames frames, identical core/ROM"
echo "NOTE: the video driver may ignore resolution requests."
echo "PROFILE          ELAPSED    EFFECTIVE FPS"
for entry in native 640x480 960x540 1280x720 1920x1080; do
    case "$entry" in
        native) width=0; height=0 ;;
        *) width=${entry%x*}; height=${entry#*x} ;;
    esac
    cat > "$tmp/$entry.cfg" <<EOF
config_save_on_exit = "false"
video_vsync = "false"
audio_sync = "false"
video_threaded = "false"
video_shader_enable = "false"
video_smooth = "false"
video_fullscreen = "true"
video_windowed_fullscreen = "false"
video_fullscreen_x = "$width"
video_fullscreen_y = "$height"
EOF
    start=$(awk '{printf "%.3f", $1}' /proc/uptime)
    if (cd "$home" && HOME="$home/.home" "$runtime" --verbose --config "$config" --appendconfig "$tmp/$entry.cfg" --max-frames "$frames" --libretro "$core" "$rom") >"$tmp/$entry.log" 2>&1; then
        end=$(awk '{printf "%.3f", $1}' /proc/uptime)
        awk -v n="$entry" -v f="$frames" -v a="$start" -v b="$end" 'BEGIN{d=b-a;if(d<=0)d=.001;printf "%-16s %7.2f s %8.2f FPS\n",n,d,f/d}'
        echo "  Driver hints:"
        grep -i -E 'resolution|video mode|video size|fullscreen|set video|screen size' "$tmp/$entry.log" | tail -3 || true
    else
        echo "$entry: FAILED"
        tail -6 "$tmp/$entry.log"
    fi
done
echo "Important: identical results may mean the driver kept the same output resolution."
echo "Values include core startup/shutdown and are not actual in-game FPS."
echo "No permanent RetroArch config changed."
