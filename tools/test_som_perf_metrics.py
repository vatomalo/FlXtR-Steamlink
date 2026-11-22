"""tools/som_perf_benchmark.py parsing and derived metrics (no device needed)."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import som_perf_benchmark as bench


def window(seq, fps, late=0, p99=20.0):
    phases = {name: {"n": 600, "mean_ms": 5.0, "p50_ms": 4.0, "p95_ms": 9.0,
                     "p99_ms": p99, "max_ms": p99 + 5, "total_ms": 3000.0}
              for name in ("interval", "run", "core", "video", "audio", "proxy")}
    return {"seq": seq, "frames": 600, "window_s": 600 / fps, "fps": fps,
            "nominal_fps": 60.099, "late": late, "late_streak_max": late and 2,
            "audio_frames": 320000, "audio_rate": 32040.0, "music_frames": 0,
            "music_short": 0, "phases": phases}


SAMPLE = """@begin
@stat
cpu  1000 0 500 100 0 0 0 0 0 0
ctxt 5000
processes 900
@procs
633 (retroarch.exec) R 1 2 3 4 5 6 7 8 9 10 400 100 0 0
1433 (tailscaled) S 1 2 3 4 5 6 7 8 9 10 50 10 0 0
@up
100.00 50.00
@tick 102.0 250
@perf {}
@tick 104.0 10
@perf {}
@perf {}
@end
@stat
cpu  1300 0 600 200 0 0 0 0 0 0
ctxt 7000
processes 920
@procs
633 (retroarch.exec) R 1 2 3 4 5 6 7 8 9 10 650 150 0 0
1433 (tailscaled) S 1 2 3 4 5 6 7 8 9 10 60 10 0 0
@up
105.00 50.00
""".format(json.dumps(window(1, 48.0, late=3)), json.dumps(window(2, 50.0, p99=31.0)),
           json.dumps(window(2, 50.0, p99=31.0)))


class TestBenchmark(unittest.TestCase):
    def test_parses_and_derives_without_inventing_values(self):
        begin, end, ticks, windows = bench.parse_output(SAMPLE)
        self.assertEqual([w["seq"] for w in windows], [1, 2])        # duplicates collapse
        d = bench.derive(begin, end, ticks, windows)
        self.assertAlmostEqual(d["seconds"], 5.0)
        self.assertAlmostEqual(d["cpu"]["idle"], 20.0)
        self.assertAlmostEqual(d["cpu"]["ctxt_per_s"], 400.0)
        self.assertAlmostEqual(d["tick_fps"], 16 / 2.0)              # 250 -> 10 wraps
        self.assertEqual(d["processes"][0], {"pid": 633, "name": "retroarch.exec",
                                             "cpu_pct": 60.0})
        self.assertEqual(d["proxy"]["late_frames"], 3)
        self.assertEqual(d["proxy"]["phases"]["interval"]["p99_ms_worst"], 31.0)
        self.assertEqual(d["proxy"]["fps_min_window"], 48.0)

    def test_old_proxy_without_timing_reports_not_measured(self):
        begin, end, ticks, _ = bench.parse_output(SAMPLE.replace("@perf", "@nope"))
        d = bench.derive(begin, end, ticks, [])
        self.assertIsNone(d["proxy"])
        text = bench.summary({"scenario": "A", "started_utc": "t", "host": "h", "git": "g",
                              "note": "", "runs": [{"derived": d}]})
        self.assertIn("Emulated FPS (proxy):    not measured", text)
        self.assertIn("Game FPS (WRAM tick):    8.00", text)

    def test_realtime_requires_every_run_and_window(self):
        nominal = 60.0
        proxy = {"nominal_fps": nominal, "fps_min_window": 59.5}
        report = {"runs": [{"derived": {"proxy": proxy}}]}
        self.assertTrue(bench.reaches_realtime(report))
        proxy["fps_min_window"] = 59.3
        self.assertFalse(bench.reaches_realtime(report))
        report["runs"][0]["derived"]["proxy"] = None
        self.assertFalse(bench.reaches_realtime(report))

    def test_compare_reports_change_against_the_first_scenario(self):
        with tempfile.TemporaryDirectory() as tmp:
            paths = []
            for name, fps in (("A", 50.0), ("D", 40.0)):
                d = {"tick_fps": fps, "proxy": None, "cpu": {"idle": 10.0}}
                path = Path(tmp) / f"{name}.json"
                path.write_text(json.dumps({"scenario": name, "runs": [{"derived": d}]}))
                paths.append(path)
            table = bench.compare(paths)
        self.assertIn("40.00 (-20%)", table)

    def test_sampler_is_valid_busybox_style_sh(self):
        self.assertEqual(subprocess.run(["sh", "-n", "-c", bench.SAMPLER]).returncode, 0)


if __name__ == "__main__":
    unittest.main()
