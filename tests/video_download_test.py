"""Real HTTP + HLS remuxing, offline decode, failed/cancelled download cleanup."""
import functools, http.server, json, pathlib, subprocess, tempfile, threading, time, os

root=pathlib.Path(__file__).resolve().parents[1]
worker=root/'build/greenlink-download'
def command(*args, **kwargs):
    return subprocess.run(args,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,**kwargs)

with tempfile.TemporaryDirectory(prefix='flxtr-video-download-') as tmp:
    base=pathlib.Path(tmp);web=base/'web';app=base/'app';web.mkdir();app.mkdir()
    command('ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=160x90:rate=24',
            '-f','lavfi','-i','sine=frequency=440','-t','8','-c:v','libx264','-preset','ultrafast',
            '-c:a','aac','-movflags','+faststart',str(web/'video.mp4'))
    command('ffmpeg','-v','error','-y','-i',str(web/'video.mp4'),'-c','copy','-hls_time','2',
            '-hls_list_size','0',str(web/'vod.m3u8'))
    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self,*args): pass
        def do_GET(self):
            if self.path=='/slow.mp4':
                data=(web/'video.mp4').read_bytes()
                self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers()
                try:
                    for i in range(0,len(data),2048):
                        self.wfile.write(data[i:i+2048]);self.wfile.flush();time.sleep(.08)
                except (BrokenPipeError,ConnectionResetError): pass
            else: super().do_GET()
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(web)))
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    url=f'http://127.0.0.1:{server.server_port}'
    for key,source in [('0000000000000001','video.mp4'),('0000000000000002','vod.m3u8')]:
        command(str(worker),url+'/'+source,key,'720',cwd=app,timeout=25)
        saved=app/'phone-files'/f'offline-{key}.mkv'
        info=json.loads(command('ffprobe','-v','error','-show_streams','-show_format','-of','json',str(saved)).stdout)
        assert {s['codec_type'] for s in info['streams']}=={'video','audio'}
        assert 7.5<float(info['format']['duration'])<9
    # An existing video is kept byte-for-byte; a duplicate request must fail.
    saved=app/'phone-files/offline-0000000000000001.mkv';original=saved.read_bytes()
    assert subprocess.run([str(worker),url+'/video.mp4','0000000000000001','720'],cwd=app,stderr=subprocess.DEVNULL).returncode
    assert saved.read_bytes()==original
    missing=subprocess.run([str(worker),url+'/missing.mp4','0000000000000003','720'],cwd=app,stderr=subprocess.DEVNULL,timeout=10)
    assert missing.returncode and not (app/'phone-files/offline-0000000000000003.mkv').exists()
    child=subprocess.Popen([str(worker),url+'/slow.mp4','0000000000000004','720'],cwd=app,stderr=subprocess.DEVNULL)
    time.sleep(.3);child.terminate();assert child.wait(timeout=10)!=0
    assert not (app/'phone-files/offline-0000000000000004.mkv').exists()
    assert not list((app/'phone-files').glob('*.part'))
    # Simulate a nearly full filesystem without filling or modifying the disk.
    (base/'low-space.c').write_text('#include <sys/statvfs.h>\n#include <string.h>\nint statvfs(const char *p,struct statvfs *s){(void)p;memset(s,0,sizeof(*s));s->f_frsize=4096;s->f_bavail=1;return 0;}\n')
    command('cc','-shared','-fPIC',str(base/'low-space.c'),'-o',str(base/'low-space.so'))
    low=subprocess.run([str(worker),url+'/video.mp4','0000000000000006','720'],cwd=app,
                       env={**os.environ,'LD_PRELOAD':str(base/'low-space.so')},stderr=subprocess.DEVNULL)
    assert low.returncode==4 and 'STORAGE' in (app/'video-downloads/error').read_text()
    assert not (app/'phone-files/offline-0000000000000006.mkv').exists()
    # Offline decode after the origin server has been shut down.
    server.shutdown();server.server_close()
    for saved in (app/'phone-files').glob('*.mkv'):
        command('ffmpeg','-v','error','-i',str(saved),'-f','null','-',timeout=15)
    assert subprocess.run([str(worker),'file:///etc/passwd','0000000000000005','720'],cwd=app,stderr=subprocess.DEVNULL).returncode
    print('PASS: MP4/HLS audio+video saved; offline decode; duplicates preserved; HTTP failure and cancellation remove incomplete files; local URL rejected')
