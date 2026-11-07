#!/usr/bin/env python3
"""Publish an immutable APK, then advance the Android startup-update channel."""
import argparse,hashlib,json,os,re,subprocess,tempfile,urllib.request,zipfile
from pathlib import Path
parser=argparse.ArgumentParser();parser.add_argument('--apk',required=True);parser.add_argument('--sha',default=os.environ.get('GITHUB_SHA'));args=parser.parse_args()
repo='vatomalo/FlXtR-Steamlink';apk=Path(args.apk).resolve()
if not args.sha:raise SystemExit('A pushed source commit is required')
sdk=Path(os.environ.get('ANDROID_HOME','/var/tmp/flxtr-console-tools/sdk'))
badging=subprocess.check_output([str(sdk/'build-tools/35.0.0/aapt'),'dump','badging',str(apk)],text=True)
package=re.search(r"package: name='([^']+)' versionCode='(\d+)' versionName='([^']+)'",badging)
if not package or package[1]!='org.flxtr.android':raise SystemExit('Wrong Android package')
code=int(package[2]);name=package[3];minimum=int(re.search(r"sdkVersion:'(\d+)'",badging)[1])
subprocess.run(['java','-jar',str(sdk/'build-tools/35.0.0/lib/apksigner.jar'),'verify',str(apk)],check=True)
with zipfile.ZipFile(apk) as bundle:
    abis=sorted({path.split('/')[1] for path in bundle.namelist() if path.startswith('lib/') and path.endswith('.so')})
universal=abis==['arm64-v8a','armeabi-v7a']
if not abis or set(abis)-{'arm64-v8a','armeabi-v7a'}:raise SystemExit('Unsupported APK architectures')
arch='universal' if universal else 'arm64' if abis==['arm64-v8a'] else 'armv7'
tag=f'android-{code}-{args.sha[:12]}'+('' if universal else '-'+arch);url=f'https://github.com/{repo}/releases/download/{tag}/FlXtR-Android-preview.apk'
manifest={'package':package[1],'versionCode':code,'versionName':name,'minSdk':minimum,'size':apk.stat().st_size,'url':url,'sha256':hashlib.sha256(apk.read_bytes()).hexdigest(),'abis':abis}
title=os.environ.get('FLXTR_RELEASE_TITLE') or f'FlXtR Android {name} ({arch})'
notes_path=os.environ.get('FLXTR_RELEASE_NOTES_FILE')
notes=Path(notes_path).read_text() if notes_path else 'Console menus, gamepad navigation and verified startup updates. Existing libraries and settings are preserved.'
notes+=f'\n\nSource: `{args.sha}`\nArchitecture: {arch}\n'
with tempfile.TemporaryDirectory(prefix='flxtr-publish-') as directory:
    folder=Path(directory);metadata=folder/'android.json';metadata.write_text(json.dumps(manifest,indent=2)+'\n')
    release_notes=folder/'release-notes.md';release_notes.write_text(notes)
    if subprocess.run(['gh','release','view',tag,'-R',repo],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:
        subprocess.run(['gh','release','create',tag,'-R',repo,'--target',args.sha,'--prerelease','--title',title,'--notes-file',str(release_notes)],check=True)
    upload=folder/'FlXtR-Android-preview.apk';__import__('shutil').copyfile(apk,upload)
    subprocess.run(['gh','release','upload',tag,'-R',repo,str(upload),'--clobber'],check=True)
    if not universal:
        subprocess.run(['gh','release','upload',tag,'-R',repo,str(metadata),'--clobber'],check=True)
        print(f'Architecture-specific APK published ({arch}); universal auto-update channel kept: {url}')
        raise SystemExit(0)
    current=0
    try:
        address=f'https://github.com/{repo}/releases/download/android-latest/android.json?check='+str(__import__('time').time_ns())
        current=json.load(urllib.request.urlopen(address,timeout=30))['versionCode']
    except Exception:
        exists=subprocess.run(['gh','release','view','android-latest','-R',repo],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0
        if exists:raise RuntimeError('Cannot read current channel; refusing to replace it blindly')
        subprocess.run(['gh','release','create','android-latest','-R',repo,'--target',args.sha,'--prerelease','--title','FlXtR Android startup update channel','--notes','Verified Android startup-update manifest.'],check=True)
    if current<code:
        subprocess.run(['gh','release','upload','android-latest','-R',repo,str(metadata),'--clobber'],check=True)
        print(f'Android channel advanced from {current} to {code}: {url}')
    else:print(f'Immutable build uploaded; channel kept at {current} (no downgrade).')
