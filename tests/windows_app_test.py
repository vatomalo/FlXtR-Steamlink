"""Run the complete extracted Windows package without network-dependent tests."""
from pathlib import Path
import argparse
import http.client
import time
from urllib.parse import urlsplit, urlencode
import json
import os
import subprocess
import tempfile
import zipfile
parser=argparse.ArgumentParser()
parser.add_argument('--package',required=True,type=Path)
parser.add_argument('--fixture',required=True,type=Path)
parser.add_argument('--helper',required=True,type=Path)
args=parser.parse_args()
package=args.package.resolve()
fixture=args.fixture.resolve()
helper=args.helper.resolve()
assert os.name=='nt','Run on native Windows'
with tempfile.TemporaryDirectory(prefix='FlXtR Windows data ø ') as temp:
    home=Path(temp)
    env=dict(os.environ,FLXTR_DATA_DIR=str(home),SDL_VIDEODRIVER='dummy',
             FLXTR_NO_ART='1',PYTHONDONTWRITEBYTECODE='1')
    def run(binary,*argv,code=0,timeout=30):
        result=subprocess.run([str(binary),*map(str,argv)],cwd=home,env=env,
                              capture_output=True,timeout=timeout)
        assert result.returncode==code,(binary,result.returncode,result.stdout,result.stderr)
        return result.stdout.decode('utf-8',errors='replace')
    run(package/'FlXtR.exe','--frames','2','--screenshot','ui.bmp')
    assert (home/'ui.bmp').stat().st_size>10000
    run(helper.with_name('windows-supervisor-test.exe'))
    print('PASS: full native SDL UI, packaged resources and Unicode data directory')
    run(package/'greenlink-player.exe',fixture,'--headless','--duration','1')
    run(helper.with_name('windows-fullscreen-test.exe'),fixture,'--headless','--duration','1')
    # The integration helper uses the actual UI launch_player(), not a mock.
    # It must be beside the package executables for child executable resolution.
    run(helper,fixture)
    print('PASS: native libmpv decoding, UI launcher and control/preload IPC')
    server=subprocess.Popen([str(helper),'--phone-server'],cwd=home,env=env,stdout=subprocess.PIPE,text=True)
    try:
        url=urlsplit(server.stdout.readline().strip())
        def phone(method,route,body=None,form=False):
            connection=http.client.HTTPConnection(url.hostname,url.port,timeout=10)
            connection.request(method,url.path+route,body,{'Content-Type':'application/x-www-form-urlencoded' if form else 'application/octet-stream'})
            response=connection.getresponse()
            result=response.status,response.read().decode()
            connection.close()
            return result
        assert phone('POST','upload/fixture.mp4',fixture.read_bytes())[0]==200
        (home/'games.json').write_text('[]')
        assert phone('POST','upload/phone-test.nes',b'NES fixture')[0]==200
        assert not (home/'games.json').exists()
        cores=home/'cores';cores.mkdir(exist_ok=True)
        (cores/'fceumm_libretro.dll').write_bytes(b'fixture core, import-only test')
        run(package/'greenlink-catalog.exe','games','1','','0','0','0')
        games=json.loads((home/'games.json').read_text())
        assert any(g['rom'].endswith('/roms/phone/phone-test.nes') for g in games)
        assert phone('POST','',urlencode({'play':'fixture.mp4'}),True)[0]==200
        assert phone('POST','',urlencode({'play':'fixture.mp4'}),True)[0]==409
        assert phone('POST','',urlencode({'control':'s'}),True)[0]==200
        deadline=time.monotonic()+15
        while not (home/'phone-player-exit').exists() and time.monotonic()<deadline:time.sleep(.05)
        assert (home/'phone-player-exit').read_text()=='40'
        assert phone('POST','',urlencode({'control':'p'}),True)[0]==409
        print('PASS: HTTP upload -> actual phone video launch/control; ROM upload -> catalog import')
    finally:
        server.terminate();server.wait(timeout=5)
    text=run(package/'greenlink-catalog.exe','games-platforms' ,'1','','0','0','0')
    assert '# pages=2 total=8' in text and 'NES' in text
    text=run(package/'greenlink-catalog.exe','core-list','1','','0','0','0','')
    assert 'core-download' in text and 'FCEUmm' in text
    text=run(package/'greenlink-catalog.exe','games-archive','1','','0','0','0')
    assert '# pages=' in text
    # Validate Unicode JSON paths and native bounded ZIP extraction.
    archive=home/'fixture ø.zip'
    with zipfile.ZipFile(archive,'w') as bundle:
        bundle.writestr('../escape.nes',b'bad')
        bundle.writestr('folder/test.nes',b'NES test fixture')
    (home/'games.json').write_text(json.dumps([{'rom':archive.as_posix()}]),encoding='utf-8')
    text=run(package/'greenlink-catalog.exe','games-zip-list','1','','1','0','0')
    assert 'test.nes' in text and 'escape.nes' not in text
    retro=home/'retroarch';retro.mkdir()
    env['FLXTR_RETROARCH_HOME']=retro.as_posix()
    run(package/'greenlink-catalog.exe','games-zip-extract','1','','1','0','1')
    assert (retro/'roms/NES/test.nes').read_bytes()==b'NES test fixture'
    assert not (home/'escape.nes').exists()
    print('PASS: catalog resources, Unicode JSON/ZIP paths and traversal rejection')
    text=run(package/'music-runtime/bin/python.exe','-I','-c',
             'import ssl,sqlite3,yt_dlp,yt_dlp_ejs;print("runtime ready")')
    assert 'runtime ready' in text
    text=run(package/'music-runtime/bin/qjs.exe','-e','console.log("QuickJS ready")')
    assert 'QuickJS ready' in text
    # Exercise actual script options, including the relocated QuickJS path.
    script=str(package/'tools/music.py')
    text=run(package/'music-runtime/bin/python.exe','-I','-c',
             'import runpy,os; d=runpy.run_path('+repr(script)+'); p=d["ydl_options"]()["js_runtimes"]["quickjs"]["path"]; assert os.path.isfile(p); print("music options ready")')
    assert 'music options ready' in text
    print('PASS: packaged Python, TLS, yt-dlp/EJS, QuickJS and music worker options')
print('PASS: Windows application package integration')
