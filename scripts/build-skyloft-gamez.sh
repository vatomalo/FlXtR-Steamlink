#!/usr/bin/env bash
set -euo pipefail
storage=/opt/dockers/storage
repo=${FLXTR_BUILD_SOURCE:-$(cd "$(dirname "$0")/.." && pwd)}
mountpoint -q "$storage"
mkdir -p "$storage/build-home/gamez" "$storage/build-cache/dotnet" "$repo/dist"
exec 6>"$storage/build-home/flxtr-build.lock"
flock -n 6 || { echo 'Another FlXtR build is running'; exit 1; }
docker run --rm --name flxtr-gamez-build --cpus=3 --memory=3g \
    --user "$(id -u):$(id -g)" -e HOME=/build-home -e DOTNET_CLI_HOME=/build-home \
    -e NUGET_PACKAGES=/nuget -e DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1 \
    -v "$repo:/src" -v "$storage/build-home/gamez:/build-home" \
    -v "$storage/build-cache/dotnet:/nuget" -w /src \
    mcr.microsoft.com/dotnet/sdk:9.0 \
    dotnet publish companion/FlXtR.Gamez -c Release -r win-x64 \
        --self-contained true -p:EnableWindowsTargeting=true -o build/gamez/publish
python3 - "$repo" <<'INFO'
import json,pathlib,subprocess,sys
root=pathlib.Path(sys.argv[1])
(root/'build/gamez/publish/gamez-build.json').write_text(json.dumps({'commit':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),'format':1})+'\n')
INFO
cp "$repo/companion/Start-Gamez.ps1" "$repo/build/gamez/publish/"
cp "$repo/companion/Update-And-Start.ps1" "$repo/build/gamez/publish/"
cp "$repo/companion/Connect-SteamLink.ps1" "$repo/build/gamez/publish/"
cp "$repo/companion/Test-Watchdog.ps1" "$repo/build/gamez/publish/"
python3 - "$repo" <<'PY'
import hashlib,pathlib,sys,zipfile
root=pathlib.Path(sys.argv[1]); output=root/'dist/FlXtR-Gamez-win-x64.zip'
with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as package:
    for path in sorted((root/'build/gamez/publish').rglob('*')):
        if path.is_file():package.write(path,path.relative_to(root/'build/gamez/publish'))
digest=hashlib.file_digest(output.open('rb'),'sha256').hexdigest()
output.with_suffix('.zip.sha256').write_text(f'{digest}  {output.name}\n')
print('Built Windows Gamez companion on SkyLoft:',output)
PY
