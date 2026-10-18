#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
root=$PWD
mkdir -p build/platform
"${CC:-cc}" -std=c99 -O2 -Wall -Wextra -Werror tests/platform_test.c -o build/platform/platform-test
run=$(mktemp -d "$root/build/platform/run.XXXXXX")
(cd "$run" && "$root/build/platform/platform-test")
rmdir "$run"
