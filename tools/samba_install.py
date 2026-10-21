"""Download pinned upstream ARM packages into an app-private Samba runtime."""
import fcntl
import gzip
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import signal
import ssl
import sys
import tarfile
import tempfile
import time
import urllib.request

MAX_DOWNLOAD = 80 * 1024 * 1024
MAX_UNPACKED = 128 * 1024 * 1024
HOST = "https://dl-cdn.alpinelinux.org/alpine/v3.23/main/armv7/"

def progress(base, message):
    temp = base / "samba-install-status.next"
    temp.write_text(message[:160] + "\n")
    temp.replace(base / "samba-install-status.txt")
    print(message, flush=True)

class HTTPSRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not newurl.startswith("https://"):
            raise ValueError("Insecure package redirect")
        return super().redirect_request(req, fp, code, msg, headers, newurl)

def download(package, output, cert, deadline=None):
    if not package["url"].startswith(HOST):
        raise ValueError("Unexpected package origin")
    size = package["size"]
    if not 0 < size <= MAX_DOWNLOAD:
        raise ValueError("Invalid package size")
    context = ssl.create_default_context(cafile=str(cert) if cert.is_file() else None)
    opener = urllib.request.build_opener(HTTPSRedirect(), urllib.request.HTTPSHandler(context=context))
    digest = hashlib.sha256()
    count = 0
    with opener.open(package["url"], timeout=30) as response, output.open("xb") as target:
        while True:
            if deadline is not None and time.monotonic() > deadline:
                raise TimeoutError("Samba download exceeded ten minutes")
            data = response.read(65536)
            if not data:
                break
            count += len(data)
            if count > size:
                raise ValueError("Package exceeds pinned size")
            digest.update(data)
            target.write(data)
    if count != size or digest.hexdigest() != package["sha256"]:
        raise ValueError("Package checksum mismatch")

def safe_path(root, name):
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ValueError("Unsafe package path")
    target = root.joinpath(*path.parts)
    for parent in target.parents:
        if parent == root:
            break
        if parent.is_symlink():
            raise ValueError("Package path crosses a symlink")
    return target

def unpack(package, root, total):
    # APK v2 has concatenated signature, control and data gzip/tar members.
    # Ignore tar terminators, but consume gzip to EOF to verify its CRC.
    with gzip.open(package, "rb") as stream:
        with tarfile.open(fileobj=stream, mode="r|", ignore_zeros=True) as archive:
            for item in archive:
                path = PurePosixPath(item.name)
                if path.is_absolute() or ".." in path.parts:
                    raise ValueError("Unsafe package path")
                parts = path.parts
                if not parts or parts[0] not in ("lib", "usr"):
                    continue
                target = safe_path(root, item.name)
                if item.isdir():
                    if target.is_symlink():
                        raise ValueError("Directory is a symlink")
                    target.mkdir(parents=True, exist_ok=True)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                if item.issym():
                    link = PurePosixPath(item.linkname)
                    if link.is_absolute():
                        raise ValueError("Absolute runtime link")
                    relative = os.path.normpath(str(PurePosixPath(item.name).parent / link))
                    safe_path(root, relative)
                    if target.exists() or target.is_symlink():
                        if target.is_symlink() and os.readlink(target) == item.linkname:
                            continue
                        raise ValueError("Duplicate runtime link")
                    target.symlink_to(item.linkname)
                elif item.isfile():
                    total += item.size
                    if item.size < 0 or total > MAX_UNPACKED or target.is_symlink():
                        raise ValueError("Unsafe runtime file")
                    with archive.extractfile(item) as source, target.open("wb") as output:
                        shutil.copyfileobj(source, output, 65536)
                    target.chmod(0o755 if item.mode & 0o111 else 0o644)
                else:
                    raise ValueError("Unsupported runtime member")
        while stream.read(65536):
            pass
    return total

def validate(root):
    for name in ("lib/ld-musl-armhf.so.1", "usr/sbin/smbd",
                 "usr/bin/smbpasswd", "usr/bin/smbcontrol", "usr/bin/testparm",
                 "usr/lib/samba/samba-dcerpcd", "usr/lib/samba/rpcd_classic"):
        with (root / name).open("rb") as f:
            header = f.read(20)
        if header[:5] != b"\x7fELF\x01" or header[5] != 1 or header[18:20] != b"\x28\x00":
            raise ValueError("Runtime is not ARM32: " + name)

def install(base, fetch=download):
    base = Path(base).resolve()
    with (base / ".samba-install-lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        destination = base / "samba-runtime"
        if destination.exists():
            validate(destination)
            progress(base, "SAMBA ALREADY INSTALLED / SET PASSWORD THEN ENABLE")
            return
        manifest = json.loads((base / "samba-packages.json").read_text())
        packages = manifest["packages"]
        if not packages or sum(p["size"] for p in packages) > MAX_DOWNLOAD:
            raise ValueError("Invalid package manifest")
        if shutil.disk_usage(base).free < MAX_UNPACKED + 32 * 1024 * 1024:
            raise ValueError("At least 160 MB free storage required")
        with tempfile.TemporaryDirectory(prefix=".samba-download-", dir=base) as staging:
            stage = Path(staging)
            runtime = stage / "samba-runtime"
            runtime.mkdir()
            total = 0
            deadline = time.monotonic() + 600
            for index, package in enumerate(packages):
                progress(base, "SAMBA DOWNLOAD %d / %d" % (index + 1, len(packages)))
                archive = stage / "package.apk"
                if fetch is download:
                    fetch(package, archive, base / "certs/cacert.pem", deadline=deadline)
                else:
                    fetch(package, archive, base / "certs/cacert.pem")
                total = unpack(archive, runtime, total)
                archive.unlink()
            validate(runtime)
            shutil.copyfile(base / "samba-paths.so", runtime / "flxtr-samba-paths.so")
            shutil.copyfile(base / "samba-packages.json", runtime / "PACKAGE-SOURCES.json")
            (runtime / "VERSION").write_text(manifest["version"] + "\n")
            runtime.rename(destination)
        progress(base, "SAMBA INSTALLED / SET PASSWORD THEN ENABLE")

def main():
    base = Path(__file__).resolve().parent.parent
    def interrupted(signum, frame):
        raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM, interrupted)
    try:
        install(base)
    except (Exception, KeyboardInterrupt) as error:
        progress(base, "SAMBA INSTALL FAILED / SEE SAMBA-SERVICE.LOG")
        print(type(error).__name__ + ": " + str(error), file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
