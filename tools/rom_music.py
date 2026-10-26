#!/usr/bin/env python3
"""Supervise opt-in music during one ROM session, retaining game sound effects."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

stopped = False


def stop(_signal, _frame):
    global stopped
    stopped = True


def process_identity(pid):
    try:
        stat = Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()
        if stat[0] == 'Z':
            return None
        return stat[19]  # starttime (field 22), distinguishes reused PIDs
    except (OSError, IndexError):
        return None


def run_child(args, game_pid, game_stamp):
    child = subprocess.Popen(args, start_new_session=True)
    try:
        while child.poll() is None:
            if stopped or process_identity(game_pid) != game_stamp:
                return False
            time.sleep(.25)
        return child.returncode == 0
    finally:
        if child.poll() is None:
            os.killpg(child.pid, signal.SIGTERM)
            try:
                child.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait()


def run(game_pid):
    request = Path('game-music-request')
    lines = request.read_text().splitlines()
    pid_text, autoplay = lines[0].split('\t')
    initial = int(pid_text)
    if initial < 2 or Path(f'/proc/{initial}/comm').read_text().strip() != 'greenlink-audio':
        raise ValueError('Music worker is no longer running')
    initial_stamp = process_identity(initial)
    game_stamp = process_identity(game_pid)
    if not initial_stamp or not game_stamp:
        raise ValueError('Music or game session ended before handoff')
    queue = []
    for line in lines[1:100]:
        kind, key = line.split('\t', 1)
        if kind not in ('video', 'query') or not key or len(key) > 220:
            raise ValueError('Invalid queued music selection')
        queue.append((kind, key))
    request.unlink()
    try:
        print('Background music handed off; game audio unchanged', flush=True)
        while not stopped and process_identity(game_pid) == game_stamp:
            if process_identity(initial) == initial_stamp:
                time.sleep(.25)
                continue
            if autoplay != '1':
                break
            command = ['resolve', *queue.pop(0)] if queue else ['next']
            if not run_child(['/bin/sh', 'scripts/music-runtime.sh', 'tools/music.py', *command], game_pid, game_stamp):
                break
            if not run_child(['./greenlink-audio', 'music-cache/stream.txt'], game_pid, game_stamp):
                break
    finally:
        if process_identity(initial) == initial_stamp:
            os.kill(initial, signal.SIGTERM)
            for _ in range(12):
                if process_identity(initial) != initial_stamp:
                    break
                time.sleep(.25)
            if process_identity(initial) == initial_stamp:
                os.kill(initial, signal.SIGKILL)
        print('ROM music session finished', flush=True)


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    try:
        run(int(sys.argv[1]))
    except (OSError, ValueError, IndexError) as error:
        print('ROM music unavailable:', type(error).__name__, file=sys.stderr)
        sys.exit(1)
