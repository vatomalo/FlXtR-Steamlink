"""tools/som_wram_server.py: same replies as the shell loop, plus deltas."""
import gzip
import os
import random
import stat
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import som_qwen_bridge as bridge
from som_qwen_bridge import REMOTE_LOOP, RemoteLink, WRAM_BYTES
from som_wram_server import BANNER, Server

SERVER = Path(__file__).resolve().parent / "som_wram_server.py"


def shell_reply(loop, root, line):
    """What the old shell loop answers to one request."""
    return subprocess.run(["sh", "-c", loop], input=line.encode() + b"\n",
                          capture_output=True, cwd=root, timeout=10).stdout


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        rng = random.Random(7)
        self.image = bytearray(rng.randrange(256) if i % 5 else 0 for i in range(WRAM_BYTES))
        self.wram().write_bytes(self.image)
        fake = self.root / "fake-ssh"
        fake.write_text('#!/bin/sh\nfor last; do :; done\nexec sh -c "$last"\n')
        fake.chmod(fake.stat().st_mode | stat.S_IEXEC)
        self.fake = str(fake)
        self.loop = (REMOTE_LOOP.replace("/tmp/", str(self.root) + "/")
                     .replace(bridge.GAMBIT_FILE, str(self.root / "som_ai_gambits"))
                     .replace(bridge.COMBAT_FILE, str(self.root / "som_ai_combat"))
                     .replace(bridge.QUEST_FILE, str(self.root / "som_ai_quest"))
                     .replace(bridge.BRAIN_FILE, str(self.root / "som_ai_brain"))
                     .replace(bridge.DISCIPLINE_FILE, str(self.root / "som_ai_disciplines")))
        self.server_cmd = f"exec {sys.executable} {SERVER} --tmp {self.root} --config {self.root}"

    def tearDown(self):
        self.tmp.cleanup()

    def wram(self):
        return self.root / "flxtr-som-wram.bin"

    def link(self, server=None):
        return RemoteLink("host", timeout=5, remote_loop=self.loop, ssh_command=[self.fake],
                          server=self.server_cmd if server is None else server)


class TestServerMatchesShellLoop(Fixture):
    def test_plain_replies_are_byte_identical(self):
        server = Server(str(self.root), str(self.root))
        (self.root / "som_ai_gambits").write_text("504321\n")
        (self.root / "som_ai_combat").write_text("bogus\n")
        (self.root / "som_ai_brain").write_text("14 0\n")
        (self.root / "flxtr-som-familiar").write_text("2\n")
        for marker in (False, True):
            if marker:
                (self.root / "flxtr-som-ai-enabled").write_text("enabled\n")
            for line in ("obs", "obsm", "nav", "gmb", "gmc", "gmm", "fam", "dsc", "act up",
                         "act rm -rf", "act ", "msg Hello there!|Next", "msg $(reboot)",
                         "nonsense", "dltx"):
                with self.subTest(line=line, marker=marker):
                    self.assertEqual(server.handle(line), shell_reply(self.loop, self.root, line))
        self.assertEqual((self.root / "flxtr-som-ai-action").read_text(), "up\n")
        self.assertEqual(server.handle("gmm"), b"GMM 14 0\n")
        (self.root / "som_ai_brain").write_text("99 1\n")
        self.assertEqual(server.handle("gmm"), b"GMM 0 1\n")
        self.assertEqual(shell_reply(self.loop, self.root, "gmm"), b"GMM 0 1\n")
        for text, want in (("v1 1 5 29 4 131\n", b"DSC 1\n"), ("v1 255 1 0\n", b"DSC 255\n"),
                           ("bogus\n", b"DSC 255\n")):
            (self.root / "som_ai_disciplines").write_text(text)
            self.assertEqual(server.handle("dsc"), want)
            self.assertEqual(shell_reply(self.loop, self.root, "dsc"), want)

    def test_compressed_replies_inflate_to_the_same_bytes(self):
        server = Server(str(self.root), str(self.root))
        (self.root / "flxtr-som-ai-enabled").write_text("enabled\n")
        for line in ("obsz", "obsmz", "hotz", "hotmz"):
            ours, theirs = server.handle(line), shell_reply(self.loop, self.root, line)
            head, _, body = ours.partition(b"\n")
            self.assertEqual(head, theirs.partition(b"\n")[0])
            self.assertEqual(gzip.decompress(body), gzip.decompress(theirs.partition(b"\n")[2]))
        (self.root / "flxtr-som-hot.bin").write_bytes(b"\x01" * bridge.HOT_BYTES)
        self.assertEqual(gzip.decompress(server.handle("hotz")[6:]), b"\x01" * bridge.HOT_BYTES)

    def test_missing_snapshot_and_marker(self):
        server = Server(str(self.root), str(self.root))
        self.assertEqual(server.handle("dltm"), b"STOP\n")
        self.wram().unlink()
        for line in ("dlt", "obs", "obsz", "hotz"):
            self.assertEqual(server.handle(line), b"NONE\n")


