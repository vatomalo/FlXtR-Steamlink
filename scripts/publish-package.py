#!/usr/bin/env python3
"""Publish a complete Steam Link package without modifying publish-shell.py.

Requires a freshly built dist/greenlink.tgz and GH_TOKEN (or token on stdin).
Releases are immutable: package-<commit> is never overwritten.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import urllib.error
import urllib.parse
import urllib.request

root = Path(__file__).resolve().parent.parent
os.chdir(root)

def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()

if git("status", "--porcelain"):
    sys.exit("Checkout has uncommitted changes. Commit or stash them, then rebuild.")
version = git("rev-parse", "HEAD")
if git("rev-parse", "HEAD") != version:
    sys.exit("Git HEAD changed during preparation.")
package = Path("dist/greenlink.tgz")
if not package.is_file():
    sys.exit("Missing dist/greenlink.tgz; run bash scripts/build-steamlink.sh first.")
manifest_path = Path("dist/shell-manifest.txt")
if not manifest_path.is_file():
    sys.exit("Missing shell manifest; rebuild first.")
manifest = manifest_path.read_text()
if not manifest.startswith(f"version={version}\n"):
    sys.exit("Build version differs from current HEAD; rebuild after committing.")
shell = Path("build/greenlink-arm")
if not shell.is_file() or f"sha256={hashlib.sha256(shell.read_bytes()).hexdigest()}\n" not in manifest:
    sys.exit("Shell manifest hash does not match binary; rebuild.")
with tarfile.open(package, "r:gz") as archive:
    member = archive.extractfile("greenlink/greenlink")
    if member is None or hashlib.sha256(member.read()).digest() != hashlib.sha256(shell.read_bytes()).digest():
        sys.exit("Package shell differs from freshly built shell; rebuild.")
data = package.read_bytes()
digest = hashlib.sha256(data).hexdigest()
build_info = f"version={version}\nsha256={digest}\nasset=greenlink.tgz\n".encode()
token = os.environ.get("GH_TOKEN") or sys.stdin.readline().strip()
if not token:
    sys.exit("Set GH_TOKEN or provide a token via stdin (not stored).")
base = "https://api.github.com/repos/vatomalo/FlXtR-Steamlink"

def request(url, payload, mime="application/json", method="POST"):
    req = urllib.request.Request(url, data=payload, method=method, headers={
        "Authorization": "Bearer " + token,
        "Accept": "application/vnd.github+json",
        "Content-Type": mime,
        "User-Agent": "FlXtR-package-publisher",
        "X-GitHub-Api-Version": "2022-11-28",
    })
    try:
        with urllib.request.urlopen(req, timeout=90) as response:
            return json.load(response)
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")
        sys.exit(f"GitHub API HTTP {exc.code}: {detail}")

tag = "package-" + version
print(f"Publishing {tag}, package sha256={digest}")
release = request(base + "/releases", json.dumps({
    "tag_name": tag, "target_commitish": version,
    "name": "FlXtR Steam Link package " + version[:8],
    "draft": True,
    "make_latest": "false",
    "body": "Complete Steam Link app package with binaries, launcher, and assets. SHA-256: " + digest,
}).encode())
upload_url = release["upload_url"].split("{", 1)[0]
for name, payload, mime in (
    ("greenlink.tgz", data, "application/gzip"),
    ("build-info.txt", build_info, "text/plain"),
):
    request(upload_url + "?" + urllib.parse.urlencode({"name": name}), payload, mime)
published = request(base + "/releases/" + str(release["id"]),
                    b'{"draft":false,"make_latest":"false"}', method="PATCH")
print(published["html_url"])
