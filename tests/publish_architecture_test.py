"""A single-ABI APK must never replace the universal Android update channel."""
import contextlib
import io
import json
import os
from pathlib import Path
import runpy
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/publish-android.py'


class AndroidPublicationTest(unittest.TestCase):
    def publish(self, abis):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            apk = root / 'build.apk'
            with zipfile.ZipFile(apk, 'w') as archive:
                for abi in abis:
                    archive.writestr(f'lib/{abi}/libtest.so', b'fixture')
            notes = root / 'notes.md'
            notes.write_text('- A change\n- Literal $(do-not-run)\n')
            calls, release_notes = [], []

            def run(command, **kwargs):
                calls.append(command)
                if '--notes-file' in command:
                    release_notes.append(Path(command[command.index('--notes-file') + 1]).read_text())
                return SimpleNamespace(returncode=1 if command[:3] == ['gh', 'release', 'view'] else 0)

            badging = "package: name='org.flxtr.android' versionCode='11' versionName='test'\nsdkVersion:'26'\n"
            with patch.dict(os.environ, FLXTR_RELEASE_NOTES_FILE=str(notes), FLXTR_RELEASE_TITLE='Chosen title'), \
                 patch.object(sys, 'argv', [str(SCRIPT), '--apk', str(apk), '--sha', 'a' * 40]), \
                 patch('subprocess.check_output', return_value=badging), patch('subprocess.run', side_effect=run), \
                 patch('urllib.request.urlopen', return_value=io.BytesIO(json.dumps({'versionCode': 10}).encode())), \
                 contextlib.redirect_stdout(io.StringIO()):
                try:
                    runpy.run_path(str(SCRIPT), run_name='__main__')
                except SystemExit as exit:
                    self.assertEqual(exit.code, 0)
            self.assertTrue(release_notes[0].startswith(notes.read_text()))
            creation = next(command for command in calls if command[:3] == ['gh', 'release', 'create'])
            self.assertEqual(creation[creation.index('--title') + 1], 'Chosen title')
            return calls

    def test_arm64_does_not_advance_universal_channel(self):
        calls = self.publish(['arm64-v8a'])
        self.assertFalse(any('android-latest' in command for command in calls))
        self.assertTrue(any('android-11-aaaaaaaaaaaa-arm64' in command for command in calls))

    def test_universal_advances_update_channel(self):
        calls = self.publish(['arm64-v8a', 'armeabi-v7a'])
        self.assertTrue(any(command[:4] == ['gh', 'release', 'upload', 'android-latest'] for command in calls))


if __name__ == '__main__':
    unittest.main()
