#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
FLXTR_DISABLE_AUTO_UPDATE=1 bash scripts/build-steamlink.sh
target=dist/frankenstein/apps/flxtr-frankenstein
if [ -e "$target" ]; then
    echo "Frankenstein staging directory already exists: $target" >&2
    exit 1
fi
mkdir -p "$(dirname "$target")"
cp -a dist/steamlink/apps/greenlink "$target"
printf 'name=FlXtR Frankenstein\nicon=assets/icon.bmp\nrun=greenlink.sh\n' > "$target/toc.txt"
printf '#!/bin/sh\necho "Frankenstein updates require rebuilding the experimental branch."\nexit 1\n' > "$target/update.sh"
chmod +x "$target/update.sh"
tar -C dist/frankenstein/apps -czf dist/flxtr-frankenstein.tgz flxtr-frankenstein
sha256sum dist/flxtr-frankenstein.tgz
