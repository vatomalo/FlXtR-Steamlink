#!/usr/bin/env python3
"""Run a Secret of Mana Ultimate scenario headless on the real ROM.

Builds a private copy of the AI proxy whose writable paths point into a
throw-away run folder (so the player's settings, saves and the live game's
/tmp command files are never touched), runs tools/som_headless/som_harness.c
with a scenario script, collects the log and screenshots, checks
`#! expect-log REGEX` / `#! expect-no-log REGEX` lines, and removes the run
folder. Nothing is ever installed.

Remote (Steam Link, where the core and ROM live):
    python3 tools/som_headless/run.py --host root@100.124.74.13 \\
        --rom '/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.smc' \\
        --start '/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.srm' \\
        tools/som_headless/scenarios/title_start.txt
Local (needs a libretro SNES core built for this machine):
    FLXTR_AI_BASE_CORE=/path/snes9x2005_plus_libretro.so python3 tools/som_headless/run.py \\
        --rom ~/roms/'Secret of Mana, Turbo.smc' --start - scenario.txt

The ROM and start file are copied into the run folder (a disposable copy);
the originals are only read. Exit status: the harness's (0 pass, 1 an
expectation failed, 2 setup error), 3 timeout, 4 an expect-log failed.
"""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

REPO = Path(__file__).resolve().parents[2]
TURBO_NAME = "Secret of Mana, Turbo.smc"          # the proxy enables itself on this name
LIVE_PREFIXES = ("/home/apps/retroarch/cores/experimental/som_", "/tmp/flxtr-")
SDK_CANDIDATES = ("/opt/steamlink-sdk", "/opt/dockers/storage/toolchains/steamlink-sdk")
DEVICE_DATA = ("som_song_names.tsv", "som_ai_rom_patch.txt", "som_cartwheel_frames.bin",
               "som_ai_disciplines")


def rewrite_paths(text, run_dir):
    """Point every writable proxy path at run_dir (no trailing slash)."""
    text = text.replace(LIVE_PREFIXES[0], run_dir + "/som_")
    return text.replace(LIVE_PREFIXES[1], run_dir + "/flxtr-")


def scenario_log_checks(script_text):
    """[(want_present, regex)] from '#! expect-log' and '#! expect-no-log' lines."""
    checks = []
    for line in script_text.splitlines():
        match = re.match(r"#!\s*(expect-log|expect-no-log)\s+(.+)$", line.strip())
        if match:
            checks.append((match.group(1) == "expect-log", re.compile(match.group(2))))
    return checks


def check_log(log, checks):
    failures = []
    for want, pattern in checks:
        if bool(pattern.search(log)) != want:
            failures.append(("missing " if want else "unexpected ") + pattern.pattern)
    return failures


def ppm_to_png(source, target, scale=2):
    data = Path(source).read_bytes()
    magic, size, depth, pixels = data.split(b"\n", 3)
    if magic != b"P6" or depth != b"255":
        raise ValueError("not a binary 8-bit PPM")
    width, height = map(int, size.split())
    rows = []
    for y in range(height):
        row = pixels[y * width * 3:(y + 1) * width * 3]
        wide = b"".join(row[x * 3:x * 3 + 3] * scale for x in range(width))
        rows.extend([b"\0" + wide] * scale)

    def chunk(kind, body):
        return (struct.pack(">I", len(body)) + kind + body +
                struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))
    header = struct.pack(">IIBBBBB", width * scale, height * scale, 8, 2, 0, 0, 0)
    Path(target).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
                             chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) +
                             chunk(b"IEND", b""))


def compiler(remote):
    if not remote:
        return "cc", "cc"
    sdk = os.environ.get("STEAMLINK_SDK_PATH") or next(
        (c for c in SDK_CANDIDATES if Path(c, "setenv.sh").is_file()), None)
    if not sdk:
        raise SystemExit("Steam Link SDK not found: set STEAMLINK_SDK_PATH")
    prefix = "set +u; . %s/setenv.sh >/dev/null; " % shlex.quote(sdk)
    return prefix + '$CC', prefix + '$CC'


