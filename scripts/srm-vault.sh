#!/bin/sh
# Rename or copy a RetroArch .srm save safely, without overwriting existing files.
# Usage: sh srm-vault.sh backup /path/Game.srm my-backup
#        sh srm-vault.sh rename /path/Game.srm my-other-save
# Refuse changes while ANY RetroArch game is running (conservative safety policy).
set -eu
mode=${1:-}; source=${2:-}; label=${3:-}
case "$mode" in backup|rename) ;; *) echo "Usage: sh srm-vault.sh backup|rename /absolute/Game.srm label" >&2; exit 2;; esac
case "$source" in /*.srm) ;; *) echo "An absolute .srm source path is required" >&2; exit 2;; esac
[ -f "$source" ] && [ ! -L "$source" ] || { echo "Save not found or unsafe path" >&2; exit 2; }
case "$label" in ''|*[!a-zA-Z0-9_-]*) echo "Label must use letters, numbers, _ or -" >&2; exit 2;; esac
[ "${#label}" -le 48 ] || { echo "Label too long" >&2; exit 2; }
# Never touch SRAM while the emulator might still write to it.
# Steam Link exposes process names in /proc/PID/comm; fail closed if unavailable.
retroarch_stopped() {
    [ -r /proc/self/comm ] || { echo "Cannot inspect running processes; refusing SRAM operation" >&2; return 1; }
    for process in /proc/[0-9]*/comm; do
        [ -e "$process" ] || continue
        [ -r "$process" ] || { echo "Cannot inspect process; refusing SRAM operation" >&2; return 1; }
        IFS= read -r name < "$process" || name=
        case "$name" in
            retroarch|retroarch.*|RetroArch|RetroArch.*)
                echo "RetroArch is running; exit the game before editing SRAM saves" >&2
                return 1 ;;
        esac
    done
    return 0
}
retroarch_stopped || exit 4
target=${source%.srm}.$label.srm
[ ! -e "$target" ] && [ ! -L "$target" ] || { echo "Backup already exists: $target" >&2; exit 3; }
retroarch_stopped || exit 4
if [ "$mode" = backup ]; then
    # Exclusive creation avoids accidental replacement; remove incomplete output on failure.
    (set -C; : > "$target") || { echo "Cannot create destination" >&2; exit 3; }
    if ! cat "$source" > "$target"; then rm -f "$target"; exit 1; fi
else
    mv "$source" "$target"
fi
echo "$target"
