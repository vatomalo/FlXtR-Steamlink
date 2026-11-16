#!/usr/bin/env python3
"""Measure Secret of Mana performance on the Steam Link, reproducibly.

One run = warm-up, then SECONDS of sampling on the device while the game runs:
  * the proxy's frame timing windows (src/som_perf.h; this tool creates
    /tmp/flxtr-som-perf-enable for the run and removes it again if it made it),
  * /proc/stat (busy/idle/iowait, context switches, forks) and per-process
    CPU from /proc/<pid>/stat at start and end,
  * the game's own frame tick (WRAM 7E:00F4, one byte, sampled every 2 s from
    the proxy's snapshot), which works even with an older proxy.
Nothing is changed on the device except that marker. `--ai off` stops the PC's
flxtr-som-ai service for the run and starts it again afterwards.

Scenarios (docs/SOM-ARM-PERFORMANCE-RESULTS.md) are labels: set the game up
(music on/off, AI on/off, the place to stand or fight) and name it, e.g.

    python3 tools/som_perf_benchmark.py --scenario D --runs 3 --seconds 120
    python3 tools/som_perf_benchmark.py --compare artifacts/som-perf/A-*.json \
        artifacts/som-perf/D-*.json

Raw results: artifacts/som-perf/<scenario>-<UTC time>.json, plus a .txt summary.
No numbers are invented: a value that was not measured is reported as null.
"""
import argparse
import datetime
import json
import shlex
import statistics
import subprocess
import sys
import time
from pathlib import Path

HOST = "root@100.124.74.13"
SSH = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8"]
PERF_FILE = "/tmp/flxtr-som-perf.json"
PERF_MARKER = "/tmp/flxtr-som-perf-enable"
HZ = 100                                    # Steam Link kernel USER_HZ

# Runs on the device under busybox ash; forks about two processes every 2 s.
SAMPLER = r"""
n=$1; marker=$2; made=0
if [ "$marker" = 1 ] && [ ! -e %(marker)s ]; then : > %(marker)s; made=1; fi
snap(){ echo "@stat"; head -1 /proc/stat; grep -E '^(ctxt|processes) ' /proc/stat
        echo "@procs"; for p in /proc/[0-9]*; do read -r l < $p/stat 2>/dev/null && echo "$l"; done
        echo "@up"; cat /proc/uptime; }
tick(){ dd if=/tmp/flxtr-som-wram.bin bs=1 skip=244 count=1 2>/dev/null | od -An -tu1; }
echo "@begin"; snap
last=; i=0
while [ $i -lt $n ]; do
  sleep 2; i=$((i+2))
  read -r up _ < /proc/uptime; echo "@tick $up $(tick)"
  if [ -f %(perf)s ]; then read -r line < %(perf)s; echo "@perf $line"; fi
done
echo "@end"; snap
[ $made = 0 ] || rm -f %(marker)s
""" % {"marker": PERF_MARKER, "perf": PERF_FILE}


def ssh(host, command, timeout, stdin=None):
    return subprocess.run([*SSH, host, command], input=stdin, capture_output=True,
                          text=True, timeout=timeout)


def parse_snapshot(lines):
    snap = {"cpu": None, "ctxt": None, "processes": None, "procs": {}, "uptime": None}
    section = None
    for line in lines:
        if line.startswith("@"):
            section = line[1:]
            continue
        if section == "stat":
            parts = line.split()
            if parts[0] == "cpu":
                snap["cpu"] = [int(v) for v in parts[1:]]
            elif parts[0] in ("ctxt", "processes"):
                snap[parts[0]] = int(parts[1])
        elif section == "procs":
            head, _, rest = line.partition(" (")
            name, _, tail = rest.rpartition(") ")
            fields = tail.split()
            if head.isdigit() and len(fields) > 13:
                snap["procs"][int(head)] = (name, int(fields[11]) + int(fields[12]))
        elif section == "up":
            snap["uptime"] = float(line.split()[0])
    return snap


def parse_output(text):
    """Split the sampler output into begin/end snapshots, ticks and perf windows."""
    begin, end, ticks, windows, current = [], [], [], {}, None
    for line in text.splitlines():
        if line == "@begin":
            current = begin
            continue
        if line == "@end":
            current = end
            continue
        if line.startswith("@tick "):
            parts = line.split()
            if len(parts) == 3 and parts[2].isdigit():
                ticks.append((float(parts[1]), int(parts[2])))
            continue
        if line.startswith("@perf "):
            try:
                window = json.loads(line[6:])
                windows[window["seq"]] = window
            except (ValueError, KeyError):
                pass
            continue
        if current is not None:
            current.append(line)
    return parse_snapshot(begin), parse_snapshot(end), ticks, [windows[k] for k in sorted(windows)]


def tick_rate(ticks):
    """Game frames per second from the wrapping one-byte WRAM tick."""
    if len(ticks) < 2:
        return None
    frames = sum((b[1] - a[1]) % 256 for a, b in zip(ticks, ticks[1:]))
    seconds = ticks[-1][0] - ticks[0][0]
    return frames / seconds if seconds > 0 else None


