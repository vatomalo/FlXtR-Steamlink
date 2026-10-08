#!/usr/bin/env bash
# Publish immutable beta payload first, then advance only its Linux channel.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -z "$(git status --porcelain)" ] || { echo 'Commit and rebuild before publishing.' >&2; exit 1; }
version=$(git rev-parse HEAD)
target=linux-$(uname -m)
python3 - "$version" "$target" <<'PY'
import hashlib,json,sys
from pathlib import Path
version,target=sys.argv[1:]
manifest=json.loads(Path('dist/'+target+'.json').read_text())
assert manifest['version']==version and manifest['platform']==target
assert manifest['sha256']==hashlib.sha256(Path('dist/'+target+'.tgz').read_bytes()).hexdigest()
PY
repo=vatomalo/FlXtR-Steamlink
tag=$target-$version
gh release create "$tag" --repo "$repo" --target "$version" --draft --prerelease \
    --title "FlXtR Linux beta ${version:0:8}" --notes-file docs/LINUX-RELEASE.md \
    "dist/$target.tgz" "dist/$target.json" "dist/FlXtR-$target-install.tgz" scripts/autobuild
gh release edit "$tag" --repo "$repo" --draft=false --prerelease
channel=$target-latest
if ! gh release view "$channel" --repo "$repo" >/dev/null 2>&1; then
    gh release create "$channel" --repo "$repo" --target "$version" --prerelease \
        --title "FlXtR $target beta update channel" \
        --notes 'Platform-specific manifest. Install the full Linux beta package from its versioned release.'
fi
gh release upload "$channel" "dist/$target.json" scripts/autobuild --repo "$repo" --clobber
echo "https://github.com/$repo/releases/tag/$tag"
