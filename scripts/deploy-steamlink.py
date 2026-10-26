#!/usr/bin/env python3
"""Verify and deploy shell/catalog/player and playback activity helpers.

Run in WSL/Linux with SSH access. User libraries and settings are never extracted.
"""
import argparse
import hashlib
import io
import re
import shlex
import subprocess
import tarfile
import tempfile
import uuid
from pathlib import Path

BINARIES = ("greenlink", "greenlink-catalog", "greenlink-player")
PAYLOAD_FILES = BINARIES + ("greenlink.sh", "power-activity.sh")


def verified_payload(package, destination):
    with tarfile.open(package, "r:gz") as archive:
        members = {}
        for member in archive.getmembers():
            parts = member.name.split("/")
            if parts[0] != "greenlink" or ".." in parts or member.name.startswith("/"):
                raise ValueError(f"Unsafe package path: {member.name}")
            if member.name in members:
                raise ValueError(f"Duplicate package member: {member.name}")
            members[member.name] = member

        def read(name):
            member = members["greenlink/" + name]
            if not member.isfile() or member.size > 16 * 1024 * 1024:
                raise ValueError(f"Invalid payload file: {name}")
            return archive.extractfile(member).read()

        version = read("build-version.txt").decode().strip()
        if not re.fullmatch(r"[0-9a-f]{40}", version):
            raise ValueError("Package must contain a complete commit ID")
        if read("build-source-dirty.txt").strip() != b"0":
            raise ValueError("Commit the source before building a deployment package")
        manifest = read("build-sha256.txt")
        expected = {}
        for line in manifest.decode().splitlines():
            digest, name = line.split()
            if name not in PAYLOAD_FILES or name in expected or not re.fullmatch(r"[0-9a-f]{64}", digest):
                raise ValueError("Invalid binary manifest")
            expected[name] = digest
        if set(expected) != set(PAYLOAD_FILES):
            raise ValueError("Incomplete binary manifest")
        files = {name: read(name) for name in PAYLOAD_FILES}
        for name, data in files.items():
            if hashlib.sha256(data).hexdigest() != expected[name]:
                raise ValueError(f"Package hash mismatch: {name}")
            if name in BINARIES and (data[:5] != b"\x7fELF\x01" or int.from_bytes(data[18:20], "little") != 40):
                raise ValueError(f"Not a 32-bit ARM ELF executable: {name}")
        files["build-version.txt"] = (version + "\n").encode()
        files["build-sha256.txt"] = manifest
        with tarfile.open(destination, "w:gz") as payload:
            for name, data in files.items():
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = 0o755 if name in PAYLOAD_FILES else 0o644
                payload.addfile(info, io.BytesIO(data))
        return version


REMOTE = r'''
set -eu
app=/home/apps/greenlink
stage=/home/apps/.flxtr-stage-TOKEN
backup=/home/apps/greenlink-backup-TOKEN
upload=/home/apps/.flxtr-upload-TOKEN.tgz
test ! -d "$app/.update-lock"
available=$(df -Pk "$app" | awk 'NR==2 {print $4}')
test "$available" -ge REQUIRED_KB || { echo 'Insufficient space for staging, backup and atomic replacement' >&2; exit 1; }
mkdir "$stage" "$backup"
tar -xzf "$upload" -C "$stage"
cd "$stage"
sha256sum -c build-sha256.txt
for part in greenlink greenlink-catalog greenlink-player; do
    test "$(./$part --version)" = VERSION
done
./greenlink --mtv-genres
cd "$app"
test -x ./greenlink.sh
# Back up exactly the files being replaced, plus private configuration for recovery.
for part in greenlink greenlink-catalog greenlink-player greenlink.sh power-activity.sh build-version.txt build-sha256.txt .no-auto-update; do
    if test -e "$part"; then cp -p "$part" "$backup/$part"; fi
done
for part in settings.cfg mtv-weights.tsv mtv-query.txt mtv-fusion-style.txt mtv-vga-subject.txt catalog.local.tsv favorites.tsv tv-progress.tsv; do
    if test -f "$part"; then cp -p "$part" "$backup/$part"; sha256sum "$part"; fi
done > "$backup/private-state-sha256.txt"
changed=0
rollback() {
    result=$?
    if test "$result" -ne 0 && test "$changed" -eq 1; then
        for part in greenlink greenlink-catalog greenlink-player greenlink.sh power-activity.sh build-version.txt build-sha256.txt .no-auto-update; do
            if test -f "$backup/$part"; then
                cp -p "$backup/$part" "$app/$part.rollback"
                mv -f "$app/$part.rollback" "$app/$part"
            else rm -f "$app/$part"; fi
        done
        echo "Deployment failed; previous files restored from $backup" >&2
    fi
}
trap rollback EXIT
changed=1
printf 'Manual feature-branch test; preserve this build until testing is complete.\n' > .no-auto-update
for part in greenlink greenlink-catalog greenlink-player greenlink.sh power-activity.sh build-version.txt build-sha256.txt; do
    cp -p "$stage/$part" "$part.deploy-new"
    mv -f "$part.deploy-new" "$part"
done
sha256sum -c build-sha256.txt
for part in greenlink greenlink-catalog greenlink-player; do
    test "$(./$part --version)" = VERSION
done
sha256sum -c "$backup/private-state-sha256.txt"
printf 'Installed commit: %s\nBackup: %s\n' VERSION "$backup"
./greenlink --mtv-genres
echo 'Files verified. Relaunch the app to replace the running process.'
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--host", default="root@192.168.10.2")
    parser.add_argument("--control-path", help="Existing OpenSSH multiplexing socket")
    parser.add_argument("--verify-only", action="store_true", help="Validate package locally without SSH")
    args = parser.parse_args()
    options = ["-o", "ConnectTimeout=10"]
    if args.control_path:
        options += ["-S", args.control_path]
    with tempfile.TemporaryDirectory(prefix="flxtr-deploy-") as temp:
        payload = Path(temp) / "payload.tgz"
        version = verified_payload(args.package, payload)
        print(f"Verified package: {version}", flush=True)
        if args.verify_only:
            return
        token = uuid.uuid4().hex
        remote = f"/home/apps/.flxtr-upload-{token}.tgz"
        # Allow space for upload, uncompressed staging, old binaries and replacements.
        with tarfile.open(payload) as archive:
            required_kb = (sum(item.size for item in archive) * 4 + payload.stat().st_size) // 1024 + 2048
        preflight = 'test ! -d /home/apps/greenlink/.update-lock && df -Pk /home/apps/greenlink'
        status = subprocess.run(["ssh", *options, args.host, preflight], capture_output=True, text=True, check=True)
        print(status.stdout, end="", flush=True)
        if int(status.stdout.splitlines()[-1].split()[3]) < required_kb:
            raise RuntimeError(f"Need at least {required_kb} KiB free before deployment")
        # Transfer via SSH, avoiding modern scp's SFTP requirement on old firmware.
        with payload.open("rb") as source:
            subprocess.run(["ssh", *options, args.host, "cat > " + shlex.quote(remote)], stdin=source, check=True)
        script = REMOTE.replace("TOKEN", token).replace("VERSION", shlex.quote(version)).replace("REQUIRED_KB", str(required_kb))
        subprocess.run(["ssh", *options, args.host, "sh -s"], input=script, text=True, check=True)


if __name__ == "__main__":
    main()
