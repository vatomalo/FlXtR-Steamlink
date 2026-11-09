"""Simulate a legacy updater that installed only the new assets directory."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BootstrapTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "assets").mkdir()
        (self.root / "source/moonlight").mkdir(parents=True)
        source = self.root / "source"
        for name in ("moonlight", "flxtr-audio-test"):
            path = source / "moonlight" / name
            path.write_bytes(b"#!/bin/sh\nexit 0\n")
            path.chmod(0o755)
        for name in ("LICENSE", "BUILD.txt"):
            (source / "moonlight" / name).write_text("test source information\n")
        (source / "moonlight/SHA256SUMS").write_text("".join(
            f"{hashlib.sha256((source / 'moonlight' / name).read_bytes()).hexdigest()}  {name}\n"
            for name in ("moonlight", "flxtr-audio-test")))
        shutil.copy(ROOT / "packaging/moonlight-run.sh", source)
        bundle = self.root / "assets/moonlight-bootstrap.tgz"
        with tarfile.open(bundle, "w:gz") as archive:
            archive.add(source / "moonlight", arcname="moonlight")
            archive.add(source / "moonlight-run.sh", arcname="moonlight-run.sh")
        (self.root / "assets/moonlight-bootstrap.sha256").write_text(
            hashlib.sha256(bundle.read_bytes()).hexdigest() + "  assets/moonlight-bootstrap.tgz\n")
        (self.root / "streaming-audio.txt").write_text("device:My Headset\n")
        (self.root / "settings.cfg").write_text("quality=1\n")

    def run_bootstrap(self, check=True):
        return subprocess.run(["sh", str(ROOT / "packaging/moonlight-bootstrap.sh")],
                              cwd=self.root, text=True, capture_output=True, check=check)

    def test_legacy_update_installs_client_and_preserves_preferences(self):
        self.run_bootstrap()
        self.assertTrue((self.root / "moonlight/moonlight").stat().st_mode & 0o111)
        self.assertTrue((self.root / "moonlight-run.sh").exists())
        self.assertEqual((self.root / "streaming-audio.txt").read_text(), "device:My Headset\n")
        self.assertEqual((self.root / "settings.cfg").read_text(), "quality=1\n")
        self.assertEqual(self.run_bootstrap().stdout, "")

    def test_corrupt_bundle_does_not_replace_existing_client(self):
        (self.root / "moonlight").mkdir()
        (self.root / "moonlight/moonlight").write_bytes(b"previous working binary")
        with (self.root / "assets/moonlight-bootstrap.tgz").open("ab") as bundle:
            bundle.write(b"corrupt")
        self.assertNotEqual(self.run_bootstrap(check=False).returncode, 0)
        self.assertEqual((self.root / "moonlight/moonlight").read_bytes(), b"previous working binary")


if __name__ == "__main__":
    unittest.main()
