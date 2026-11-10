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
# Keep the host interpreter free of bundled Ubuntu C libraries.
if [ -n "${FLXTR_HOST_LIBRARY_PATH_SET-}" ]; then
    if [ -n "${FLXTR_HOST_LIBRARY_PATH-}" ]; then export LD_LIBRARY_PATH="$FLXTR_HOST_LIBRARY_PATH"; else unset LD_LIBRARY_PATH; fi
fi
export PYTHONPATH="$r/site-packages"
export SSL_CERT_FILE="$PWD/certs/cacert.pem"
export PYTHONDONTWRITEBYTECODE=1
exec python3 "$@"
