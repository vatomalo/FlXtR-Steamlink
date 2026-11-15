"""ROM-independent tests of the headless harness (tools/som_headless).

The real-ROM scenarios run only when SOM_HEADLESS_HOST, SOM_HEADLESS_ROM and
SOM_HEADLESS_START point at a user-supplied setup; otherwise they are
skipped with the reason printed.
"""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("som_headless_run", ROOT / "tools/som_headless/run.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
SCENARIOS = sorted((ROOT / "tools/som_headless/scenarios").glob("*.txt"))
COMMANDS = re.compile(r"^(frames \d+|hold [A-Z0-9,]+ \d+|cmd \S.*|shot \S+|peek|oam|save \S+|"
                      r"unload|mark|expect-moved \d+ \d+|expect u(8|16) [0-9A-Fa-f]+ "
                      r"(==|!=|<|<=|>|>=) (0x[0-9A-Fa-f]+|\d+)|monster [0-2] -?\d+ -?\d+)$")


class RewriteTests(unittest.TestCase):
    def test_every_live_path_moves_into_the_run_folder(self):
        source = "\n".join(p.read_text(errors="replace") for p in (ROOT / "src").rglob("*.[ch]"))
        fixed = runner.rewrite_paths(source, "/tmp/som-headless-1")
        for prefix in runner.LIVE_PREFIXES:
            self.assertIn(prefix, source)
            self.assertNotIn(prefix, fixed, prefix)
        self.assertIn("/tmp/som-headless-1/som_ai_disciplines", fixed)
        self.assertIn("/tmp/som-headless-1/flxtr-som-ai-action", fixed)
        # The SNES core it loads stays the installed one (read-only).
        self.assertIn("/home/apps/retroarch/cores/experimental/snes9x2005_plus_flxtr", fixed)


class LogCheckTests(unittest.TestCase):
    def test_expect_log_lines(self):
        checks = runner.scenario_log_checks("#! expect-log EVADE\n#! expect-no-log XP \nframes 1\n")
        self.assertEqual(runner.check_log("[som-ai] EVADE x\n", checks), [])
        self.assertEqual(len(runner.check_log("[som-ai] XP Magic +1\n", checks)), 2)


class PngTests(unittest.TestCase):
    def test_ppm_converts_to_a_valid_png(self):
        with tempfile.TemporaryDirectory() as folder:
            ppm, png = Path(folder, "a.ppm"), Path(folder, "a.png")
            ppm.write_bytes(b"P6\n2 1\n255\n" + bytes([255, 0, 0, 0, 0, 255]))
            runner.ppm_to_png(ppm, png, scale=1)
            data = png.read_bytes()
            self.assertTrue(data.startswith(b"\x89PNG"))
            idat = data[data.index(b"IDAT") + 4:data.index(b"IEND") - 8]
            self.assertEqual(zlib.decompress(idat), b"\0" + bytes([255, 0, 0, 0, 0, 255]))
            ppm.write_bytes(b"P5\n1 1\n255\n\0")
            with self.assertRaises(ValueError):
                runner.ppm_to_png(ppm, png)


class ScenarioTests(unittest.TestCase):
    def test_scenarios_use_only_known_commands(self):
        self.assertTrue(SCENARIOS)
        for path in SCENARIOS:
            for number, line in enumerate(path.read_text().splitlines(), 1):
                if line.strip() and not line.startswith("#"):
                    self.assertRegex(line, COMMANDS, "%s:%d" % (path.name, number))


@unittest.skipUnless(subprocess.run(["sh", "-c", "command -v cc"], capture_output=True).returncode == 0,
                     "no C compiler")
class HarnessBinaryTests(unittest.TestCase):
    def test_builds_and_rejects_bad_input(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder, "som-harness")
            subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-o", str(binary),
                            str(ROOT / "tools/som_headless/som_harness.c"), "-ldl"], check=True)
            usage = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(usage.returncode, 2)
            self.assertIn("usage", usage.stderr)
            missing = subprocess.run([str(binary), "/nonexistent/core.so", "rom", "-", "s", folder],
                                     capture_output=True, text=True)
            self.assertEqual(missing.returncode, 2)
            self.assertIn("FAIL dlopen", missing.stderr)


REAL = [os.environ.get(k) for k in ("SOM_HEADLESS_HOST", "SOM_HEADLESS_ROM", "SOM_HEADLESS_START")]


@unittest.skipUnless(all(REAL), "real-ROM scenarios need SOM_HEADLESS_HOST, SOM_HEADLESS_ROM and "
                                "SOM_HEADLESS_START (user-supplied ROM and a disposable save copy)")
class RealRomScenarios(unittest.TestCase):
    def test_all_scenarios(self):
        host, rom, start = REAL
        for path in SCENARIOS:
            with self.subTest(path.name):
                self.assertEqual(runner.main([str(path), "--host", host, "--rom", rom,
                                              "--start", start]), 0)


if __name__ == "__main__":
    unittest.main()