def derive(begin, end, ticks, windows):
    result = {"seconds": None, "cpu": {}, "processes": [], "tick_fps": tick_rate(ticks),
              "proxy": None}
    if begin["cpu"] and end["cpu"]:
        delta = [b - a for a, b in zip(begin["cpu"], end["cpu"])]
        total = sum(delta) or 1
        names = ["user", "nice", "system", "idle", "iowait", "irq", "softirq"]
        result["cpu"] = {name: round(100.0 * delta[i] / total, 2)
                         for i, name in enumerate(names) if i < len(delta)}
        result["seconds"] = total / HZ
        if begin["ctxt"] is not None and end["ctxt"] is not None:
            result["cpu"]["ctxt_per_s"] = round((end["ctxt"] - begin["ctxt"]) * HZ / total, 1)
        if begin["processes"] is not None and end["processes"] is not None:
            result["cpu"]["forks_per_s"] = round(
                (end["processes"] - begin["processes"]) * HZ / total, 2)
        rows = []
        for pid, (name, after) in end["procs"].items():
            if pid in begin["procs"] and begin["procs"][pid][0] == name:
                share = 100.0 * (after - begin["procs"][pid][1]) / total
                if share >= 0.2:
                    rows.append({"pid": pid, "name": name, "cpu_pct": round(share, 2)})
        result["processes"] = sorted(rows, key=lambda r: -r["cpu_pct"])
    if windows:
        def series(key):
            return [w[key] for w in windows if isinstance(w.get(key), (int, float))]
        def phase(name, key):
            return [w["phases"][name][key] for w in windows if name in w.get("phases", {})]
        proxy = {"windows": len(windows),
                 "fps_mean": round(statistics.mean(series("fps")), 3),
                 "fps_min_window": round(min(series("fps")), 3),
                 "nominal_fps": windows[-1].get("nominal_fps"),
                 "late_frames": sum(series("late")),
                 "late_streak_max": max(series("late_streak_max")),
                 "audio_rate": round(statistics.mean(series("audio_rate")), 1),
                 "music_short_frames": sum(series("music_short")),
                 "phases": {}}
        for name in windows[-1].get("phases", {}):
            means, p95, p99, peak = (phase(name, "mean_ms"), phase(name, "p95_ms"),
                                     phase(name, "p99_ms"), phase(name, "max_ms"))
            proxy["phases"][name] = {
                "mean_ms": round(statistics.mean(means), 3),
                "p50_ms_median": round(statistics.median(phase(name, "p50_ms")), 3),
                "p95_ms_median": round(statistics.median(p95), 3),
                "p99_ms_worst": round(max(p99), 3),
                "max_ms": round(max(peak), 3)}
        result["proxy"] = proxy
    return result


def run_once(host, seconds, warmup, proxy_timing, log):
    if warmup:
        log(f"  warm-up {warmup} s")
        time.sleep(warmup)
    command = "sh -s %d %d" % (seconds, 1 if proxy_timing else 0)
    out = ssh(host, command, timeout=seconds + 60, stdin=SAMPLER)
    if out.returncode != 0 and "@end" not in out.stdout:
        raise RuntimeError(f"sampler failed: {out.stderr.strip()[:200]}")
    begin, end, ticks, windows = parse_output(out.stdout)
    return {"raw": {"ticks": ticks, "windows": windows,
                    "cpu_begin": begin["cpu"], "cpu_end": end["cpu"]},
            "derived": derive(begin, end, ticks, windows)}


def fmt(value, unit="", digits=2):
    return "not measured" if value is None else f"{value:.{digits}f}{unit}"


