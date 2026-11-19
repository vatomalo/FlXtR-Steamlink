"""Offline regressions for failed-build log replay and pinned Moonlight cleanup."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from android_sources import checkout


class TestBuildLogReplay(unittest.TestCase):
    def test_full_output_remains_copyable_without_a_build(self):
        with tempfile.TemporaryDirectory() as directory:
            logs = Path(directory) / "build-logs"
            logs.mkdir()
            (logs / "flxtr-20261119T001000Z-17.log").write_text("old failure\n")
            newer = logs / "flxtr-20261119T001500Z-19.log"
            newer.write_text("javac: missing GamezBridge\nBUILD FAILED\n")
            environment = dict(os.environ, FLXTR_STORAGE=directory)
            cmd = ["bash", str(ROOT / "scripts/flxtr-build")]
            copy = subprocess.run(cmd + ["--last-log"], capture_output=True, text=True,
                                  env=environment, check=True)
            self.assertEqual(copy.stdout, newer.read_text())
            path = subprocess.run(cmd + ["--last-log-path"], capture_output=True,
                                  text=True, env=environment, check=True)
            self.assertEqual(path.stdout.strip(), str(newer))
            self.assertEqual(copy.stderr, "")

    def test_no_saved_output_reports_error(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(
                ["bash", str(ROOT / "scripts/flxtr-build"), "--last-log"],
                env=dict(os.environ, FLXTR_STORAGE=directory), capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("No saved", result.stderr)


class TestGeneratedAndroidSources(unittest.TestCase):
    @staticmethod
    def git(directory, *args):
        return subprocess.run(["git", "-C", str(directory), *args], check=True,
                              capture_output=True, text=True).stdout.strip()

    def test_dirty_upstream_appview_is_backed_up_and_reset(self):
        with tempfile.TemporaryDirectory() as directory:
            deps = Path(directory) / "game-sources"
            upstream = deps / "moonlight"
            java = upstream / "app/src/main/java/com/limelight"
            java.mkdir(parents=True)
            self.git(upstream, "init")
            self.git(upstream, "config", "user.email", "flxtr-tests@example.invalid")
            self.git(upstream, "config", "user.name", "FlXtR test")
            view = java / "AppView.java"
            view.write_text("package com.limelight;\npublic class AppView {}\n")
            self.git(upstream, "add", ".")
            self.git(upstream, "commit", "-m", "pinned upstream")
            rev = self.git(upstream, "rev-parse", "HEAD")
            view.write_text("package com.limelight;\nimport com.limelight.flxtr.GamezBridge;\n")
            returned = checkout(deps, "moonlight", str(upstream), rev)
            self.assertEqual(returned, upstream.resolve())
            self.assertEqual(view.read_text(),
                             "package com.limelight;\npublic class AppView {}\n")
            backups = list(deps.glob("moonlight-local-*.patch"))
            self.assertEqual(len(backups), 1)
            self.assertIn(b"GamezBridge", backups[0].read_bytes())
            self.assertEqual(self.git(upstream, "status", "--porcelain"), "")

    def test_invalid_checkout_name_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(RuntimeError):
                checkout(Path(directory), "../outside", "unused", "unused")


if __name__ == "__main__":
    unittest.main()
