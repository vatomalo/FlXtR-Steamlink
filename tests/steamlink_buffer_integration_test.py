"""Real FFmpeg/player/control path with a test SLVideo sink; no device is simulated."""
import http.server
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

binary, fixture = map(lambda p: Path(p).resolve(), sys.argv[1:3])
class Handler(http.server.SimpleHTTPRequestHandler):
    requests = 0
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(fixture.parent), **kwargs)
    def do_GET(self):
        type(self).requests += 1
        try:
            super().do_GET()
        except (BrokenPipeError,ConnectionResetError):
            pass
    def log_message(self, *args):
        pass
server = http.server.ThreadingHTTPServer(("127.0.0.1",0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
url=f"http://127.0.0.1:{server.server_port}/{fixture.name}"
with tempfile.TemporaryDirectory(prefix="FlXtR RAM player ") as directory:
    home=Path(directory)
    env=dict(os.environ,SDL_AUDIODRIVER="dummy",FLXTR_BOOKMARK_KEY="1234abcd")
    # A source with only 720p must still be usable when 480p is preferred.
    probe=subprocess.run([str(binary),"--probe",url,"--height","480"],cwd=home,env=env,
                         capture_output=True,text=True,timeout=20)
    assert probe.returncode==0,probe.stderr
    assert "using available 720p" in probe.stderr,probe.stderr
    gate,go=os.pipe()
    controls,control=os.pipe()
    with (home/"preload.log").open("w+") as log:
        child=subprocess.Popen([str(binary),url,"--preload-fd",str(gate),
                                "--control-fd",str(controls),"--buffer-mb","16"],
                               cwd=home,env=env,pass_fds=(gate,controls),
                               stdout=log,stderr=subprocess.STDOUT)
        os.close(gate);os.close(controls)
        try:
            began=time.monotonic()
            while "TV preloading compressed packets" not in (home/"preload.log").read_text():
                assert child.poll() is None,(home/"preload.log").read_text()
                assert time.monotonic()-began<10
                time.sleep(.05)
            time.sleep(.2)
            assert "TEST SLVIDEO CONTEXT OPEN" not in (home/"preload.log").read_text()
            assert not (home/"playback-started").exists(),"Preloader counted as played"
            count=Handler.requests
            os.write(go,b"G")
            began=time.monotonic()
            while "TEST SLVIDEO CONTEXT OPEN" not in (home/"preload.log").read_text():
                assert time.monotonic()-began<10
                time.sleep(.05)
            assert Handler.requests==count,"Releasing preload gate reopened the stream"
            while not (home/"playback-started").exists():
                assert time.monotonic()-began<10
                time.sleep(.05)
            assert (home/"playback-started").read_text().split()[0]=="1234abcd"
            os.write(control,b"S");assert child.wait(timeout=10)==40
        finally:
            os.close(go);os.close(control)
            if child.poll() is None:
                child.kill();child.wait()
    reader,writer=os.pipe()
    with (home/"player.log").open("w+") as log:
        child=subprocess.Popen([str(binary),url,"--height","480","--control-fd",str(reader),
                                "--buffer-seconds","5","--buffer-mb","16"],cwd=home,env=env,
                               pass_fds=(reader,),stdout=log,stderr=subprocess.STDOUT)
        os.close(reader)
        try:
            began=time.monotonic()
            while True:
                time.sleep(.1)
                log.flush()
                text=(home/"player.log").read_text()
                if "RAM buffer:" in text:
                    break
                assert child.poll() is None,text
                assert time.monotonic()-began<15,text
            time.sleep(.8)
            count=Handler.requests
            os.write(writer,b"F")
            began=time.monotonic()
            while "RAM seek:" not in (home/"player.log").read_text():
                assert time.monotonic()-began<5,(home/"player.log").read_text()
                time.sleep(.05)
            assert Handler.requests==count,"Cached seek opened a new HTTP connection"
            os.write(writer,b"S")
            assert child.wait(timeout=10)==40
            request=(home/"playback-request").read_text().split()
            assert request[0]=="7"
            output=(home/"player.log").read_text()
            assert "16 MiB / 15 seconds prefill" in output,output
            assert not (home/"playback-cache").exists(),"RAM mode wrote a disk cache"
        finally:
            os.close(writer)
            if child.poll() is None:
                child.kill()
                child.wait()
server.shutdown()
print("PASS: real player accepts a 720p-only source at 480p preference, preloads packets with hardware closed, enforces 15s RAM prefill, seeks without another HTTP request and stops")