class TestLivingProfile(Fixture):
    def test_living_profile_matches_its_restricted_loop(self):
        import som_living_world as lw
        loop = lw.REMOTE_LOOP.replace("/tmp/", str(self.root) + "/")
        server = Server(str(self.root), str(self.root), profile="living")
        (self.root / "flxtr-som-ai-enabled").write_text("enabled\n")
        for line in ("obs", "npc npc_walk 131 8 24 30", "npc npc_walk 1;reboot", "npc npc_walk ",
                     "npc other 1", "msg Good morning, Potos!", "msg ^caret", "act up",
                     "obsm", "nav", "gmb", "hotz", "dltm", "fam"):
            with self.subTest(line=line):
                self.assertEqual(server.handle(line), shell_reply(loop, self.root, line))
        self.assertEqual((self.root / "flxtr-som-npc-command").read_text(),
                         "npc_walk 131 8 24 30\n")
        self.assertFalse((self.root / "flxtr-som-ai-action").exists())
        ours, theirs = server.handle("obsz"), shell_reply(loop, self.root, "obsz")
        self.assertEqual(gzip.decompress(ours[6:]), gzip.decompress(theirs[6:]))
        self.assertTrue(server.handle("dlt").startswith(b"WRAMB "))

    def test_living_world_links_ask_for_the_living_profile(self):
        self.assertTrue(bridge.server_remote("living").count("--profile living"))
        self.assertNotIn("--profile", bridge.SERVER_REMOTE)


class TestDeltaLink(Fixture):
    def test_every_read_is_the_complete_current_image(self):
        link = self.link()
        try:
            self.assertEqual(link.fetch_wram(), bytes(self.image))
            self.assertTrue(link.serving)
            process, first = link.process, link.stats["bytes"]
            for step in range(12):
                self.image[0xE002] = step                    # hot area
                self.image[0x14010 + step] = 200 - step      # cold area: no stale base
                self.wram().write_bytes(self.image)
                self.assertEqual(link.fetch_wram(), bytes(self.image))
            self.assertIs(link.process, process)            # one session, no restarts
            self.assertLess((link.stats["bytes"] - first) / 12, 1000)  # vs ~7.9 KB per gzip hot read
        finally:
            link.close()

    def test_marker_actions_messages_and_settings(self):
        link = self.link()
        try:
            self.assertIsNone(link.fetch_wram(require_marker=True))
            (self.root / "flxtr-som-ai-enabled").write_text("enabled\n")
            self.assertEqual(link.fetch_wram(require_marker=True), bytes(self.image))
            link.send_action("up")
            self.assertEqual((self.root / "flxtr-som-ai-action").read_text(), "up\n")
            with self.assertRaises(ValueError):
                link.send_action("rm -rf /")
            link.show_message("^Hello there!|Second line")
            self.assertEqual((self.root / "flxtr-som-ai-message").read_text(),
                             "^Hello there!|Second line\n")
            self.assertEqual(link.fetch_gambits(), tuple(int(c) for c in bridge.GAMBIT_DEFAULTS))
            (self.root / "som_ai_combat").write_text("01234501\n")
            self.assertEqual(link.fetch_combat(), (0, 1, 2, 3, 4, 5, 0, 1))
            self.assertEqual(link.fetch_brain(), (0, True))
            (self.root / "som_ai_brain").write_text("9 0\n")
            self.assertEqual(link.fetch_brain(), (9, False))
            self.assertEqual(link.fetch_familiar(), 0)
            self.assertIsNone(link.fetch_discipline())
            (self.root / "som_ai_disciplines").write_text("v1 1 5 29\n")
            self.assertEqual(link.fetch_discipline(), 1)
            self.assertTrue(link.serving)
        finally:
            link.close()

    def test_restart_resynchronises_the_delta_base(self):
        link = self.link()
        try:
            self.assertEqual(link.fetch_wram(), bytes(self.image))
            link.process.kill()                              # session lost mid-game
            link.process.wait()
            self.image[0x100] ^= 0xFF
            self.wram().write_bytes(self.image)
            self.assertEqual(link.fetch_wram(), bytes(self.image))
            self.assertEqual(link.stats["sessions"], 2)
        finally:
            link.close()

    def test_missing_snapshot_raises_and_keeps_the_session(self):
        link = self.link()
        try:
            self.wram().unlink()
            with self.assertRaises(OSError) as raised:
                link.fetch_wram()
            self.assertIn("NONE", str(raised.exception))
            process = link.process
            self.wram().write_bytes(self.image)
            self.assertEqual(link.fetch_wram(), bytes(self.image))
            self.assertIs(link.process, process)
        finally:
            link.close()

    def test_device_without_the_server_uses_the_shell_loop(self):
        link = self.link(server="echo NOSERVER")
        try:
            self.assertEqual(link.fetch_wram(), bytes(self.image))
            self.assertFalse(link.serving)
            self.assertEqual(link.server, "")
            link.send_action("left")
            self.assertEqual((self.root / "flxtr-som-ai-action").read_text(), "left\n")
        finally:
            link.close()

    def test_a_server_that_never_starts_falls_back_after_two_tries(self):
        link = self.link(server="exit 3")
        try:
            self.assertEqual(link.fetch_wram(), bytes(self.image))
            self.assertEqual(link.server, "")
        finally:
            link.close()

    def test_custom_loops_and_the_override_keep_the_shell(self):
        self.assertEqual(RemoteLink("h", remote_loop=self.loop).server, "")
        self.assertEqual(RemoteLink("h").server, bridge.SERVER_REMOTE)
        os.environ["SOM_AI_SHELL_LOOP"] = "1"
        try:
            self.assertEqual(RemoteLink("h").server, "")
        finally:
            del os.environ["SOM_AI_SHELL_LOOP"]

    def test_banner_is_the_first_thing_the_server_says(self):
        out = subprocess.run([sys.executable, str(SERVER), "--tmp", str(self.root)],
                             input=b"", capture_output=True, timeout=10).stdout
        self.assertEqual(out, BANNER)


if __name__ == "__main__":
    unittest.main()
