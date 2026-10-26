#!/bin/sh
# Firmware API: system bus, uint32 activity (0 idle, 1 interactive).
# Refreshing activity resets the idle/countdown timers; timeout files stay unchanged.
set -u
activity() {
    dbus-send --system --type=method_call --dest=system.powermanager \
        /powermanager system.powermanager.SetActivity "uint32:$1"
}
case "${1:-}" in
    --once) activity 1; exit $? ;;
    --idle) activity 0; exit $? ;;
    ''|*[!0-9]*) echo 'Usage: power-activity.sh PID | --once | --idle' >&2; exit 2 ;;
esac
target=$1
[ "$target" -gt 1 ] || exit 2
command -v dbus-send >/dev/null 2>&1 || exit 0
identity() { sed 's/^.*) //' "/proc/$target/stat" 2>/dev/null | awk '{print $1, $20}'; }
original=$(identity)
case "$original" in ''|Z\ *|X\ *) exit 0 ;; esac
started=0
sleeper=
cleanup() {
    trap - 0 1 2 15
    if [ -n "$sleeper" ]; then kill "$sleeper" 2>/dev/null || true; wait "$sleeper" 2>/dev/null || true; fi
    [ "$started" -eq 0 ] || activity 0
}
trap cleanup 0
trap 'exit 0' 1 2 15
while kill -0 "$target" 2>/dev/null; do
    current=$(identity)
    case "$current" in ''|Z\ *|X\ *) break ;; esac
    [ "${current#* }" = "${original#* }" ] || break
    activity 1 || break
    started=1
    sleep "${FLXTR_POWER_INTERVAL:-10}" &
    sleeper=$!
    wait "$sleeper" || true
    sleeper=
done
