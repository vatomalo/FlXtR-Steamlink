#!/bin/sh
cd "$(dirname "$0")" || exit 1
export SDL_GAMECONTROLLERCONFIG="${SDL_GAMECONTROLLERCONFIG:-}"
exec ./greenlink "$@" >>greenlink.log 2>&1
