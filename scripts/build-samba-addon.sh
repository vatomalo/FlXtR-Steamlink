#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build/samba-arm-keys
source=$(docker create --platform linux/arm/v7 alpine:3.21@sha256:ce64758a109eb420d874a118f87920e625e12d3634e03b4a5573fd9f6e5d3507)
trap 'docker rm "$source" >/dev/null' EXIT
docker cp "$source:/etc/apk/keys/." build/samba-arm-keys/
docker run --rm --mount "type=bind,src=$PWD/build,dst=/work" alpine:3.23@sha256:85fe1e81d6758c208f3e1eed4338a1997e19d4be002d4dd32d3100c9a8c010a0 sh -ec '
r=/tmp/flxtr-samba
mkdir -p "$r/etc/apk/keys"
cp /work/samba-arm-keys/* "$r/etc/apk/keys/"
apk --arch armv7 --root "$r" --initdb --no-scripts --repositories-file /etc/apk/repositories add samba-server=4.22.10-r0 samba-common-tools=4.22.10-r0
apk --root "$r" info -v > "$r/PACKAGES.txt"
cp "$r/lib/apk/db/installed" "$r/APK-PROVENANCE.txt"
tar -C "$r" -czf /work/samba-runtime-probe.tgz lib usr etc PACKAGES.txt APK-PROVENANCE.txt
du -sh "$r"
'
mkdir -p build/samba-runtime dist
docker run --rm --mount "type=bind,src=$PWD,dst=/src" --entrypoint /bin/bash steamlink-sdk:latest -c '
set -e
tar -xzf /src/build/samba-runtime-probe.tgz -C /src/build/samba-runtime
source /opt/steamlink-sdk/setenv.sh
$CC -Os -Wall -Wextra -Werror -fPIC -shared -nostdlib /src/src/samba_paths.c -o /src/build/samba-runtime/flxtr-samba-paths.so
mkdir -p /src/build/samba-addon/greenlink
cp -a /src/build/samba-runtime /src/build/samba-addon/greenlink/
cp /src/packaging/samba-run.sh /src/packaging/samba-server.sh /src/build/samba-addon/greenlink/
cp /src/docs/STEAMLINK-SAMBA.md /src/build/samba-addon/greenlink/SAMBA-README.md
tar -C /src/build/samba-addon -czf /src/dist/FlXtR-SteamLink-Samba-armv7.tgz greenlink
'
sha256sum dist/FlXtR-SteamLink-Samba-armv7.tgz >dist/FlXtR-SteamLink-Samba-armv7.tgz.sha256
