"""Exercise the real desktop player through its inherited control/preload pipes."""
from pathlib import Path
import os
import subprocess
import tempfile
import time
root = Path(__file__).resolve().parents[1]
binary = root / 'build/desktop-player-platform-test'
flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'mpv'], text=True).split()
subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                'tests/desktop_fullscreen_test.c', *flags, '-pthread', '-o', str(binary)], cwd=root, check=True)
with tempfile.TemporaryDirectory(prefix='flxtr player ø ') as directory:
    home = Path(directory)
    clip = home / 'fixture.mp4'
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 'lavfi', '-i',
                    'testsrc2=size=160x90:rate=24', '-f', 'lavfi', '-i',
                    'sine=frequency=440', '-t', '1', '-c:v', 'libx264',
                    '-c:a', 'aac', str(clip)], check=True)
    env = dict(os.environ)
    subprocess.run([str(binary), str(clip), '--headless', '--duration', '1'],
                   cwd=home, env=env, check=True, timeout=15, capture_output=True)
    controls = os.pipe()
    gate = os.pipe()
    child = subprocess.Popen([str(binary), str(clip), '--headless',
                              '--control-fd', str(controls[0]),
                              '--preload-fd', str(gate[0])],
                             cwd=home, env=env, pass_fds=(controls[0], gate[0]),
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    os.close(controls[0])
    os.close(gate[0])
    try:
        time.sleep(.1)
        assert child.poll() is None and not (home / 'playback-request').exists()
        os.write(controls[1], b'b')
        os.write(gate[1], b'G')
        output = child.communicate(timeout=15)[0]
        assert child.returncode == 40, output
        assert (home / 'playback-request').read_text().split()[0] == '7'
    finally:
        os.close(controls[1])
        os.close(gate[1])
        if child.poll() is None:
            child.kill()
            child.wait()
    controls = os.pipe()
    child = subprocess.Popen([str(binary),str(clip),'--headless','--control-fd',str(controls[0])],
                             cwd=home,env=env,pass_fds=(controls[0],),stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    os.close(controls[0])
    try:
        os.write(controls[1],b'q')
        output=child.communicate(timeout=15)[0]
        assert child.returncode==40,output
        assert (home/'playback-request').read_text().split()[0]=='10'
    finally:
        os.close(controls[1])
        if child.poll() is None:child.kill();child.wait()
    gate = os.pipe()
    child = subprocess.Popen([str(binary), str(clip), '--headless',
                              '--preload-fd', str(gate[0])],
                             cwd=home, env=env, pass_fds=(gate[0],),
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    os.close(gate[0])
    os.close(gate[1])
    assert child.wait(timeout=5) == 1
    invalid = subprocess.run([str(binary), str(clip), '--control-fd', 'not-a-handle'],
                             cwd=home, capture_output=True, timeout=5)
    assert invalid.returncode == 2
print('PASS: fullscreen default, real desktop playback, preload gating, controller request, gate EOF and invalid handles')
