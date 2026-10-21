"""Verify initial FFmpeg seeks use the same normalized clock as cached seeks."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
binary,fixture=map(lambda s:Path(s).resolve(),sys.argv[1:3])
with tempfile.TemporaryDirectory() as directory:
    home=Path(directory);(home/"phone-files").mkdir()
    (home/"phone-files/offset.ts").write_bytes(fixture.read_bytes())
    reader,writer=os.pipe()
    with (home/"player.log").open("w") as log:
        child=subprocess.Popen([str(binary),"phone-files/offset.ts","--start","10","--control-fd",str(reader)],
            cwd=home,env=dict(os.environ,SDL_AUDIODRIVER="dummy",FLXTR_BOOKMARK_KEY="1234abcd"),
            pass_fds=(reader,),stdout=log,stderr=log)
        os.close(reader)
        try:
            began=time.monotonic()
            while not (home/"playback-started").exists():
                assert child.poll() is None,(home/"player.log").read_text()
                assert time.monotonic()-began<10,(home/"player.log").read_text()
                time.sleep(.02)
            position=float((home/"playback-started").read_text().split()[1])
            assert 8<=position<12,position
            os.write(writer,b"S");assert child.wait(timeout=5)==40
            assert "Audio timestamp discontinuity:" not in (home/"player.log").read_text()
        finally:
            os.close(writer)
            if child.poll() is None:child.kill();child.wait()
print("PASS: initial seek timestamp includes the stream offset; audio/video report normalized media time")