def summary(report):
    lines = [f"SoM ARM Performance Report - scenario {report['scenario']}",
             f"{report['started_utc']}  host {report['host']}  git {report['git']}",
             f"note: {report['note'] or '-'}", ""]
    for i, run in enumerate(report["runs"], 1):
        d = run["derived"]
        p = d["proxy"] or {}
        ph = p.get("phases", {})
        def phase(name, key):
            return ph.get(name, {}).get(key)
        lines += [f"Run {i} ({fmt(d['seconds'], ' s', 1)})",
                  f"  Target game FPS (core):  {fmt(p.get('nominal_fps'), '', 3)}",
                  f"  Game FPS (WRAM tick):    {fmt(d['tick_fps'])}",
                  f"  Emulated FPS (proxy):    {fmt(p.get('fps_mean'))} "
                  f"(worst 10 s window {fmt(p.get('fps_min_window'))})",
                  f"  Frame interval P50/P95/P99: {fmt(phase('interval', 'p50_ms_median'))} / "
                  f"{fmt(phase('interval', 'p95_ms_median'))} / "
                  f"{fmt(phase('interval', 'p99_ms_worst'))} ms",
                  f"  retro_run mean / P99:    {fmt(phase('run', 'mean_ms'))} / "
                  f"{fmt(phase('run', 'p99_ms_worst'))} ms",
                  f"  core / video / audio mean: {fmt(phase('core', 'mean_ms'))} / "
                  f"{fmt(phase('video', 'mean_ms'))} / {fmt(phase('audio', 'mean_ms'))} ms",
                  f"  Audio callback P99:      {fmt(phase('audio', 'p99_ms_worst'))} ms",
                  f"  Proxy overhead mean:     {fmt(phase('proxy', 'mean_ms'), ' ms', 3)}",
                  f"  Late frames / streak:    {p.get('late_frames', 'not measured')} / "
                  f"{p.get('late_streak_max', 'not measured')}",
                  f"  Music ring shortfall:    {p.get('music_short_frames', 'not measured')} frames",
                  f"  CPU busy / idle:         "
                  f"{fmt(100 - d['cpu'].get('idle', 100) if d['cpu'] else None, ' %', 1)} / "
                  f"{fmt(d['cpu'].get('idle'), ' %', 1)}",
                  f"  Context switches / forks: {d['cpu'].get('ctxt_per_s', 'n/a')} / "
                  f"{d['cpu'].get('forks_per_s', 'n/a')} per s",
                  "  Top processes: " + ", ".join(
                      f"{r['name']} {r['cpu_pct']:.1f}%" for r in d["processes"][:6]), ""]
    return "\n".join(lines)


def compare(paths):
    reports = [json.loads(Path(p).read_text()) for p in paths]
    def mean_of(report, getter):
        values = [v for v in (getter(r["derived"]) for r in report["runs"]) if v is not None]
        return statistics.mean(values) if values else None
    metrics = [
        ("game FPS (tick)", lambda d: d["tick_fps"]),
        ("emulated FPS (proxy)", lambda d: (d["proxy"] or {}).get("fps_mean")),
        ("interval P99 ms", lambda d: ((d["proxy"] or {}).get("phases", {})
                                       .get("interval", {}).get("p99_ms_worst"))),
        ("late frames", lambda d: (d["proxy"] or {}).get("late_frames")),
        ("CPU idle %", lambda d: d["cpu"].get("idle") if d["cpu"] else None),
        ("forks per s", lambda d: d["cpu"].get("forks_per_s") if d["cpu"] else None),
    ]
    base = reports[0]
    header = "metric".ljust(24) + "".join(r["scenario"].rjust(14) for r in reports)
    rows = [header]
    for name, getter in metrics:
        b = mean_of(base, getter)
        cells = []
        for r in reports:
            v = mean_of(r, getter)
            if v is None:
                cells.append("n/a".rjust(14))
            elif r is base or not b:
                cells.append(f"{v:.2f}".rjust(14))
            else:
                cells.append(f"{v:.2f} ({100 * (v - b) / b:+.0f}%)".rjust(14))
        rows.append(name.ljust(24) + "".join(cells))
    return "\n".join(rows)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--scenario", default="adhoc")
    parser.add_argument("--note", default="")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--warmup", type=int, default=20)
    parser.add_argument("--no-proxy-timing", action="store_true")
    parser.add_argument("--ai", choices=("keep", "off"), default="keep",
                        help="off: stop the PC flxtr-som-ai service during the runs")
    parser.add_argument("--out", type=Path, default=Path("artifacts/som-perf"))
    parser.add_argument("--compare", nargs="+", type=Path)
    args = parser.parse_args(argv)
    if args.compare:
        print(compare(args.compare))
        return 0
    if not 10 <= args.seconds <= 900 or not 1 <= args.runs <= 10:
        parser.error("use 10..900 seconds and 1..10 runs")
    git = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True,
                         text=True).stdout.strip() or "unknown"
    started = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    report = {"scenario": args.scenario, "note": args.note, "host": args.host, "git": git,
              "started_utc": started, "seconds": args.seconds, "warmup": args.warmup,
              "ai": args.ai, "runs": []}
    log = lambda text: print(text, file=sys.stderr, flush=True)
    stopped = False
    try:
        if args.ai == "off":
            subprocess.run(["systemctl", "--user", "stop", "flxtr-som-ai"], check=True)
            stopped = True
        for i in range(args.runs):
            log(f"{args.scenario}: run {i + 1}/{args.runs}, {args.seconds} s")
            report["runs"].append(run_once(args.host, args.seconds, args.warmup,
                                           not args.no_proxy_timing, log))
    finally:
        if stopped:
            subprocess.run(["systemctl", "--user", "start", "flxtr-som-ai"])
    args.out.mkdir(parents=True, exist_ok=True)
    stem = args.out / f"{args.scenario}-{started}"
    stem.with_suffix(".json").write_text(json.dumps(report, indent=1) + "\n")
    text = summary(report)
    stem.with_suffix(".txt").write_text(text + "\n")
    print(text)
    print(f"raw: {stem.with_suffix('.json')}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
