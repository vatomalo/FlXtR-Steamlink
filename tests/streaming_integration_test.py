"""Run on SkyLoft/Linux: exercise the real launcher with a fake stream process."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StreamingLauncherTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="flxtr stream ")
        self.addCleanup(self.temp.cleanup)
        self.app = Path(self.temp.name)
        (self.app / "moonlight").mkdir()
        shutil.copy(ROOT / "packaging/moonlight-run.sh", self.app)
        binary = self.app / "moonlight/moonlight"
        binary.write_text('''#!/usr/bin/env python3
import json, os, sys
print(json.dumps({"args": sys.argv[1:], "backend": os.getenv("ML_AUDIO"),
"device": os.getenv("FLXTR_AUDIO_DEVICE"), "driver": os.getenv("SDL_AUDIO_DRIVER")}))
''')
        binary.chmod(0o755)

    def run_launcher(self, audio="auto", profile=""):
        (self.app / "streaming-audio.txt").write_text(audio + "\n")
        (self.app / "streaming.cfg").write_text(profile)
        result = subprocess.run(["sh", str(self.app / "moonlight-run.sh"), "stream",
                                 "100.1.2.3", "Game's name; $(echo unsafe)"],
                                text=True, capture_output=True, check=True)
        return json.loads(result.stdout.splitlines()[-1])

    def test_hdmi_overrides_alsa_and_keeps_app_name_literal(self):
        result = self.run_launcher("hdmi")
        self.assertEqual(result["backend"], "slaudio")
        self.assertEqual(result["driver"], "marvell")
        self.assertEqual(result["args"][:3], ["stream", "100.1.2.3", "Game's name; $(echo unsafe)"])

    def test_named_output_is_preserved_exactly(self):
        device = "Headset's audio; $(touch NEVER)"
        result = self.run_launcher("device:" + device)
        self.assertEqual(result["backend"], "sdl")
        self.assertEqual(result["device"], device)
        self.assertFalse((self.app / "NEVER").exists())

    def test_hdmi_compatibility_uses_sdl_marvell(self):
        result = self.run_launcher("hdmi-sdl")
        self.assertEqual(result["backend"], "sdl")
        self.assertEqual(result["driver"], "marvell")

    def test_profile_changes_real_stream_arguments(self):
        result = self.run_launcher(profile="resolution=1080\nfps=30\nbitrate=20000\n")
        args = result["args"]
        for option, value in (("--resolution", "1920x1080"), ("--fps", "30"), ("--bitrate", "20000")):
            self.assertEqual(args[args.index(option) + 1], value)

    def test_invalid_profile_keeps_hardware_safe_defaults(self):
        result = self.run_launcher(profile="resolution=4k\nfps=240\nbitrate=999999\n")
        args = result["args"]
        self.assertEqual(args[args.index("--resolution") + 1], "1280x720")
        self.assertEqual(args[args.index("--fps") + 1], "60")
        self.assertEqual(args[args.index("--bitrate") + 1], "8000")

    def test_audio_test_uses_selected_device_without_streaming(self):
        shutil.copy(self.app / "moonlight/moonlight", self.app / "moonlight/flxtr-audio-test")
        (self.app / "streaming-audio.txt").write_text("hdmi\n")
        result = subprocess.run(["sh", str(self.app / "moonlight-run.sh"), "test"],
                                text=True, capture_output=True, check=True)
        data = json.loads(result.stdout)
        self.assertEqual(data["args"], ["hdmi"])
        self.assertEqual(data["driver"], "marvell")

    def test_named_preference_survives_settings_reload_and_reorder(self):
        harness = self.app / "preferences.c"
        harness.write_text('''#include <stdio.h>
#include <string.h>
#include "streaming_settings.h"
int main(void) {
    strcpy(stream_audio,"device:Headset B");stream_resolution=1080;
    stream_fps=30;stream_bitrate=20000;streaming_save();
    strcpy(stream_audio,"auto");streaming_load();
    if(strcmp(streaming_audio_label(),"Headset B") || stream_resolution!=1080 || stream_fps!=30 || stream_bitrate!=20000)return 1;
    streaming_audio_step(1);stream_audio_test=0;
    return strcmp(stream_audio,"auto") || stream_audio_test;
}
''')
        (self.app / "moonlight/audio-devices.txt").write_text("Headset A\nHeadset B\n")
        subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "src"),
                        str(harness), "-o", str(self.app / "preferences")], check=True)
        subprocess.run([str(self.app / "preferences")], cwd=self.app, check=True)

    def test_busybox_timeout_uses_legacy_t_option(self):
        shutil.copy(self.app / "moonlight/moonlight", self.app / "moonlight/flxtr-audio-test")
        (self.app / "streaming-audio.txt").write_text("hdmi\n")
        commands = self.app / "commands"
        commands.mkdir()
        timeout = commands / "timeout"
        timeout.write_text('''#!/bin/sh
if [ "$1" = --help ]; then echo 'Usage: timeout [-t SECS] [-s SIG] PROG ARGS'; exit 0; fi
[ "$1 $2 $3 $4" = '-t 8 -s KILL' ] || exit 90
shift 4
exec "$@"
''')
        timeout.chmod(0o755)
        result = subprocess.run(["sh", str(self.app / "moonlight-run.sh"), "test"], text=True,
                                capture_output=True, check=True,
                                env={**os.environ, "PATH": str(commands) + ":" + os.environ["PATH"]})
        self.assertEqual(json.loads(result.stdout)["args"], ["hdmi"])


if __name__ == "__main__":
    unittest.main()
