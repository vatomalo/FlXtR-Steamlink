import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import runpy
import shutil
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/publish-desktop.py'


class DesktopPublishTest(unittest.TestCase):
    def publish(self, platform, stale=False, existing=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'scripts').mkdir()
            copy = root / 'scripts/publish-desktop.py'
            shutil.copy(SCRIPT, copy)
            (root / 'dist').mkdir()
            name = 'FlXtR-Linux-x86_64.tgz' if platform == 'linux' else 'FlXtR-Windows-x86_64.zip'
            package = root / 'dist' / name
            package.write_bytes(b'complete package fixture')
            (root / 'dist' / (platform + '-desktop-build.json')).write_text(json.dumps(dict(
                platform=platform, architecture='x86_64', commit=('b' if stale else 'a') * 40,
                artifact=name, size=package.stat().st_size, sha256=hashlib.sha256(package.read_bytes()).hexdigest())))
            notes = root / 'notes.md'
            notes.write_text('- The announced change\n- Literal $(not-a-command)\n')
            calls, observed_notes = [], []

            def run(command, **kwargs):
                calls.append(command)
                if '--notes-file' in command:
                    observed_notes.append(Path(command[command.index('--notes-file') + 1]).read_text())
                if command[:3] == ['gh', 'release', 'view']:
                    return SimpleNamespace(returncode=0 if existing else 1, stdout='{"isDraft":false}')
                return SimpleNamespace(returncode=0)

            before = Path.cwd()
            try:
                with patch.dict(os.environ, FLXTR_RELEASE_TITLE='Chosen desktop title', FLXTR_RELEASE_NOTES_FILE=str(notes)), \
                     patch.object(sys, 'argv', [str(copy), platform]), patch('subprocess.run', side_effect=run), \
                     patch('subprocess.check_output', side_effect=lambda command, **kwargs: 'a'*40 if 'rev-parse' in command else b''), \
                     contextlib.redirect_stdout(io.StringIO()):
                    if stale:
                        with self.assertRaises(SystemExit):
                            runpy.run_path(str(copy), run_name='__main__')
                    else:
                        runpy.run_path(str(copy), run_name='__main__')
            finally:
                os.chdir(before)
            return calls, observed_notes

    def test_linux_release_contains_finished_package_and_notes(self):
        calls, notes = self.publish('linux')
        create = next(command for command in calls if command[:3] == ['gh', 'release', 'create'])
        self.assertIn('--prerelease', create)
        self.assertEqual(create[create.index('--title')+1], 'Chosen desktop title')
        self.assertTrue(notes[0].startswith('- The announced change\n- Literal $(not-a-command)\n'))
        upload = next(command for command in calls if command[:3] == ['gh', 'release', 'upload'])
        self.assertTrue(any(value.endswith('FlXtR-Linux-x86_64.tgz') for value in upload))
        self.assertTrue(any(value.endswith('.sha256') for value in upload))

    def test_stale_windows_package_cannot_publish(self):
        calls, _ = self.publish('windows', stale=True)
        self.assertEqual(calls, [])

    def test_existing_windows_payload_is_not_overwritten(self):
        calls, _ = self.publish('windows', existing=True)
        self.assertFalse(any(command[:3] == ['gh', 'release', 'upload'] for command in calls))
        self.assertTrue(any(command[:3] == ['gh', 'release', 'edit'] for command in calls))


if __name__ == '__main__':
    unittest.main()
