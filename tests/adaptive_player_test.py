"""Exercise measured adaptation through real FFmpeg/audio and a delayed SLVideo test sink."""
import http.server
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

binary,fixture=map(lambda p:Path(p).resolve(),sys.argv[1:3])
class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self,*args,**kwargs):
        super().__init__(*args,directory=str(fixture.parent),**kwargs)
    def do_GET(self):
        try:
            super().do_GET()
        except (BrokenPipeError,ConnectionResetError):
            pass
    def log_message(self,*args):
        pass
server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
with tempfile.TemporaryDirectory(prefix="FlXtR adaptive player ") as directory:
    home=Path(directory)
    env=dict(os.environ,SDL_AUDIODRIVER="dummy",FLXTR_TEST_VIDEO_STALL="1",FLXTR_BOOKMARK_KEY="1234abcd")
    result=subprocess.run([str(binary),f"http://127.0.0.1:{server.server_port}/{fixture.name}",
                           "--height","720","--adaptive","--buffer-mb","32"],
                          cwd=home,env=env,capture_output=True,text=True,timeout=40)
    assert result.returncode==46,result.stderr
    request=(home/"adaptive-request").read_text().split()
    assert request[0]=="480" and 10<float(request[1])<22,request
    assert request[3]=="720" and request[4] in ("2","3"),request
    assert "actual=720p requested=720p renditions=7" in result.stderr,result.stderr
    assert "Auto decision: 720p -> 480p" in result.stderr,result.stderr
    assert "TEST SLVIDEO CONTEXT OPEN" in result.stderr
    assert (home/"adaptive-ready").exists()
    history=(home/"adaptive-quality.log").read_text()
    assert "Auto decision: 720p -> 480p" in history and "decoder=3" in history,history
    print(result.stderr[-1400:])
    # Fatal rejection can bypass the startup grace period and request a lower tier.
    assert (home/"playback-started").read_text().split()[0]=="1234abcd"
    (home/"playback-started").unlink()
    (home/"adaptive-ready").unlink()
    rejected=subprocess.run([str(binary),f"http://127.0.0.1:{server.server_port}/{fixture.name}",
                              "--height","720","--adaptive","--buffer-mb","32"],
                             cwd=home,env=dict(env,FLXTR_TEST_VIDEO_REJECT="1"),
                             capture_output=True,text=True,timeout=15)
    assert rejected.returncode==46,rejected.stderr
    request=(home/"adaptive-request").read_text().split()
    assert request[0]=="480" and float(request[1])<2 and request[4]=="2",request
    assert not (home/"playback-started").exists(),"Rejected first frame counted as played"
    assert not (home/"adaptive-ready").exists(),"Rejected first frame must not mark a quality switch successful"
server.shutdown()
print("PASS: measured decoder/audio stalls produce a bounded 720-to-480 request with a resume timestamp through the real player")
