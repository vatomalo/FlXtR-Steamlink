#!/bin/sh
# Explicit musl loader; never export library/preload paths into other apps.
set -eu
BASE=$(CDPATH= cd -- "$(dirname "$0")" && pwd -P)
R="$BASE/samba-runtime"
program=${1:-}
case "$program" in ''|*[!a-zA-Z0-9_-]*) echo "Invalid Samba program" >&2; exit 2;; esac
shift
binary=
for folder in usr/bin usr/sbin usr/lib/samba; do
    if [ -x "$R/$folder/$program" ]; then binary="$R/$folder/$program"; break; fi
done
[ -n "$binary" ] || { echo "Samba program unavailable: $program" >&2; exit 2; }
loader="$R/lib/ld-musl-armhf.so.1"
[ -x "$loader" ] && [ -f "$R/flxtr-samba-paths.so" ] || { echo "Install the Steam Link Samba add-on first" >&2; exit 2; }
export FLXTR_SAMBA_RUNTIME="$R" FLXTR_SAMBA_HELPERS="$BASE/samba/helpers"
unset LD_PRELOAD LD_LIBRARY_PATH
if [ -n "${FLXTR_SAMBA_EMULATOR:-}" ]; then
    exec "$FLXTR_SAMBA_EMULATOR" "$loader" --library-path "$R:$R/lib:$R/usr/lib:$R/usr/lib/samba" --preload flxtr-samba-paths.so "$binary" "$@"
fi
exec "$loader" --library-path "$R:$R/lib:$R/usr/lib:$R/usr/lib/samba" --preload flxtr-samba-paths.so "$binary" "$@"
