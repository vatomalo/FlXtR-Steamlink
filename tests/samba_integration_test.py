"""Run the actual ARM Samba runtime through QEMU in a disposable container."""
from pathlib import Path
import os,secrets,shutil,subprocess,tempfile,time
root=Path("/work")
# docker build -f tests/samba.Dockerfile -t flxtr-samba-test:local .
# docker run --rm -v "$PWD:/work" flxtr-samba-test:local python3 tests/samba_integration_test.py
with tempfile.TemporaryDirectory(prefix="FlXtR Samba ") as directory:
    app=Path(directory)/"app";app.mkdir()
    for name in ("samba-run.sh","samba-server.sh"):shutil.copyfile(root/"packaging"/name,app/name)
    shutil.copytree(root/os.environ.get("FLXTR_SAMBA_TEST_RUNTIME","build/samba-runtime"),app/"samba-runtime",symlinks=True)
    env=dict(os.environ,FLXTR_SAMBA_EMULATOR="/usr/bin/qemu-arm",FLXTR_SAMBA_PORT="1445",FLXTR_SAMBA_INTERFACES="lo")
    def server(action,**kw):
        return subprocess.run(["sh",str(app/"samba-server.sh"),action],env=env,cwd=app,
                              capture_output=True,text=True,timeout=40,**kw)
    password=secrets.token_hex(16)

    configured=server("configure",input=password+"\n"+password+"\n")
    if configured.returncode:
        logs="\n".join(p.read_text(errors="replace") for p in (app/"samba/logs").glob("*") if p.is_file())
        print("\n".join(line for line in (configured.stderr+logs).splitlines() if any(v in line.lower() for v in ("module","tdbsam","error","failed","dlopen"))))
    assert configured.returncode==0,(configured.stdout,configured.stderr[-4000:])
    assert not (app/"samba/.enabled").exists()
    started=server("enable")
    assert started.returncode==0,(started.stdout,started.stderr,[(str(p),p.read_text(errors="replace")[-4000:]) for p in (app/"samba/logs").glob("*") if p.is_file()])
    assert (app/"samba/.enabled").exists()
    assert server("start").returncode==0
    try:
        time.sleep(2)
        current=server("status")
        assert current.stdout.strip()=="RUNNING"
        auth=app/"auth";auth.write_text("username=root\npassword="+password+"\n");auth.chmod(0o600)
        def client(share,command,guest=False):
            args=["smbclient","//127.0.0.1/"+share,"-p","1445","-m","SMB3","-c",command]
            args+=["-N"] if guest else ["-A",str(auth)]
            return subprocess.run(args,cwd=app,capture_output=True,text=True,timeout=30)
        denied=client("ROMs","ls",True);assert denied.returncode!=0
        auth.write_text("username=root\npassword=incorrect-password\n")
        assert client("ROMs","ls").returncode!=0
        auth.write_text("username=root\npassword="+password+"\n")
        (app/"upload.rom").write_bytes(b"FlXtR SMB transfer fixture")
        put=client("ROMs","put upload.rom test.nes");assert put.returncode==0,(put.stdout,put.stderr)
        assert (app/"roms/test.nes").read_bytes()==b"FlXtR SMB transfer fixture"
        movie=client("Movies","put upload.rom movie.mp4");assert movie.returncode==0,(movie.stdout,movie.stderr)
        assert (app/"videos/movie.mp4").read_bytes()==b"FlXtR SMB transfer fixture"
        get=client("ROMs","get test.nes downloaded.rom");assert get.returncode==0,(get.stdout,get.stderr)
        assert (app/"downloaded.rom").read_bytes()==(app/"upload.rom").read_bytes()
        # An exposed folder may not follow links into the private app state.
        (app/"roms/private-link").symlink_to(app/"auth")
        escaped=client("ROMs","get private-link escaped");assert escaped.returncode!=0
        assert not (app/"escaped").exists() or not (app/"escaped").read_bytes()
        listing=subprocess.run(["smbclient","-L","127.0.0.1","-p","1445","-A",str(auth),"-m","SMB3"],capture_output=True,text=True,timeout=30)
        if "Movies" not in listing.stdout:
            for log in (app/"samba/logs").glob("*"):
                if log.is_file():print(log.name,log.read_text(errors="replace")[-8000:])
        assert listing.returncode==0 and "Movies" in listing.stdout and "ROMs" in listing.stdout,(listing.stdout,listing.stderr)
        print("PASS: actual ARM SMB3 authentication, guest denial, ROM/movie upload, download, share listing and symlink confinement")
    finally:
        stopped=server("disable")
        if stopped.returncode:
            for log in (app/"samba/logs").glob("*"):
                if log.is_file():print(log.name,log.read_text(errors="replace")[-7000:])
        assert stopped.returncode==0,(stopped.stdout,stopped.stderr)
        assert server("status").stdout.strip()=="STOPPED"
        assert not (app/"samba/.enabled").exists()
        assert server("stop").returncode==0
        assert server("start").returncode==0
        assert server("stop").returncode==0
        print("PASS: opt-in enable/disable, repeated start/stop and restart")
