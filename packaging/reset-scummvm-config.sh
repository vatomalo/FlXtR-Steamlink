#!/bin/sh
# Explicit one-shot reset, never called by launch or update.
set -eu
app=${1:-/home/apps/scummvm}
case "$app" in /*) ;; *) echo "Use an absolute ScummVM app path" >&2; exit 2;; esac
if command -v pidof >/dev/null 2>&1 && pidof scummvm scummvm.exec >/dev/null 2>&1; then
    echo "Close ScummVM before resetting its config." >&2; exit 2
fi
backup="$app-config-backup-$(date +%Y%m%d-%H%M%S)-$$"
move_config() {
    [ -f "$1" ] || return 0
    mkdir -p "$backup"
    chmod 700 "$backup"
    destination="$backup/$(printf '%s' "$1" | sed 's,/,_,g')"
    mv "$1" "$destination"
    printf 'Reset: %s (backup: %s)\n' "$1" "$destination"
}
export backup
if [ -d "$app" ]; then
    find "$app" -type f \( -name scummvm.ini -o -name .scummvmrc \) -exec sh -c '
        for config do
            mkdir -p "$backup"; chmod 700 "$backup"
            target="$backup/$(printf "%s" "$config" | sed "s,/,_,g")"
            mv "$config" "$target" || exit 1
            printf "Reset: %s (backup: %s)\n" "$config" "$target"
        done
    ' sh {} +
fi
move_config "${XDG_CONFIG_HOME:-${HOME:-/root}/.config}/scummvm/scummvm.ini"
move_config "${HOME:-/root}/.scummvmrc"
# Earlier ROM-menu launches inherited FlXtR's home instead of ScummVM's.
move_config "$app/../greenlink/.home/.config/scummvm/scummvm.ini"
move_config "$app/../greenlink/.home/.scummvmrc"
move_config "$app/../greenlink/.config/scummvm/scummvm.ini"
if [ -d "$backup" ]; then echo "Restart ScummVM to use defaults. Games and saves were kept."
else echo "No ScummVM config found in app/home locations; check the launcher for a custom --config path."; fi
