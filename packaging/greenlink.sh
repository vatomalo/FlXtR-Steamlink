#!/bin/sh
cd "$(dirname "$0")" || exit 1
export SDL_GAMECONTROLLERCONFIG="${SDL_GAMECONTROLLERCONFIG:-}"
if [ "$#" -eq 0 ] && [ -f catalog.local.tsv ]; then
    set -- --catalog catalog.local.tsv
fi
exec ./greenlink "$@" >>greenlink.log 2>&1
