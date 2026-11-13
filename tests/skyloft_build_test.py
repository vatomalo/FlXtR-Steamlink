"""Exercise branch updates and build dispatch against real disposable Git repos."""
import fcntl
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

COMMAND = Path(__file__).resolve().parents[1] / 'scripts/flxtr-build'


class BuildCommandTest(unittest.TestCase):
    def git(self, directory, *args):
        return subprocess.check_output(['git', '-C', str(directory), *args], stderr=subprocess.DEVNULL, text=True).strip()

    def identity(self, directory):
        self.git(directory, 'config', 'user.name', 'Build Test')
        self.git(directory, 'config', 'user.email', 'test@example.invalid')

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.storage = root / 'storage'
        self.project = self.storage / 'projects/FlXtR'
        self.project.mkdir(parents=True)
        self.producer = root / 'producer'
        self.producer.mkdir()
        self.remote = root / 'remote.git'
        subprocess.run(['git', 'init', '-q', '--bare', '-b', 'main', str(self.remote)], check=True)
        subprocess.run(['git', 'init', '-q', '-b', 'main', str(self.producer)], check=True)
        self.identity(self.producer)
        (self.producer / 'scripts').mkdir()
        for name, target in [('build-skyloft-android.sh', 'android'), ('build-skyloft-steamlink.sh', 'steamlink')]:
            (self.producer / 'scripts' / name).write_text(
                f'#!/bin/bash\nprintf "{target}\\n" >> build-events\n'
                f'if [[ ${{FAIL_TARGET:-}} == {target} ]]; then exit 23; fi\n')
        (self.producer / 'scripts/publish-skyloft.sh').write_text(
            '#!/bin/bash\n'
            'if [[ $1 == --check ]]; then [[ ${FAIL_AUTH:-0} != 1 ]] || exit 17; exit 0; fi\n'
            'printf "publish-%s\\n" "${1#--}" >> build-events\n'
            'if [[ ${FAIL_PUBLISH:-0} == 1 ]]; then exit 42; fi\n')
        (self.producer / 'scripts/build-skyloft-desktop.sh').write_text(
            '#!/bin/bash\ntarget=${1#--}\nprintf "%s\\n" "$target" >> build-events\n'
            'if [[ ${FAIL_TARGET:-} == "$target" ]]; then exit 23; fi\n'
            'mkdir -p dist\n'
            'if [[ $target == linux ]]; then name=FlXtR-Linux-x86_64.tgz; else name=FlXtR-Windows-x86_64.zip; fi\n'
            'printf "finished fixture package" > "dist/$name"\n')
        (self.producer / '.gitignore').write_text('build-events\ndist/\n')
        self.git(self.producer, 'add', '.')
        self.git(self.producer, 'commit', '-qm', 'fixture')
        self.git(self.producer, 'remote', 'add', 'origin', str(self.remote))
        self.git(self.producer, 'push', '-q', 'origin', 'main')
        self.repo = self.project / 'source'
        subprocess.run(['git', 'clone', '-q', str(self.remote), str(self.repo)], check=True)
        self.identity(self.repo)
        (self.project / 'check-signing.sh').write_text('printf "signed\\n" >> "$FLXTR_BUILD_SOURCE/build-events"\n')
        commands = root / 'bin'
        commands.mkdir()
        mountpoint = commands / 'mountpoint'
        mountpoint.write_text('#!/bin/sh\nexit 0\n')
        mountpoint.chmod(0o755)
        self.env = dict(os.environ, FLXTR_STORAGE=str(self.storage), FLXTR_BUILD_SOURCE=str(self.repo),
                        PATH=str(commands) + ':' + os.environ['PATH'])
        self.env.pop('FLXTR_BUILD_LOCK_HELD', None)

    def run_build(self, *args, publish=False, **env):
        options = list(args)
        if not publish:
            options.append('--no-publish')
        return subprocess.run(['bash', str(COMMAND), *options], env=dict(self.env, **env),
                              capture_output=True, text=True)

    def test_pull_and_dispatch_both(self):
        (self.producer / 'new-source').write_text('updated\n')
        self.git(self.producer, 'add', '.')
        self.git(self.producer, 'commit', '-qm', 'update')
        self.git(self.producer, 'push', '-q', 'origin', 'main')
        result = self.run_build('--all')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.git(self.repo, 'rev-parse', 'HEAD'), self.git(self.producer, 'rev-parse', 'HEAD'))
        self.assertEqual((self.repo / 'build-events').read_text(), 'android\nsigned\nsteamlink\nlinux\nwindows\n')

    def test_select_remote_branch_with_slash(self):
        self.git(self.producer, 'switch', '-qc', 'feature/phone')
        (self.producer / 'feature').write_text('phone build\n')
        self.git(self.producer, 'add', '.')
        self.git(self.producer, 'commit', '-qm', 'feature')
        self.git(self.producer, 'push', '-q', 'origin', 'feature/phone')
        result = self.run_build('--steamlink', '--branch', 'feature/phone')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.git(self.repo, 'branch', '--show-current'), 'feature/phone')
        self.assertEqual((self.repo / 'build-events').read_text(), 'steamlink\n')

    def test_dirty_checkout_is_preserved(self):
        note = self.repo / 'private-notes'
        note.write_text('keep me\n')
        self.assertNotEqual(self.run_build('--android').returncode, 0)
        self.assertEqual(note.read_text(), 'keep me\n')
        self.assertFalse((self.repo / 'build-events').exists())

    def test_generated_checkout_files_are_cleaned_before_build(self):
        generated = [
            self.repo / 'android/.gradle/cache.bin',
            self.repo / 'android/app/.cxx/build.ninja',
            self.repo / 'android/local.properties',
            self.repo / 'companion/FlXtR.Gamez/bin/Release/FlXtR.Gamez.dll',
        ]
        for path in generated:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('generated\n')
        result = self.run_build('--android')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('Cleaning generated files', result.stdout)
        self.assertFalse(any(path.exists() for path in generated))

    def test_generated_cleanup_does_not_hide_tracked_edits(self):
        tracked = self.repo / 'tracked-source'
        tracked.write_text('original\n')
        self.git(self.repo, 'add', '.')
        self.git(self.repo, 'commit', '-qm', 'tracked fixture')
        self.git(self.repo, 'push', '-q', 'origin', 'main')
        tracked.write_text('edited\n')
        generated = self.repo / 'android/.gradle/cache.bin'
        generated.parent.mkdir(parents=True)
        generated.write_text('generated\n')
        result = self.run_build('--android')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(tracked.read_text(), 'edited\n')
        self.assertFalse(generated.exists())

    def test_branch_predating_build_command_uses_installed_runner(self):
        self.assertEqual(self.run_build('--android').returncode, 0)
        self.git(self.producer, 'switch', '-qc', 'older/branch')
        self.git(self.producer, 'rm', '-qr', 'scripts')
        self.git(self.producer, 'commit', '-qm', 'older source without server runners')
        self.git(self.producer, 'push', '-q', 'origin', 'older/branch')
        result = self.run_build('--all', '--branch', 'older/branch')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.repo / 'build-events').read_text(), 'android\nsigned\nandroid\nsigned\nsteamlink\nlinux\nwindows\n')
        # A branch's older server runner must not replace the installed runner.
        (self.producer / 'scripts').mkdir()
        (self.producer / 'scripts/build-skyloft-android.sh').write_text('exit 99\n')
        self.git(self.producer, 'add', '.')
        self.git(self.producer, 'commit', '-qm', 'legacy runner')
        self.git(self.producer, 'push', '-q', 'origin', 'older/branch')
        result = self.run_build('--android')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_local_commit_is_not_reset(self):
        (self.repo / 'local').write_text('unpublished work\n')
        self.git(self.repo, 'add', '.')
        self.git(self.repo, 'commit', '-qm', 'local')
        before = self.git(self.repo, 'rev-parse', 'HEAD')
        self.assertNotEqual(self.run_build('--android').returncode, 0)
        self.assertEqual(self.git(self.repo, 'rev-parse', 'HEAD'), before)

    def test_build_error_propagates_and_stops_next_target(self):
        result = self.run_build('--all', publish=True, FAIL_TARGET='android')
        self.assertEqual(result.returncode, 23)
        self.assertEqual((self.repo / 'build-events').read_text(), 'android\n')

    def test_default_publishes_after_both_builds(self):
        result = self.run_build('--all', publish=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.repo / 'build-events').read_text(),
                         'android\nsigned\nsteamlink\nlinux\nwindows\npublish-android\npublish-steamlink\npublish-linux\npublish-windows\n')

    def test_no_publish_needs_no_github_auth(self):
        result = self.run_build('--all', '--no-publish', publish=True, FAIL_AUTH='1')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.repo / 'build-events').read_text(), 'android\nsigned\nsteamlink\nlinux\nwindows\n')

    def test_missing_auth_stops_before_build(self):
        result = self.run_build('--android', publish=True, FAIL_AUTH='1')
        self.assertEqual(result.returncode, 17)
        self.assertFalse((self.repo / 'build-events').exists())

    def test_failed_second_build_publishes_nothing(self):
        result = self.run_build('--all', publish=True, FAIL_TARGET='steamlink')
        self.assertEqual(result.returncode, 23)
        self.assertEqual((self.repo / 'build-events').read_text(), 'android\nsigned\nsteamlink\n')

    def test_publish_failure_is_reported(self):
        result = self.run_build('--all', publish=True, FAIL_PUBLISH='1')
        self.assertEqual(result.returncode, 42)
        self.assertEqual((self.repo / 'build-events').read_text(),
                         'android\nsigned\nsteamlink\nlinux\nwindows\npublish-android\n')

    def test_concurrent_build_is_rejected(self):
        folder = self.storage / 'build-home'
        folder.mkdir()
        with (folder / 'flxtr-build.lock').open('w') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            result = self.run_build('--android')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Another FlXtR build', result.stderr)
        self.assertFalse((self.repo / 'build-events').exists())

    def test_invalid_arguments(self):
        for args in [[], ['--branch'], ['--android', '--branch', '../invalid'], ['--macos'],
                     ['--steamlink', '--arch', 'arm64'], ['--android', '--arch', 'x86'], ['--android', '--notes-file', '/nonexistent-notes']]:
            self.assertEqual(self.run_build(*args).returncode, 2)

    def test_architecture_and_notes_are_forwarded_as_data(self):
        runner = self.repo / 'scripts/build-skyloft-android.sh'
        runner.write_text('printf "%s\\n" "$FLXTR_ANDROID_ABIS" >> build-events\n')
        self.git(self.repo, 'add', '.')
        self.git(self.repo, 'commit', '-qm', 'ABI fixture')
        self.git(self.repo, 'push', '-q', 'origin', 'main')
        notes = self.project / 'notes.md'
        notes.write_text('- Literal $(touch not-a-command)\n')
        result = self.run_build('--android', '--arch', 'arm64', '--notes-file', str(notes),
                                '--release-title', 'New build; still a title')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.repo / 'build-events').read_text(), 'arm64-v8a\nsigned\n')
        self.assertEqual(notes.read_text(), '- Literal $(touch not-a-command)\n')
        self.assertFalse((self.repo / 'not-a-command').exists())

    def test_desktop_packages_are_available_without_publishing(self):
        output = self.project / 'downloads'
        result = self.run_build('--desktop', '--output-dir', str(output), FAIL_AUTH='1')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.repo / 'build-events').read_text(), 'linux\nwindows\n')
        self.assertEqual(sorted(path.name for path in output.iterdir()),
                         ['FlXtR-Linux-x86_64.tgz', 'FlXtR-Windows-x86_64.zip'])

    def test_windows_publication_is_optional(self):
        result = self.run_build('--windows', '--arch', 'x86_64', publish=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.repo / 'build-events').read_text(), 'windows\npublish-windows\n')


if __name__ == '__main__':
    unittest.main()
