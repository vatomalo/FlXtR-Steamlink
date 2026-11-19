"""Safe reset of generated third-party Android checkouts to pinned revisions.

Never touch the main FlXtR Git checkout. Save tracked vendor edits as a patch
before resetting so earlier experimental Moonlight integrations are recoverable.
"""
import subprocess
from datetime import datetime, timezone
from pathlib import Path


def checkout(deps, name, url, revision):
    deps = Path(deps).resolve()
    path = (deps / name).resolve()
    if name not in ("moonlight", "gambatte", "fceumm") or not path.is_relative_to(deps):
        raise RuntimeError("Refusing to modify a path outside generated Android sources")
    if not (path / ".git").exists():
        subprocess.run(["git", "clone", "--no-checkout", url, str(path)], check=True)

    # git checkout --detach at the same SHA leaves edited AppView.java in place.
    # This caused a missing GamezBridge import to break the Android build.
    # Preserve a diff of tracked generated-vendor files, not the user's repo.
    dirty = subprocess.run(["git", "-C", str(path), "diff", "HEAD", "--binary"],
                           capture_output=True, check=True).stdout
    if dirty:
        timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        backup = deps / f"{name}-local-{timestamp}.patch"
        backup.write_bytes(dirty)
        print(f"Backed up uncommitted generated {name} patches: {backup}", flush=True)
    subprocess.run(["git", "-C", str(path), "reset", "--hard"], check=True)
    subprocess.run(["git", "-C", str(path), "checkout", "--detach", revision], check=True)
    subprocess.run(["git", "-C", str(path), "reset", "--hard", revision], check=True)
    subprocess.run(["git", "-C", str(path), "submodule", "update", "--init", "--recursive"],
                   check=True)
    return path
