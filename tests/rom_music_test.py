import importlib.util
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest

HELPER = Path('tools/rom_music.py').resolve()
INITIAL = '''import ctypes,time,signal
ctypes.CDLL(None).prctl(15,b'greenlink-audio',0,0,0)
time.sleep(30)
'''

class RomMusicTest(unittest.TestCase):
    def test_handoff_queue_and_exit_cleanup(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            (root/'scripts').mkdir()
            (root/'scripts/music-runtime.sh').write_text('#!/bin/sh\necho resolve >> trace\nexit 0\n')
            audio=root/'greenlink-audio'
            audio.write_text('#!/bin/sh\necho $$ > audio-pid\necho audio >> trace\nexec sleep 30\n')
            audio.chmod(0o755)
            game=subprocess.Popen(['sleep','30'])
            initial=subprocess.Popen([sys.executable,'-c',INITIAL])
            helper=None
            try:
                for _ in range(100):
                    if Path(f'/proc/{initial.pid}/comm').read_text().strip()=='greenlink-audio':break
                    time.sleep(.01)
                (root/'game-music-request').write_text(f'{initial.pid}\t1\nvideo\tfixture\n')
                helper=subprocess.Popen([sys.executable,str(HELPER),str(game.pid)],cwd=root)
                for _ in range(100):
                    if not (root/'game-music-request').exists():break
                    time.sleep(.02)
                self.assertIsNone(initial.poll(), 'Handoff must keep current song running')
                initial.terminate();initial.wait()
                for _ in range(100):
                    if (root/'audio-pid').exists():break
                    time.sleep(.02)
                self.assertEqual((root/'trace').read_text(),'resolve\naudio\n')
                child=int((root/'audio-pid').read_text())
                game.terminate();game.wait()
                self.assertEqual(helper.wait(timeout=6),0)
                self.assertFalse(Path(f'/proc/{child}').exists(), 'No background player may leak after exit')
            finally:
                for proc in (helper,initial,game):
                    if proc and proc.poll() is None:proc.kill();proc.wait()

    def test_early_game_exit_stops_initial_music(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);game=subprocess.Popen(['sleep','30']);initial=subprocess.Popen([sys.executable,'-c',INITIAL]);helper=None
            try:
                for _ in range(100):
                    if Path(f'/proc/{initial.pid}/comm').read_text().strip()=='greenlink-audio':break
                    time.sleep(.01)
                (root/'game-music-request').write_text(f'{initial.pid}\t0\n')
                helper=subprocess.Popen([sys.executable,str(HELPER),str(game.pid)],cwd=root)
                for _ in range(100):
                    if not (root/'game-music-request').exists():break
                    time.sleep(.02)
                helper.terminate()
                self.assertEqual(helper.wait(timeout=6),0)
                self.assertEqual(initial.wait(timeout=3),-signal.SIGTERM)
                self.assertIsNone(game.poll(), 'Music supervisor must never stop the game')
            finally:
                for proc in (helper,initial,game):
                    if proc and proc.poll() is None:proc.kill();proc.wait()

if __name__=='__main__':unittest.main()
