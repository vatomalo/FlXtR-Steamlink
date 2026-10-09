#!/bin/sh
cd "$(dirname "$0")" || exit 1
# The box has no reliable clock after some cold boots. Keep HTTPS validation on.
if [ "$(date +%Y)" -lt 2024 ] && command -v ntpd >/dev/null 2>&1; then
    ntpd -n -q -p pool.ntp.org >clock.log 2>&1 &
    clock_pid=$!
    tries=0
    while [ "$tries" -lt 10 ] && [ "$(date +%Y)" -lt 2024 ]; do
        sleep 1
        tries=$((tries + 1))
    done
    kill "$clock_pid" 2>/dev/null || true
    wait "$clock_pid" 2>/dev/null || true
fi
export SDL_GAMECONTROLLERCONFIG="${SDL_GAMECONTROLLERCONFIG:-}"
if [ "$#" -eq 0 ] && [ -f catalog.local.tsv ]; then
    set -- --catalog catalog.local.tsv
fi
# A failed/offline check must never prevent launch. No credentials on the device.
if [ ! -f .no-auto-update ]; then sh ./update.sh >update.log 2>&1 || true; fi
while :; do
    ./greenlink "$@" >>greenlink.log 2>&1
    code=$?
    if [ "$code" -eq 43 ]; then
        ./greenlink-catalog --run-game >game.log 2>&1
        printf '%d\n' "$?" >game-exit-status
        set -- --library games
        continue
    fi
    [ "$code" -eq 42 ] || exit "$code"
done
