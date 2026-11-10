#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
root=$PWD
mkdir -p build/platform
"${CC:-cc}" -std=c99 -O2 -Wall -Wextra -Werror tests/platform_test.c -o build/platform/platform-test
"${CC:-cc}" -std=c99 -O2 -Wall -Wextra -Werror tests/platform_ipc_test.c -pthread -o build/platform/platform-ipc-test
run=$(mktemp -d "$root/build/platform/run.XXXXXX")
(cd "$run" && "$root/build/platform/platform-test" && "$root/build/platform/platform-ipc-test")
rmdir "$run"
