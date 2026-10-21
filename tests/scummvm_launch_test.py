"""Run only in a disposable Linux container; uses /home/apps as on Steam Link."""
from pathlib import Path
import os,subprocess,tempfile
root=Path("/src")
app=Path("/home/apps/scummvm")
assert not app.exists(), "Use a fresh disposable container"
app.mkdir(parents=True)
exe=app/"scummvm.exec"
exe.write_text("#!/bin/sh\nprintf '%s\n' \"$PWD\" \"$HOME\" \"$XDG_CONFIG_HOME\" > /src/build/scummvm-launch-env.txt\n")
exe.chmod(0o755)
with tempfile.TemporaryDirectory() as tmp:
    data=Path(tmp)
    (data/"game-request").write_text("0\n")
    env=dict(os.environ,HOME=str(data/"wrong-home"),XDG_CONFIG_HOME=str(data/"wrong-xdg"))
    subprocess.run([str(root/"build/scummvm-launch-test-catalog"),"--run-game"],cwd=data,env=env,check=True)
    actual=(root/"build/scummvm-launch-env.txt").read_text().splitlines()
    assert actual==[str(app),str(app/".home"),str(app/".home/.config")],actual
    old=Path("/home/apps/greenlink/.home/.config/scummvm/scummvm.ini")
    old.parent.mkdir(parents=True);old.write_text("gfx_mode=broken")
    current=app/".home/.config/scummvm/scummvm.ini"
    current.parent.mkdir();current.write_text("gfx_mode=broken")
    save=app/"game.sav";save.write_bytes(b"save")
    subprocess.run(["sh",str(root/"packaging/reset-scummvm-config.sh")],env=env,check=True)
    assert not old.exists() and not current.exists() and save.read_bytes()==b"save"
    assert len(list(Path("/home/apps").glob("scummvm-config-backup-*/*")))==2
print("PASS: standalone ScummVM owns cwd/HOME/XDG; reset includes legacy FlXtR configs and preserves saves")
