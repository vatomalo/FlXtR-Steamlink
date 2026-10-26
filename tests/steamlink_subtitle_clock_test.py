"""Actual text decoding/overlays and audio clock against controlled captions."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
binary=Path(sys.argv[1]).resolve()
lead=os.environ.get("FLXTR_TEST_DECODE_LEAD_MS","100")
fine=os.environ.get("FLXTR_TEST_SUBTITLE_DELAY_MS","0")
fixtures=[Path(s).resolve() for s in sys.argv[2:]]
for fixture in fixtures:
    with tempfile.TemporaryDirectory() as root:
        home=Path(root);(home/"phone-files").mkdir()
        (home/"video-decode-lead-ms").write_text(lead+"\n")
        (home/"subtitle-delay-ms").write_text(fine+"\n")
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
        (Path("build")/("subtitle-clock-"+lead+"-"+fine+"-"+fixture.name+".log")).write_text(result.stderr)
        cues=[tuple(map(float,m)) for m in re.findall(
            r"Subtitle presented: start=([\d.-]+) end=([\d.-]+) clock=([\d.-]+)",result.stderr)]
        assert len(cues)>=2,result.stderr
        presentation=[float(s) for s in re.findall(r"Subtitle presentation clock: ([\d.-]+)",result.stderr)]
        assert len(presentation)==len(cues),result.stderr
        assert all(abs(raw-clock-int(fine)/1000)<.01 for raw,(_,_,clock) in zip(presentation,cues)),(presentation,cues)
        # Both cues must appear within 100 ms of the correct audio timestamp.
        assert all(0<=clock-start<.10 for start,end,clock in cues),cues
        endings=[tuple(map(float,m)) for m in re.findall(
            r"Subtitle cue ended: start=([\d.-]+) end=([\d.-]+) clock=([\d.-]+)",result.stderr)]
        assert len(endings)>=2,result.stderr
        assert all(0<=clock-end<.10 for start,end,clock in endings),endings
        assert "Audio timestamp discontinuity:" not in result.stderr,result.stderr
        print("PASS:",fixture.name,"captions follow the audio clock with decode lead",lead,"ms and subtitle delay",fine,"ms:",cues)
