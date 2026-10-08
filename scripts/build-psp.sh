#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
FLXTR_VERSION=${FLXTR_VERSION:-$(git -c safe.directory="$PWD" rev-parse HEAD)}
FLXTR_BUILD=${FLXTR_BUILD:-$(git -c safe.directory="$PWD" show -s --format=%ct HEAD)}
[[ $FLXTR_VERSION =~ ^[a-f0-9]{40}$ && $FLXTR_BUILD =~ ^[0-9]+$ ]] || { echo 'Invalid build identity.' >&2; exit 1; }
export FLXTR_VERSION FLXTR_BUILD
if ! command -v psp-gcc >/dev/null 2>&1; then
    [[ ${1:-} != --inside ]] || { echo 'PSP compiler missing inside the selected Docker image.' >&2; exit 1; }
    exec docker run --rm --entrypoint bash --user "$(id -u):$(id -g)" \
        -e FLXTR_VERSION -e FLXTR_BUILD -v "$PWD:/source" -w /source \
        "${FLXTR_PSP_IMAGE:-garden-gaiden-psp-sdk}" scripts/build-psp.sh --inside
fi
stage=build/psp/source
mkdir -p "$stage/third_party" dist/psp/FlXtR
cp -R psp "$stage/"
cp -R third_party/tilefinch-media "$stage/third_party/"
make -C "$stage/psp" clean
make -C "$stage/psp" -j"${JOBS:-4}" VERSION="$FLXTR_VERSION" BUILD="$FLXTR_BUILD" 2>&1 | tee build/psp/build.log
if grep -q 'could not fixup imports' build/psp/build.log; then
    echo 'PSP firmware import table invalid; refusing to package.' >&2; exit 1
fi
if [[ ! -f build/cacert.pem ]]; then
    wget -q https://curl.se/ca/cacert-2026-09-25.pem -O build/cacert.pem
fi
echo 'a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505  build/cacert.pem' | sha256sum -c -
cp "$stage/psp/EBOOT.PBP" psp/streams.tsv dist/psp/FlXtR/
cp build/cacert.pem dist/psp/FlXtR/
cp docs/PSP-STATUS.md dist/psp/FlXtR/README.md
cp third_party/tilefinch-media/LICENSE dist/psp/FlXtR/Tilefinch-LICENSE.txt
cp LICENSE dist/psp/FlXtR/FlXtR-LICENSE.txt
cp packaging/CERTIFICATES.txt dist/psp/FlXtR/
mkdir -p dist/psp/FlXtR/licenses
for name in pspsdk newlib mbedtls zlib cjson; do
    cp -R "$PSPDEV/psp/share/licenses/$name" dist/psp/FlXtR/licenses/
done
cp packaging/psp-licenses/* dist/psp/FlXtR/licenses/
sha=$(sha256sum dist/psp/FlXtR/EBOOT.PBP | cut -d' ' -f1)
printf '{"platform":"psp","version":"%s","build":%s,"sha256":"%s"}\n' "$FLXTR_VERSION" "$FLXTR_BUILD" "$sha" > dist/psp/psp.json
echo "PSP test build: $PWD/dist/psp/FlXtR/EBOOT.PBP"
