"""Real launcher recovery: mixed media errors recover, healthy games are not killed."""
import os, pathlib, shutil, subprocess, tempfile, signal
launcher=pathlib.Path('packaging/greenlink.sh').resolve()
for mode in ("healthy","burst","failing"):
    failing=mode=="failing"
    with tempfile.TemporaryDirectory() as tmp:
        root=pathlib.Path(tmp);shutil.copy(launcher,root/'greenlink.sh')
        (root/'.no-auto-update').touch()
        def executable(name,body):
            p=root/name;p.write_text(body);p.chmod(0o755)
        executable('greenlink','#!/bin/sh\nif [ ! -f launched ]; then touch launched; exit 43; fi\nexit 0\n')
        executable('sleep','#!/usr/bin/env python3\nimport time\ntime.sleep(0.1)\n')
        executable('greenlink-catalog','#!/usr/bin/env python3\nimport os,signal,time,pathlib\npathlib.Path("test-pid").write_text(str(os.getpid()))\n'+(
            'signal.signal(signal.SIGTERM,signal.SIG_IGN)\nwhile True:\n print("MV_PE_InputBufWrite() failed: 0x80007061\\nother driver diagnostic",flush=True)\n time.sleep(0.01)\n' if failing else ('print(("MV_PE_InputBufWrite() failed: 0x80007061\\n"*8),flush=True)\nfor i in range(30):\n print("healthy progress",flush=True)\n time.sleep(0.02)\n' if mode=="burst" else 'time.sleep(0.3)\n')))
        try:
            subprocess.run(['sh','greenlink.sh'],cwd=root,env={**os.environ,'PATH':str(root)+':'+os.environ['PATH']},timeout=8,check=True,capture_output=True)
            code=int((root/'game-exit-status').read_text())
            assert (code!=0)==failing
            assert (root/'game-failure.log').exists()==failing
            if failing: assert 'media input failure' in (root/'watchdog.log').read_text()
        finally:
            if (root/'test-pid').exists():
                try:os.kill(int((root/'test-pid').read_text()),signal.SIGKILL)
                except ProcessLookupError:pass
print('PASS: healthy launch, repeated mixed media errors, forced recovery and preserved failure log')
