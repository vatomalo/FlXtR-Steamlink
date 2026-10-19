#!/usr/bin/env python3
"""Cross-build the native Windows application from regular Debian/Ubuntu WSL."""
from pathlib import Path
import argparse
import os
import subprocess
import sys
ROOT = Path(__file__).resolve().parent.parent
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--no-fetch',action='store_true')
    args=parser.parse_args()
    os.chdir(ROOT)
    if not args.no_fetch:
        subprocess.run([sys.executable,'scripts/fetch-windows-sdk.py'],check=True)
    sdk=ROOT/'build/windows-sdk'
    compiler=next(sdk.glob('llvm-mingw-*/bin/x86_64-w64-mingw32-clang'))
    include=sdk/'ucrt64/include'
    lib=sdk/'ucrt64/lib'
    build=ROOT/'build/windows-app';build.mkdir(parents=True,exist_ok=True)
    version=subprocess.check_output(['git','rev-parse','--short=12','HEAD'],text=True).strip()
    flags=[str(compiler),'-std=c11','-O2','-g','-Wall','-Wextra',
           '-Wno-unused-command-line-argument','-Wno-unused-function','-Wno-unused-but-set-variable','-Wno-unused-but-set-global','-Wno-misleading-indentation','-Wno-unterminated-string-initialization',
           '-D_WIN32_WINNT=0x0601','-DWIN32_LEAN_AND_MEAN','-DFLXTR_DESKTOP',
           '-DSDL_MAIN_HANDLED',f'-DFLXTR_VERSION="{version}"',
           '-I'+str(include),'-I'+str(include/'SDL2'),'-L'+str(lib)]
    libs=['-lSDL2','-lSDL2_image','-lcurl','-ljson-c','-lcrypto','-lmpv','-larchive',
          '-lws2_32','-liphlpapi','-lbcrypt','-ladvapi32','-lshell32']
    for source,name,extra in [
        ('src/shell.c','FlXtR.exe',['-mwindows']),
        ('src/catalog.c','greenlink-catalog.exe',[]),
        ('src/player_linux.c','greenlink-player.exe',[]),
        ('src/audio_windows.c','greenlink-audio.exe',[])]:
        print('Building',name,flush=True)
        subprocess.run(flags+['-municode']+extra+[source,'-o',str(build/name)]+libs,check=True)
    # WABT is a host utility; reuse the project's verified source/build cache.
    cache=Path(os.environ.get('GREENLINK_BUILD_CACHE','/tmp/greenlink-build'))
    subprocess.run(['bash','scripts/build-resolver.sh','--prepare-only'],check=True)
    runtime=cache/'wabt-1.0.36/wasm2c'
    generated=build/'resolver';generated.mkdir(exist_ok=True)
    subprocess.run([str(cache/'wabt-host/wasm2c'),str(cache/'img_data-6942482f.wasm'),
                    '-o',str(generated/'img_data.c'),'--module-name','img_data'],check=True)
    subprocess.run([sys.executable,'tools/resolver_imports.py',str(generated/'img_data.h'),
                    str(generated/'resolver_imports.inc')],check=True)
    objects=[]
    common=flags+['-DNDEBUG','-DWASM_RT_USE_MMAP=0','-DWASM_RT_MEMCHECK_BOUNDS_CHECK=1',
                  '-I'+str(generated),'-Isrc','-I'+str(runtime)]
    for source in [generated/'img_data.c',Path('src/resolver_bridge.c'),
                   runtime/'wasm-rt-impl.c',runtime/'wasm-rt-mem-impl.c',Path('src/resolver.c')]:
        obj=generated/(source.stem+'.o')
        subprocess.run(common+(['-Wno-unused-parameter'] if source.parent==generated else [])+['-c',str(source),'-o',str(obj)],check=True)
        objects.append(str(obj))
    subprocess.run(flags+['-municode']+objects+['-o',str(build/'greenlink-resolver.exe')]+libs+['-lm','-lpthread'],check=True)
    subprocess.run(flags+['-municode','tests/desktop_fullscreen_test.c','-o',str(build/'windows-fullscreen-test.exe')]+libs,check=True)
    subprocess.run(flags+['-municode','tests/windows_app_test.c','-o',str(build/'windows-app-test.exe')]+libs,check=True)
    subprocess.run([sys.executable,'scripts/package-windows.py'],check=True)
if __name__=='__main__':
    main()
