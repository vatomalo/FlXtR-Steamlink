#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
r="$PWD/music-runtime"
if [ -x "$r/lib/ld-musl-armhf.so.1" ]; then
    exec "$r/lib/ld-musl-armhf.so.1" --library-path "$r/lib:$r/usr/lib" "$r/usr/bin/qjs" "$@"
fi
if [ -x "$r/bin/qjs" ]; then exec "$r/bin/qjs" "$@"; fi
exec qjs "$@"