def build(work, run_dir, remote):
    src = work / "src"
    shutil.copytree(REPO / "src", src)
    for path in src.rglob("*"):
        if path.suffix in (".c", ".h"):
            text = path.read_text(errors="surrogateescape")
            fixed = rewrite_paths(text, run_dir)
            if fixed != text:
                path.write_text(fixed, errors="surrogateescape")
    (work / "tools" / "som_headless").mkdir(parents=True)
    shutil.copy(Path(__file__).with_name("som_harness.c"), work / "tools/som_headless/som_harness.c")
    cc_proxy, cc_harness = compiler(remote)
    commands = [
        cc_proxy + " -Os -std=c99 -Wall -Wextra -fPIC -shared src/som_ai_proxy.c"
                   " -o som_ai_proxy_test.so -ldl -lm",
        cc_harness + " -O1 -std=gnu99 -Wall -o som-harness tools/som_headless/som_harness.c -ldl",
    ]
    for command in commands:
        done = subprocess.run(["bash", "-c", command], cwd=work, capture_output=True, text=True)
        if done.returncode:
            sys.stderr.write(done.stderr)
            raise SystemExit("build failed: " + command.split(";")[-1].strip())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("scenario", type=Path, nargs="?")
    parser.add_argument("--train-boss", action="store_true", help="reload a boss save and train all three party members")
    parser.add_argument("--rom", required=True, help="ROM path (on --host when remote)")
    parser.add_argument("--start", default="-", help=".srm or save state to copy, or -")
    parser.add_argument("--host", help="ssh target; omit to run locally")
    parser.add_argument("--out", type=Path, help="where logs and screenshots go")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--keep", action="store_true", help="keep the run folder")
    args = parser.parse_args(argv)
    if args.train_boss:
        if args.scenario or args.start == "-" or args.start.endswith(".srm"):
            parser.error("--train-boss requires a savestate and no scenario file")
    elif args.scenario is None:
        parser.error("specify a scenario or --train-boss")
    script = "" if args.train_boss else args.scenario.read_text()
    name = "boss-training" if args.train_boss else args.scenario.stem
    out = args.out or REPO / "build" / "som-headless" / (name + time.strftime("-%Y%m%d-%H%M%S"))
    out.mkdir(parents=True, exist_ok=True)
    run_dir = "/tmp/som-headless-%d-%d" % (os.getpid(), int(time.time()))
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        build(work, run_dir, bool(args.host))
        (work / "scenario.txt").write_text(script)
        start = "-" if args.start == "-" else run_dir + "/start" + (".srm" if args.start.endswith(".srm") else ".state")
        rom = run_dir + "/" + TURBO_NAME
        setup = ["mkdir -p %s" % shlex.quote(run_dir),
                 "cp %s %s" % (shlex.quote(args.rom), shlex.quote(rom))]
        upload_start = bool(args.host) and start != "-" and Path(args.start).is_file()
        if start != "-" and not upload_start:     # a path on the run host
            setup.append("cp %s %s" % (shlex.quote(args.start), shlex.quote(start)))
        # SOM_HEADLESS_DATA: a folder on the run host with newer copies
        # (e.g. a freshly built frame pack) that win over the installed ones.
        local = os.environ.get("SOM_HEADLESS_DATA")
        for name in DEVICE_DATA:
            live = "/home/apps/retroarch/cores/experimental/" + name
            setup.append("[ ! -f %s ] || cp %s %s/" % (live, live, run_dir))
            if local:
                newer = shlex.quote(os.path.join(local, name))
                setup.append("[ ! -f %s ] || cp %s %s/" % (newer, newer, run_dir))
        base = os.environ.get("SOM_HEADLESS_BASE_CORE")   # a core already on the host
        run = ("cd %s && " % shlex.quote(run_dir)) + \
              ("FLXTR_AI_BASE_CORE=%s " % shlex.quote(base) if base else "") + \
              ("FLXTR_SOM_NO_DEPTH=1 " if os.environ.get("SOM_HEADLESS_NO_DEPTH") == "1" else "") + \
              ("FLXTR_SOM_AI_DIRECT=${SOM_HEADLESS_DIRECT:-1} FLXTR_SOM_AUTO_STATE=0 FLXTR_SOM_TEST_COMMANDS=1 ./som-harness "
               "./som_ai_proxy_test.so %s %s %s %s") % (
                   shlex.quote(rom), shlex.quote(start),
                   "--train-boss" if args.train_boss else "scenario.txt", shlex.quote(run_dir))
        cleanup = "rm -rf %s" % shlex.quote(run_dir)

        def shell(command, **kw):
            if args.host:
                return subprocess.run(["ssh", "-o", "BatchMode=yes", args.host, command], **kw)
            return subprocess.run(["sh", "-c", command], **kw)
        try:
            if shell(" && ".join(setup)).returncode:
                raise SystemExit("could not prepare the run folder (ROM/start path?)")
            if upload_start:                          # a local fixture, sent over
                with open(args.start, "rb") as data:
                    subprocess.run(["ssh", "-o", "BatchMode=yes", args.host,
                                    "cat > %s" % shlex.quote(start)], stdin=data, check=True)
            for name in ("som-harness", "som_ai_proxy_test.so", "scenario.txt"):
                if args.host:
                    with open(work / name, "rb") as data:
                        subprocess.run(["ssh", "-o", "BatchMode=yes", args.host,
                                        "cat > %s/%s && chmod 755 %s/%s" % (run_dir, name, run_dir, name)],
                                       stdin=data, check=True)
                else:
                    shutil.copy(work / name, Path(run_dir) / name)
            log_path = out / "run.log"
            with log_path.open("wb") as stream:
                try:
                    done = shell(run, stdout=stream, stderr=subprocess.STDOUT, timeout=args.timeout)
                    code = done.returncode
                except subprocess.TimeoutExpired:
                    code = 3
                    stream.write(f"\n[runner] TIMEOUT after {args.timeout} s\n".encode())
            log = log_path.read_text(errors="replace")
            saves = re.findall(r"\[harness\] save (\S+) \d+ bytes serialize 1", log)
            saves += re.findall(r"\[harness\] dump (\S+) \d+ bytes", log)
            for saved in saves:
                remote_path = saved if saved.startswith("/") else run_dir + "/" + saved
                with open(out / Path(saved).name, "wb") as sink:      # states stay outside Git
                    if args.host:
                        subprocess.run(["ssh", "-o", "BatchMode=yes", args.host,
                                        "cat %s" % shlex.quote(remote_path)], stdout=sink, check=True)
                    else:
                        sink.write(Path(remote_path).read_bytes())
            shots = [m.group(1) for m in re.finditer(r"\[harness\] shot (\S+\.ppm)", log)]
            for remote_path in shots:
                local = out / Path(remote_path).name
                if args.host:
                    with open(local, "wb") as sink:
                        subprocess.run(["ssh", "-o", "BatchMode=yes", args.host,
                                        "cat %s" % shlex.quote(remote_path)], stdout=sink, check=True)
                else:
                    shutil.copy(remote_path, local)
                ppm_to_png(local, local.with_suffix(".png"))
                local.unlink()
        finally:
            if not args.keep:
                shell(cleanup)
    failures = check_log(log, scenario_log_checks(script))
    for line in log.splitlines():
        if "FAIL" in line or "TIMEOUT" in line or line.startswith("[training]"):
            print(line)
    for failure in failures:
        print("[runner] FAIL log check:", failure)
    if code == 0 and failures:
        code = 4
    print("[runner] %s: exit %d, output in %s" % (name, code, out))
    return code


if __name__ == "__main__":
    sys.exit(main())
