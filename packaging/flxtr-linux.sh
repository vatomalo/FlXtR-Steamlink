#!/bin/sh
set -u
FLXTR_HOME=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export FLXTR_HOME
mkdir -p "$FLXTR_HOME/data"
cd "$FLXTR_HOME/data" || exit 1
if [ ! -f .no-auto-update ]; then "$FLXTR_HOME/current/update.sh" >update.log 2>&1 || true; fi
for item in greenlink greenlink-catalog greenlink-player greenlink-resolver linux-controls.lua player-menu-v1 assets certs update.sh; do
    if [ -e "$FLXTR_HOME/current/$item" ]; then ln -sfn "../current/$item" "$item"; fi
done
if [ ! -e catalog.tsv ]; then cp "$FLXTR_HOME/current/catalog.tsv" catalog.tsv; fi
if [ "$#" -eq 0 ] && [ -f catalog.local.tsv ]; then set -- --catalog catalog.local.tsv; fi
while :; do
    ./greenlink "$@" >>greenlink.log 2>&1
    code=$?
    [ "$code" -eq 42 ] || exit "$code"
done
