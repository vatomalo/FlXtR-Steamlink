"""Check actual packaged X11 video output, rather than a headless decode."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

package = Path(sys.argv[1]).resolve()
binary = package / "current/greenlink-player"
with tempfile.TemporaryDirectory(prefix="FlXtR Deck video ") as directory:
    home = Path(directory)
    clip = home / "fixture.mp4"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                    "testsrc2=size=640x360:rate=24", "-t", "12", "-an",
                    "-c:v", "libx264", str(clip)], check=True)
    env = dict(os.environ, FLXTR_STEAMDECK="1", SDL_VIDEODRIVER="x11",
               LIBGL_ALWAYS_SOFTWARE="1")
    env.pop("WAYLAND_DISPLAY", None)
    with (home / "player.log").open("w+") as log:
        child = subprocess.Popen([str(binary), str(clip), "--duration", "10",
                                  "--buffer-seconds", "1"], cwd=home, env=env,
                                 stdout=log, stderr=subprocess.STDOUT)
        try:
            rendered = False
            for _ in range(40):
                time.sleep(.2)
                if child.poll() is not None:
                    break
                shot = home / "screen.png"
                subprocess.run(["import", "-window", "root", str(shot)],
                               check=True, timeout=10)
                mean = float(subprocess.check_output(
                    ["convert", str(shot), "-format", "%[fx:mean]", "info:"],
                    text=True))
                if mean > .04:
                    rendered = True
                    break
            code = child.wait(timeout=20)
            log.seek(0)
            output = log.read()
            assert code == 0, output
            assert "Steam Deck video: X11/EGL" in output, output
            assert rendered, "No visible video pixels on X11 display\n" + output
            assert "VO: [gpu]" in output or "VO: [x11]" in output, output
        finally:
            if child.poll() is None:
                child.kill()
                child.wait()
print("PASS: packaged Deck player presents visible video in a fullscreen X11 window")
