#!/usr/bin/env bash
# Compile inside Ubuntu 22.04, then package Linux and Steam Deck installations.
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ "${1:-}" != --inside ]]; then
    [[ $# -eq 0 ]] || { echo "Usage: $0" >&2; exit 2; }
    docker build --platform linux/amd64 -f Dockerfile.desktop -t flxtr-desktop-builder:local .
    exec docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" \
        --mount "type=bind,src=$PWD,dst=/src" --env HOME=/tmp \
        --env GREENLINK_BUILD_CACHE=/src/build/desktop-cache \
        flxtr-desktop-builder:local bash scripts/build-desktop.sh --inside
fi
[[ $(uname -m) == x86_64 ]] || { echo "Steam Deck requires x86_64" >&2; exit 1; }
bash scripts/build-linux.sh
python3 scripts/package-desktop.py
python3 tests/linux_update_test.py
python3 tests/linux_install_test.py dist/linux-x86_64.json
