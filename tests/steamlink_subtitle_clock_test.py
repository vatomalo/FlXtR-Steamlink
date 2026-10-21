"""Actual text decoding/overlays and audio clock against controlled captions."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
binary=Path(sys.argv[1]).resolve()
fixtures=[Path(s).resolve() for s in sys.argv[2:]]
for fixture in fixtures:
    with tempfile.TemporaryDirectory() as root:
        home=Path(root);(home/"phone-files").mkdir()
        media=home/"phone-files"/fixture.name
        media.write_bytes(fixture.read_bytes())
        env=dict(os.environ,SDL_AUDIODRIVER="dummy")
        if "external" in fixture.name:
            (home/"phone-files/caption.srt").write_text(
                "1\n00:00:01,000 --> 00:00:02,000\nExternal one\n\n"
                "2\n00:00:04,000 --> 00:00:05,000\nExternal two\n")
            env["FLXTR_SUBTITLE_ENGLISH"]="phone-files/caption.srt"
        result=subprocess.run([str(binary),"phone-files/"+fixture.name],cwd=home,env=env,
                              capture_output=True,text=True,timeout=20)
        assert result.returncode==0,result.stderr
        (Path("build")/("subtitle-clock-"+fixture.name+".log")).write_text(result.stderr)
        cues=[tuple(map(float,m)) for m in re.findall(
            r"Subtitle presented: start=([\d.-]+) end=([\d.-]+) clock=([\d.-]+)",result.stderr)]
        assert len(cues)>=2,result.stderr
        # Both cues must appear within 100 ms of the correct audio timestamp.
        assert all(0<=clock-start<.10 for start,end,clock in cues),cues
        assert "Audio timestamp discontinuity:" not in result.stderr,result.stderr
        print("PASS:",fixture.name,"captions follow the audio clock:",cues)
