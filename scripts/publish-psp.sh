#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
[[ -z $(git status --porcelain) ]] || { echo 'Commit and rebuild before publishing.' >&2; exit 1; }
version=$(git rev-parse HEAD)
python3 - "$version" <<'PY'
import hashlib,json,pathlib,sys,zipfile,struct
root=pathlib.Path('dist/psp')
m=json.loads((root/'psp.json').read_text());data=(root/'FlXtR/EBOOT.PBP').read_bytes()
assert m['platform']=='psp' and m['version']==sys.argv[1]
assert m['sha256']==hashlib.sha256(data).hexdigest()
assert data[:4]==b'\x00PBP' and sys.argv[1].encode() in data
offset=struct.unpack_from('<I',data,32)[0]
assert data[offset:offset+6]==b'\x7fELF\x01\x01' and struct.unpack_from('<H',data,offset+18)[0]==8
with zipfile.ZipFile(root/'FlXtR-PSP-beta.zip','w',zipfile.ZIP_DEFLATED) as z:
    for f in sorted((root/'FlXtR').rglob('*')):
        if f.is_file():z.write(f,f.relative_to(root))
PY
repo=vatomalo/FlXtR-Steamlink
tag=psp-$version
gh release create "$tag" --repo "$repo" --target "$version" --draft --prerelease \
    --title 'PSP experimental streaming beta' --notes-file docs/PSP-STATUS.md \
    dist/psp/FlXtR/EBOOT.PBP dist/psp/FlXtR-PSP-beta.zip dist/psp/psp.json
gh release edit "$tag" --repo "$repo" --draft=false --prerelease --latest=false
if ! gh release view psp-latest --repo "$repo" >/dev/null 2>&1; then
    gh release create psp-latest --repo "$repo" --target "$version" --prerelease --latest=false \
        --title 'PSP experimental update channel' --notes 'PSP-only beta manifest. Device playback and installation are not yet hardware-qualified.'
fi
gh release upload psp-latest --repo "$repo" dist/psp/psp.json --clobber
