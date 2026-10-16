#!/bin/sh
# Start the bundled runtime without installing a libc into the firmware.
set -eu
cd "$(dirname "$0")/.."
r="$PWD/music-runtime"
if [ -x "$r/lib/ld-musl-armhf.so.1" ]; then
    export PYTHONHOME="$r/usr"
    export SSL_CERT_FILE="$PWD/certs/cacert.pem"
    export PYTHONDONTWRITEBYTECODE=1
    exec "$r/lib/ld-musl-armhf.so.1" --library-path "$r/lib:$r/usr/lib" "$r/usr/bin/python3" "$@"
fi
exec python3 "$@"
