"""Exercise firmware messages and actual launcher game lifetime with a fake bus."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

project = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="flxtr-power-") as directory:
    root = Path(directory)
    def executable(name, body):
        path = root / name
        path.write_text(body)
        path.chmod(0o755)
    executable("dbus-send", '#!/bin/sh\nprintf "%s\\n" "$*" >>"$POWER_TEST_LOG"\n')
    shutil.copy(project / "packaging/power-activity.sh", root)
    env = dict(os.environ, PATH=str(root) + ":" + os.environ["PATH"], POWER_TEST_LOG=str(root / "bus.log"), FLXTR_POWER_INTERVAL="0.05")
    def messages():
        return (root / "bus.log").read_text().splitlines()
    for mode in ("--once", "--idle"):
        subprocess.run(["sh", "power-activity.sh", mode], cwd=root, env=env, check=True)
    assert messages()[0].endswith("uint32:1") and messages()[1].endswith("uint32:0")
    assert all("--system --type=method_call --dest=system.powermanager /powermanager system.powermanager.SetActivity" in row for row in messages())
    (root / "bus.log").unlink()
    child = subprocess.Popen(["sleep", "20"])
    guard = subprocess.Popen(["sh", "power-activity.sh", str(child.pid)], cwd=root, env=env)
    try:
        time.sleep(.25)
        assert len(messages()) >= 2 and all(row.endswith("uint32:1") for row in messages())
        guard.terminate(); guard.wait(timeout=2)
        assert messages()[-1].endswith("uint32:0")
    finally:
        child.terminate(); child.wait()
        if guard.poll() is None: guard.kill(); guard.wait()
    (root / "bus.log").unlink()
    child = subprocess.Popen(["sleep", ".15"])
    guard = subprocess.Popen(["sh", "power-activity.sh", str(child.pid)], cwd=root, env=env)
    child.wait(); guard.wait(timeout=2)
    assert messages()[0].endswith("uint32:1") and messages()[-1].endswith("uint32:0")
    (root / "bus.log").unlink()
    shutil.copy(project / "packaging/greenlink.sh", root)
    (root / ".no-auto-update").touch()
    executable("greenlink", '#!/bin/sh\nif [ ! -f launched ]; then touch launched; exit 43; fi\nexit 0\n')
    executable("greenlink-catalog", '#!/bin/sh\nsleep .3\n')
    subprocess.run(["sh", "greenlink.sh"], cwd=root, env=env, timeout=8, check=True, capture_output=True)
    assert sum(row.endswith("uint32:1") for row in messages()) >= 2
    assert messages()[-1].endswith("uint32:0")
print("PASS: firmware call shape, repeated ROM heartbeat, natural exit, termination cleanup and launcher integration")
