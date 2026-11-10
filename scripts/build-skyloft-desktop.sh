#!/usr/bin/env bash
set -euo pipefail
storage=/opt/dockers/storage
repo=${FLXTR_BUILD_SOURCE:-$storage/projects/FlXtR/source}
case "${1:-}" in --linux) target=linux; artifact=FlXtR-Linux-x86_64.tgz;; --windows) target=windows; artifact=FlXtR-Windows-x86_64.zip;; *) echo 'Choose --linux or --windows.' >&2; exit 2;; esac
mountpoint -q "$storage"
mkdir -p "$storage/build-home" "$storage/build-work" "$storage/build-cache/desktop" "$storage/build-cache/windows-sdk" "$storage/build-cache/desktop-music"
if [[ ${FLXTR_BUILD_LOCK_HELD:-0} != 1 ]]; then
    exec 6>"$storage/build-home/flxtr-build.lock"
    flock -n 6 || { echo 'Another FlXtR build is running.' >&2; exit 1; }
fi
docker build -f "$repo/Dockerfile.desktop" -t flxtr-desktop-builder:skyloft "$repo"
sha=$(git -C "$repo" rev-parse HEAD)
# A fresh checkout makes repeated packages reproducible without deleting a
# previous extracted package or sharing Linux/Windows intermediate binaries.
workspace=$(mktemp -d "$storage/build-work/desktop-$target.XXXXXX")
git clone --quiet --shared --no-checkout "$repo" "$workspace/source"
git -C "$workspace/source" checkout --quiet --detach "$sha"
mkdir -p "$workspace/source/build/windows-sdk" "$workspace/source/build/desktop-music"
echo "Desktop build workspace: $workspace"
docker run --rm --name "flxtr-$target-build" --cpus=3 --memory=5g \
    --user "$(id -u):$(id -g)" -e HOME=/tmp -e JOBS=3 \
    -e GREENLINK_BUILD_CACHE=/cache \
    -v "$workspace/source:/src" -v "$repo/.git:$repo/.git:ro" \
    -v "$storage/build-cache/desktop:/cache" \
    -v "$storage/build-cache/windows-sdk:/src/build/windows-sdk" \
    -v "$storage/build-cache/desktop-music:/src/build/desktop-music" \
    flxtr-desktop-builder:skyloft bash -c '
        set -euo pipefail
        if [[ "$1" == linux ]]; then FLXTR_DESKTOP_EDITION=Linux bash scripts/build-desktop.sh --inside;
        else python3 scripts/build-windows-app.py; fi
    ' bash "$target"
mkdir -p "$repo/dist"
cp "$workspace/source/dist/$artifact" "$workspace/source/dist/$artifact.sha256" "$repo/dist/"
python3 - "$repo" "$target" "$artifact" "$sha" <<'RECEIPT'
import hashlib,json,pathlib,sys
root,target,name,commit=sys.argv[1:]
file=pathlib.Path(root)/'dist'/name
receipt=dict(platform=target,architecture='x86_64',commit=commit,artifact=name,size=file.stat().st_size,
             sha256=hashlib.file_digest(file.open('rb'),'sha256').hexdigest())
(file.parent/(target+'-desktop-build.json')).write_text(json.dumps(receipt,indent=2)+'\n')
print(f'Finished package: {file}')
RECEIPT
# This exact newly-created workspace contains only generated build files.
[[ "$workspace" == "$storage/build-work/desktop-$target."* ]] && rm -rf -- "$workspace"
