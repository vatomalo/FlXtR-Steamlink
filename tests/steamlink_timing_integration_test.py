"""Compare the original serialized player and audio-master worker with real
FFmpeg/SDL and a delayed SLVideo sink. This does not measure physical display."""
import http.server
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
import time

new,old,fixture=map(lambda s:Path(s).resolve(),sys.argv[1:4])
class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self,*a,**kw):super().__init__(*a,directory=str(fixture.parent),**kw)
    def log_message(self,*args):pass
    def do_GET(self):
        try:super().do_GET()
        except (BrokenPipeError,ConnectionResetError):pass
server=http.server.ThreadingHTTPServer(("127.0.0.1",0),Handler)
threading.Thread(target=server.serve_forever,daemon=True).start()
url=f"http://127.0.0.1:{server.server_port}/{fixture.name}"
results={}
for name,binary in (("serialized",old),("audio_master",new)):
    with tempfile.TemporaryDirectory() as directory:
        home=Path(directory)
        env=dict(os.environ,SDL_AUDIODRIVER="dummy",FLXTR_TEST_VIDEO_STALL="1",FLXTR_BOOKMARK_KEY="1234abcd")
        began=time.monotonic()
        result=subprocess.run([str(binary),url,"--buffer-mb","16"],cwd=home,env=env,
                              capture_output=True,text=True,timeout=25)
        assert result.returncode==0,result.stderr
        elapsed=time.monotonic()-began
        results[name]={"empty_audio_queues":result.stderr.count("TEST AUDIO QUEUE EMPTY"),
                       "elapsed":round(elapsed,3)}
        (Path("build")/f"timing-{name}.log").write_text(result.stderr)
        if name=="audio_master":
            assert "Playback pacing capped" not in result.stderr
            assert "Audio-master timing:" in result.stderr
            assert "Audio timestamp discontinuity:" not in result.stderr,result.stderr
            assert 7<elapsed<12,elapsed
            assert (home/"playback-started").exists()
            assert "resets=" in result.stderr
assert results["serialized"]["empty_audio_queues"]>=1,results
assert results["audio_master"]["empty_audio_queues"]<results["serialized"]["empty_audio_queues"],results
print("PASS: injected decoder stalls no longer block audio feeding:",json.dumps(results))
# Pause freezes the shared clock. Stop stays responsive while the worker paces.
with tempfile.TemporaryDirectory() as directory:
    home=Path(directory);reader,writer=os.pipe()
    env=dict(os.environ,SDL_AUDIODRIVER="dummy",FLXTR_BOOKMARK_KEY="1234abcd")
    with (home/"player.log").open("w") as log:
        child=subprocess.Popen([str(new),url,"--control-fd",str(reader)],cwd=home,
                               env=env,pass_fds=(reader,),stdout=log,stderr=log)
        os.close(reader)
        try:
            began=time.monotonic()
            while not (home/"playback-started").exists():
                assert child.poll() is None,(home/"player.log").read_text()
                assert time.monotonic()-began<10
                time.sleep(.02)
            time.sleep(.7);os.write(writer,b"P");time.sleep(.5)
            os.write(writer,b"S");assert child.wait(timeout=5)==40
            position=float((home/"playback-request").read_text().split()[1])
            assert .4<position<1.3,position # pause must not add half a second
        finally:
            os.close(writer)
            if child.poll() is None:child.kill();child.wait()
server.shutdown()
print("PASS: B-frame decode order, audio-master pause/stop and bounded worker shutdown")
