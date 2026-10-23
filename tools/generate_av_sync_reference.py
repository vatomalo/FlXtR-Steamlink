#!/usr/bin/env python3
"""Generate and verify flash/click content; this does not calibrate a TV."""
import argparse
import array
from pathlib import Path
import subprocess


def generate(output):
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        'ffmpeg', '-v', 'error', '-nostdin', '-n',
        '-f', 'lavfi', '-i',
        "color=c=black:s=1280x720:r=60:d=12,drawbox=color=white:t=fill:enable='gte(n,120)*lt(mod(n,120),3)'",
        '-f', 'lavfi', '-i',
        r'aevalsrc=if(gte(n\,96000)*lt(mod(n\,96000)\,480)\,0.5*sin(2*PI*1000*n/48000)\,0):s=48000:d=12',
        '-c:v', 'libx264', '-preset', 'fast', '-profile:v', 'main',
        '-level:v', '3.2', '-pix_fmt', 'yuv420p', '-g', '120', '-bf', '2',
        '-c:a', 'pcm_s16le', '-ac', '2', '-shortest', str(output),
    ], check=True)
    video = subprocess.run([
        'ffmpeg', '-v', 'error', '-i', str(output), '-map', '0:v:0',
        '-vf', 'scale=1:1', '-pix_fmt', 'gray', '-f', 'rawvideo', 'pipe:1',
    ], check=True, capture_output=True).stdout
    flashes = [i / 60 for i, x in enumerate(video)
               if x > 200 and (i == 0 or video[i - 1] <= 200)]
    raw = subprocess.run([
        'ffmpeg', '-v', 'error', '-i', str(output), '-map', '0:a:0',
        '-ac', '1', '-ar', '48000', '-f', 's16le', 'pipe:1',
    ], check=True, capture_output=True).stdout
    pcm = array.array('h')
    pcm.frombytes(raw)
    import sys
    if sys.byteorder != 'little':
        pcm.byteswap()
    clicks, previous = [], -48000
    for i, sample in enumerate(pcm):
        if abs(sample) > 1000:
            if i - previous > 24000:
                clicks.append(i / 48000)
            previous = i
    if flashes != [2, 4, 6, 8, 10] or len(clicks) != len(flashes):
        raise RuntimeError(f'Unexpected pulse sequence: {flashes=}, {clicks=}')
    difference = max(abs(a - b) for a, b in zip(flashes, clicks))
    if difference > 2 / 48000:
        raise RuntimeError(f'Content onset mismatch: {difference * 1000:.6f} ms')
    print(f'Verified {output}: maximum content onset difference {difference * 1000:.6f} ms')
    print('Physical display, speakers and recording-device timing remain unmeasured.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', nargs='?', default='build/av-sync-reference.mkv')
    generate(parser.parse_args().output)
