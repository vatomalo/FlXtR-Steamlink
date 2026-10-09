#!/bin/sh
cd "$(dirname "$0")" || exit 1
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
