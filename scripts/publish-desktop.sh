#!/usr/bin/env bash
# Publish both bundled desktop editions together without advancing updater channels.
set -euo pipefail
cd "$(dirname "$0")/.."
[[ -z "$(git status --porcelain)" ]] || { echo "Commit changes and rebuild before publishing." >&2; exit 1; }
version=$(git rev-parse HEAD)
python3 - "$version" <<'PY'
import hashlib, json, sys, tarfile
from pathlib import Path
for edition in ('Linux', 'SteamDeck'):
    name = 'FlXtR-' + edition + '-x86_64'
    archive = Path('dist') / (name + '.tgz')
    expected = Path(str(archive) + '.sha256').read_text().split()[0]
    assert hashlib.sha256(archive.read_bytes()).hexdigest() == expected, 'Checksum mismatch'
    with tarfile.open(archive) as bundle:
        metadata = json.load(bundle.extractfile(name + '/releases/' + sys.argv[1] + '/build.json'))
        assert metadata['version'] == sys.argv[1] and metadata['platform'] == 'linux-x86_64'
PY
repo=vatomalo/FlXtR-Steamlink
tag=desktop-${version}
gh release create "$tag" --repo "$repo" --target "$version" --draft --prerelease \
    --title "FlXtR Linux and Steam Deck beta ${version:0:8}" \
    --notes-file docs/DESKTOP-RELEASE.md \
    dist/FlXtR-Linux-x86_64.tgz dist/FlXtR-Linux-x86_64.tgz.sha256 \
    dist/FlXtR-SteamDeck-x86_64.tgz dist/FlXtR-SteamDeck-x86_64.tgz.sha256
gh release edit "$tag" --repo "$repo" --draft=false --prerelease
echo "https://github.com/$repo/releases/tag/$tag"
