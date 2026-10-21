"""Summarize measured SDL queue/timing logs; does not infer physical A/V latency."""
import argparse
import pathlib
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('logs', nargs='+', type=pathlib.Path)
args = parser.parse_args()
for path in args.logs:
    text = path.read_text(errors='replace')
    obtained = re.search(r'obtained=(\d+) Hz.*period=(\d+) queue-cap=(\d+) bytes', text)
    records = []
    for line in text.splitlines():
        if '[steamlink-sdl] trace:' in line and 'elapsed_ms=' in line:
            records.append({key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', line)})
    print(f'{path.name}:')
    if not obtained or not records:
        print('  No measured gameplay timing window; no stability verdict.')
        continue
    rate, period, capacity = map(int, obtained.groups())
    first, last = records[0], records[-1]
    duration = (last['elapsed_ms'] - first['elapsed_ms']) / 1000
    byte_ms = rate * 4 / 1000
    print(f'  Output: {rate} Hz; period {period} frames ({period * 1000 / rate:.3f} ms); software cap {capacity / byte_ms:.3f} ms')
    print(f'  Logged window: {duration:.3f} s; {len(records)} interval reports')
    print(f'  Queue range at writes: {min(r["queue_min"] for r in records) / byte_ms:.3f}..{max(r["queue_max"] for r in records) / byte_ms:.3f} ms')
    print(f'  Maximum submission gap: {max(r["gap_max_us"] for r in records) / 1000:.3f} ms')
    print(f'  Maximum write duration: {max(r["write_max_us"] for r in records) / 1000:.3f} ms')
    print(f'  Possible empty episodes: {first["empty_episodes"]} at first report; {last["empty_episodes"]} at last report; +{last["empty_episodes"] - first["empty_episodes"]} within window')
    if duration > 0:
        print(f'  PCM submission rate over window: {(last["frames"] - first["frames"]) / duration:.2f} frames/s (pauses affect this average)')
    print('  TV audio delay, perceptual sync, input latency and gameplay quality require observation; no automatic pass.')
    for line in text.splitlines():
        if any(key in line.lower() for key in ('average monitor', 'average frame', 'average audio', 'audio buffer saturation')):
            print('  Frontend: ' + line.strip())
