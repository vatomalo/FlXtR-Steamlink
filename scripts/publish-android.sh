#!/usr/bin/env bash
# Immutable APK payload, then advance only the Android preview channel.
set -euo pipefail
cd "$(dirname "$0")/.."
[ -z "$(git status --porcelain)" ] || { echo 'Commit and rebuild before publishing.' >&2; exit 1; }
repo=vatomalo/FlXtR-Steamlink
commit=$(git rev-parse HEAD)
python3 - "$commit" <<'PY'
import hashlib,json,re,sys,zipfile
from pathlib import Path
source=Path('android/app/build.gradle').read_text()
code=int(re.search(r'versionCode (\d+)',source)[1])
name=re.search(r"versionName '([^']+)'",source)[1]
apk=Path('dist/FlXtR-Android-preview.apk')
# Gradle metadata comes from this build, not a guessed release tag.
metadata=json.loads(Path('android/app/build/outputs/apk/debug/output-metadata.json').read_text())
assert metadata['applicationId']=='org.flxtr.android'
assert metadata['elements'][0]['versionCode']==code and metadata['elements'][0]['versionName']==name
assert apk.read_bytes()==Path('android/app/build/outputs/apk/debug/app-debug.apk').read_bytes()
with zipfile.ZipFile(apk) as z:
    for abi in ('armeabi-v7a','arm64-v8a'): assert f'lib/{abi}/libflxtr_resolver.so' in z.namelist()
tag=f'android-{code}-{sys.argv[1][:12]}'
manifest=dict(package='org.flxtr.android',versionCode=code,versionName=name,minSdk=26,size=apk.stat().st_size,sha256=hashlib.sha256(apk.read_bytes()).hexdigest(),url=f'https://github.com/vatomalo/FlXtR-Steamlink/releases/download/{tag}/{apk.name}')
Path('dist/android.json').write_text(json.dumps(manifest,indent=2)+'\n')
Path('dist/android-tag.txt').write_text(tag)
PY
tag=$(cat dist/android-tag.txt)
python3 scripts/package-android-source.py
gh release create "$tag" --repo "$repo" --target "$commit" --draft --prerelease \
    --title "FlXtR Android preview ${tag#android-}" --notes-file docs/ANDROID-RELEASE.md \
    dist/FlXtR-Android-preview.apk dist/android.json dist/FlXtR-Android-source.zip
gh release edit "$tag" --repo "$repo" --draft=false --prerelease
if ! gh release view android-latest --repo "$repo" >/dev/null 2>&1; then
    gh release create android-latest --repo "$repo" --target "$commit" --prerelease \
        --title 'FlXtR Android preview update channel' \
        --notes 'Android-only preview channel. FlXtR checks this manifest; Android asks before installing. Requires Android 8 or later.'
fi
gh release upload android-latest dist/android.json --repo "$repo" --clobber
echo "https://github.com/$repo/releases/tag/$tag"
