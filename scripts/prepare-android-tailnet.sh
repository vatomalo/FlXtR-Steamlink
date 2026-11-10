#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo/android/tailnet"
export ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk/28.0.13004108}"
mkdir -p ../app/libs
stamp=$(cat ./*.go go.mod go.sum | sha256sum | cut -d' ' -f1)
go test ./...
if [[ ! -f ../app/libs/flxtr-tailnet.aar || ! -f ../app/libs/tailnet.sha256 || $(cat ../app/libs/tailnet.sha256) != "$stamp" ]]; then
    gomobile bind -target=android/arm,android/arm64 -androidapi=26 \
        -ldflags='-s -w -linkmode=external -extldflags=-Wl,-z,max-page-size=16384' \
        -o ../app/libs/flxtr-tailnet.aar .
    printf '%s\n' "$stamp" > ../app/libs/tailnet.sha256
fi
# Ship the license texts for the modules linked into the embedded node.
go list -m -json all | python3 -c '
import json,sys
from pathlib import Path
raw=sys.stdin.read();decoder=json.JSONDecoder();out=Path("../build/media-runtime/runtime/licenses/tailnet");out.mkdir(parents=True,exist_ok=True)
while raw.strip():
    module,n=decoder.raw_decode(raw.lstrip());raw=raw.lstrip()[n:]
    directory=module.get("Dir")
    if not directory:continue
    for source in Path(directory).glob("*"):
        if source.is_file() and source.name.upper().startswith(("LICENSE","COPYING","NOTICE","AUTHORS")):
            (out/(module["Path"].replace("/","_")+"-"+source.name)).write_bytes(source.read_bytes())
'
