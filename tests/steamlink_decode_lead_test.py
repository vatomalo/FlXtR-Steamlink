"""Decode lead changes submission timing, not audio speed or the media tail."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

binary, fixture = (Path(arg).resolve() for arg in sys.argv[1:3])
results = []
for configured, expected in ((None, 100), ("1000", 1000), ("2000", 2000), ("9999", 100)):
    with tempfile.TemporaryDirectory() as directory:
        home = Path(directory)
        (home / "phone-files").mkdir()
        (home / "phone-files/video.mp4").write_bytes(fixture.read_bytes())
        if configured:
            (home / "video-decode-lead-ms").write_text(configured + "\n")
        began = time.monotonic()
        result = subprocess.run(
            [str(binary), "phone-files/video.mp4", "--subtitles", "off"],
            cwd=home, env=dict(os.environ, SDL_AUDIODRIVER="dummy"),
            capture_output=True, text=True, timeout=15,
        )
        elapsed = time.monotonic() - began
        assert result.returncode == 0, result.stderr
        assert f"Video decode lead: {expected}ms;" in result.stderr, result.stderr
        assert 7.8 < elapsed < 11, elapsed
        frames = int(re.search(r"Hardware frames submitted: (\d+)", result.stderr)[1])
        samples = int(re.search(r"Audio samples queued: (\d+)", result.stderr)[1])
        assert frames == 240, (configured, frames, result.stderr)
        assert "TEST AUDIO QUEUE EMPTY" not in result.stderr, result.stderr
        deltas = [int(n) for n in re.findall(r" delta=(-?\d+)ms", result.stderr)]
        if expected >= 1000:
            assert max(deltas) >= expected - 200, (configured, deltas)
        results.append((configured, samples, round(elapsed, 3)))
assert len({row[1] for row in results}) == 1, results
print("PASS: bounded decode lead advances video submission, preserves every frame/PCM sample and real-time audio:", results)
