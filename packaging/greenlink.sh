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
    [ "$code" -eq 42 ] || exit "$code"
done
