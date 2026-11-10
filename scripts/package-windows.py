#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,zipfile
ROOT=Path(__file__).resolve().parent.parent
os.chdir(ROOT)
SDK=ROOT/'build/windows-sdk/ucrt64'
BUILD=ROOT/'build/windows-app'
OUT=ROOT/'dist/FlXtR-Windows-x86_64'
OUT.mkdir(parents=True,exist_ok=True)
# Only clean the verified, generated package directory.
assert OUT.resolve().parent == (ROOT/'dist').resolve()
for child in OUT.iterdir():
    if child.is_dir():shutil.rmtree(child)
    else:child.unlink()
llvm=next((ROOT/'build/windows-sdk').glob('llvm-mingw-*/bin/llvm-objdump'))
strip=next((ROOT/'build/windows-sdk').glob('llvm-mingw-*/bin/llvm-strip'))
dlls={file.name.lower():file for file in (SDK/'bin').glob('*.dll')}
system=set('advapi32 avicap32 avrt bcrypt bcryptprimitives cfgmgr32 comctl32 comdlg32 crypt32 dnsapi dwmapi dwrite gdi32 gdiplus imm32 iphlpapi kernel32 msimg32 ncrypt netapi32 ntdll ole32 oleaut32 opengl32 propsys rpcrt4 secur32 setupapi shcore shell32 shlwapi user32 userenv usp10 uxtheme version winmm wldap32 ws2_32 wsock32 xmllite advapi32 avrt bcrypt cfgmgr32 combase comctl32 comdlg32 crypt32 d3d11 d3d12 d3d9 dcomp dinput8 dnsapi dsound dwmapi dxgi dxva2 gdi32 glu32 hid imm32 iphlpapi kernel32 mf mfplat mfreadwrite mfuuid mpr msimg32 msvcrt ncrypt netapi32 normaliz ntdll ole32 oleaut32 opengl32 powrprof propsys psapi rpcrt4 secur32 setupapi shell32 shlwapi synchronization user32 userenv usp10 uxtheme version vfw32 winhttp wininet winmm winscard winspool wintrust ws2_32 wtsapi32 ucrtbase wldap32 shcore'.split())
copied=set()
def imports(file):
    text=subprocess.check_output([str(llvm),'-p',str(file)],text=True,errors='replace')
    return re.findall(r'DLL Name:\s*(\S+)',text)
def closure(files,destination):
    todo=list(files);seen=set()
    while todo:
        file=todo.pop();key=str(file)
        if key in seen:continue
        seen.add(key)
        for name in imports(file):
            source=dlls.get(name.lower())
            if source:
                dest=destination/source.name
                if not dest.exists():shutil.copy2(source,dest);todo.append(dest)
                copied.add(source.name)
            elif not name.lower().startswith(('api-ms-win-','ext-ms-win-')) and name.lower().removesuffix('.dll') not in system:
                raise RuntimeError(f'Unresolved non-system import {name} in {file}')
binaries=[]
for name in ['FlXtR.exe','greenlink-catalog.exe','greenlink-player.exe','greenlink-resolver.exe','greenlink-audio.exe']:
    file=BUILD/name
    dest=OUT/file.name;shutil.copy2(file,dest)
    subprocess.run([str(strip),str(dest)],check=True);binaries.append(dest)
shutil.copy2(SDK/'bin/curl.exe',OUT/'curl.exe');binaries.append(OUT/'curl.exe')
closure(binaries,OUT)
runtime=OUT/'music-runtime';(runtime/'bin').mkdir(parents=True)
for name in ['python.exe','qjs.exe']:shutil.copy2(SDK/'bin'/name,runtime/'bin'/name)
shutil.copytree(SDK/'lib/python3.14',runtime/'lib/python3.14',ignore=shutil.ignore_patterns(
    'imath*','vapoursynth*','VapourSynth*','libxml*','drv_libxml2.py','caca','__pycache__','*.pyc','test','tests','idlelib','tkinter','turtledemo','ensurepip','config-3.14','Tools'),symlinks=False)
closure(list((runtime/'bin').glob('*.exe'))+list(runtime.rglob('*.pyd')),runtime/'bin')
for name in ['assets','tools']:
    shutil.copytree(ROOT/name,OUT/name,ignore=shutil.ignore_patterns('__pycache__','*.pyc'))
for name in ['catalog.tsv','linux-controls.lua','LICENSE']:shutil.copy2((ROOT/("packaging/"+name) if name=="linux-controls.lua" else ROOT/name),OUT/name)
shutil.copy2(ROOT/'docs/CORES.md',OUT/'CORES.md')
shutil.copy2(ROOT/'docs/DESKTOP-MUSIC.md',OUT/'MUSIC.md')
shutil.copy2(ROOT/'docs/DESKTOP-CONTROLS.md',OUT/'CONTROLS.md')
(OUT/'certs').mkdir()
shutil.copy2('/etc/ssl/certs/ca-certificates.crt',OUT/'certs/cacert.pem')
shutil.copytree(SDK/'share/licenses',OUT/'third-party-licenses',dirs_exist_ok=True)
shutil.copy2(ROOT/'packaging/LIBRETRO-API-LICENSE.txt',OUT/'third-party-licenses/LIBRETRO-API.txt')
lock=json.loads((ROOT/'scripts/windows-sdk.lock.json').read_text())
(OUT/'THIRD-PARTY-SOURCES.json').write_text(json.dumps(lock,indent=2)+'\n')
cache=Path(os.environ.get('GREENLINK_BUILD_CACHE','/tmp/greenlink-build'))
shutil.copy2(cache/'wabt-1.0.36/LICENSE',OUT/'third-party-licenses/WABT.txt')
(OUT/'README.txt').write_text('''FlXtR for Windows x86-64 - experimental native application

Extract the entire ZIP into a folder and run FlXtR.exe.
Keep the DLLs, assets, certs, tools and music-runtime next to it.
No WSL, Docker, Python installation or C# runtime is required to run it.

Navigation: arrows or a controller; Enter/A select; Escape/B back.
The native Windows app includes the UI, catalog, resolver, libmpv video
player, phone setup server, and Python/QuickJS music worker runtime.
Settings/cache/keys live in %LOCALAPPDATA%/FlXtR.
Games > DOWNLOAD LIBRETRO CORES installs missing platform cores.
Install RetroArch separately; see CORES.md.
Updates: download a newer Windows ZIP from GitHub and extract it.

This is an experimental port. Online providers can change or fail.
Steam Link's ConnMan Wi-Fi provisioning and controller sleep scripts
do not apply to Windows. RetroArch/ScummVM and ROMs/cores are separate
installations; none are included in this package.

Source and support: https://github.com/vatomalo/FlXtR-Steamlink
See LICENSE, third-party-licenses, THIRD-PARTY-SOURCES.json.
''')
manifest={file.relative_to(OUT).as_posix():hashlib.sha256(file.read_bytes()).hexdigest()
          for file in sorted(OUT.rglob('*')) if file.is_file()}
(OUT/'SHA256SUMS.json').write_text(json.dumps(manifest,indent=2)+'\n')
archive=ROOT/'dist/FlXtR-Windows-x86_64.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as bundle:
    for file in sorted(OUT.rglob('*')):
        if file.is_file():bundle.write(file,file.relative_to(OUT))
digest=hashlib.sha256(archive.read_bytes()).hexdigest()
Path(str(archive)+'.sha256').write_text(digest+'  '+archive.name+'\n')
print(f'Packaged {archive} ({archive.stat().st_size//1048576} MiB); {len(copied)} runtime DLLs')
