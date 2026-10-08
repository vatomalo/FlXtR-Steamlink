"""Integration test against a built/downloaded manifest, without invoking sudo."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('installer', root / 'scripts/linux-install.py')
installer = importlib.util.module_from_spec(spec); spec.loader.exec_module(installer)
manifest_path = Path(sys.argv[1]); manifest = json.loads(manifest_path.read_text())
bundle = manifest_path.with_suffix('.tgz')
real_run = subprocess.run
sudo_calls = []

def run(args, **kwargs):
    if args[0] == 'sudo':
        assert args[:4] == ['sudo', 'install', '-m', '755']
        assert args[-1] == '/usr/local/bin/flxtr'
        text = Path(args[4]).read_text()
        assert text.startswith('#!/bin/sh\nexec ') and '"$@"' in text
        sudo_calls.append(args)
        return SimpleNamespace(returncode=0)
    return real_run(args, **kwargs)

with tempfile.TemporaryDirectory(prefix='flxtr-install-test-') as temporary:
    home = Path(temporary) / 'path with spaces'
    old = home / 'releases/old'; old.mkdir(parents=True)
    (old / 'build.json').write_text(json.dumps(dict(platform=manifest['platform'], version='0'*40, build=1)))
    (home / 'current').symlink_to('releases/old')
    data = home / 'data'; data.mkdir(); (data / 'settings.cfg').write_text('sentinel')
    with patch.object(installer.subprocess, 'run', side_effect=run):
        installer.install(manifest, bundle, home)
        installer.install(manifest, bundle, home)
    assert len(sudo_calls) == 2
    assert (home / 'current').resolve().name == manifest['version']
    assert (home / 'previous').resolve() == old
    assert (data / 'settings.cfg').read_text() == 'sentinel'
    assert (home / 'FlXtR.sh').stat().st_mode & 0o111
print('PASS: real package installation, repeat install, paths with spaces, settings, rollback and sudo-only launcher')
