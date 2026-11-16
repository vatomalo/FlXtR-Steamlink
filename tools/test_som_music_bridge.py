"""Offline regression tests for the Secret of Mana genre-based music bridge."""
import tempfile
from pathlib import Path
import unittest

import som_music_bridge as bridge

from som_music_bridge import (MusicBridge, candidate_score, load_names,
                              parse_song, read_genre, search_query)


class SomMusicTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.catalog = load_names(Path(__file__).resolve().parents[1] /
                                 "data/som_song_names.tsv")

    def test_full_catalog(self):
        self.assertEqual(len(self.catalog), 64)
        self.assertEqual(self.catalog[0x0E], "Into the Thick of It")
        self.assertIn("Mysterious Moaning", self.catalog[0x2D])

    def test_existing_proxy_line(self):
        self.assertEqual(parse_song("[som-ai] SONG 0x2D Song 0x2D (map 341)"), 0x2D)
        self.assertIsNone(parse_song("Some unrelated address 0x2D"))

    def test_avoid_wrong_music_and_wrong_genre(self):
        good = ("video", "abcdefghijk", "Secret of Mana Into the Thick of It Lo-Fi Cover", "Composer")
        topic = ("video", "abcdefghijk", "Into the Thick of It (Lofi Mix)", "Pokerus Project - Topic")
        other = ("video", "abcdefghijk", "Secret of Mana Danger Lo-Fi Cover", "Composer")
        wrong_style = ("video", "abcdefghijk", "Secret of Mana Into the Thick of It Metal Cover", "Composer")
        wrong_game = ("video", "abcdefghijk", "Into the Thick of It Remix (chill/lofi)", "MaTTer")
        self.assertGreater(candidate_score(good, "Into the Thick of It", 0), 0)
        self.assertGreater(candidate_score(topic, "Into the Thick of It", 0), 0)
        self.assertLess(candidate_score(other, "Into the Thick of It", 0), 0)
        self.assertLess(candidate_score(wrong_style, "Into the Thick of It", 0), 0)
        self.assertLess(candidate_score(wrong_game, "Into the Thick of It", 0), 0)

    def test_ignore_jingles_retain_current_track(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            genre = directory / "genre"
            genre.write_text("0\n")
            bridge = MusicBridge(self.catalog, genre, directory / "cache",
                                 "greenlink-audio", dry_run=True)
            bridge.line("[som-ai] SONG 0x0E Into the Thick of It (map 123)")
            self.assertEqual(bridge.current, (0x0E, 0))
            bridge.line("[som-ai] SONG 0x2D Squaresoft Logo (map 341)")
            self.assertEqual(bridge.current, (0x0E, 0))
            genre.write_text("2\n")
            bridge.poll_genre()
            self.assertEqual(bridge.current, (0x0E, 2))

    def test_default_genre(self):
        with tempfile.TemporaryDirectory() as folder:
            file = Path(folder) / "setting"
            self.assertEqual(read_genre(file), 0)
            file.write_text("City Pop\n")
            self.assertEqual(read_genre(file), 2)
            file.write_text("99\n")
            self.assertEqual(read_genre(file), 0)

    def test_tail_stops_with_game_and_skips_other_games(self):
        import tempfile, pathlib
        with tempfile.TemporaryDirectory() as folder:
            log = pathlib.Path(folder) / "game.log"
            log.write_text("[som-ai] READY\n[som-ai] SONG 0x0E Song (map 1)\n")
            seen = []
            class Bridge:
                def line(self, text): seen.append(text)
                def poll_genre(self): pass
            calls = iter([True, False])
            self.assertEqual(bridge.tail(log, Bridge(), True, lambda: next(calls)), "exited")
            self.assertEqual(len(seen), 2)
            log.write_text("[INFO] some other core\n")
            ticks = iter([0.0, 0.0, 99.0])
            self.assertEqual(bridge.tail(log, Bridge(), True, None, 30, lambda: next(ticks)),
                             "not-som")

    def make_live_bridge(self, folder, player_body, search=None):
        """A bridge with a fake yt-dlp module and a real fake player process."""
        directory = Path(folder)
        genre = directory / "genre"
        genre.write_text("0\n")
        player = directory / "player.sh"
        player.write_text("#!/bin/sh\n" + player_body + "\n")
        player.chmod(0o755)
        live = MusicBridge(self.catalog, genre, directory / "cache", str(player))

        class FakeMusic:
            searches = []

            def search(self, query):
                FakeMusic.searches.append(query)
                if search:
                    return search(query)
                title = "Secret of Mana %s Lofi Cover" % query.split("Mana ", 1)[1]
                return [("video", "abcdefghij%d" % len(FakeMusic.searches), title, "Composer")]

            def resolve(self, kind, ident):
                return None
        live.music = FakeMusic()
        return live, genre, FakeMusic

    def test_player_lifecycle(self):
        with tempfile.TemporaryDirectory() as folder:
            live, genre, fake = self.make_live_bridge(folder, "exec sleep 30")
            try:
                live.line("[som-ai] SONG 0x0E x (map 1)")
                first = live.child
                self.assertIsNotNone(first)
                live.line("[som-ai] SONG 0x0E x (map 2)")         # same song: same player
                live.line("[som-ai] SONG 0x2D x (map 2)")         # jingle: keeps playing
                self.assertIs(live.child, first)
                self.assertEqual(len(fake.searches), 1)
                live.line("[som-ai] SONG 0x10 x (map 3)")         # new track replaces it
                self.assertIsNotNone(first.poll())
                second = live.child
                self.assertIsNot(second, first)
                genre.write_text("8\n")                            # Off
                live.poll_genre()
                self.assertIsNotNone(second.poll())
                self.assertIsNone(live.child)
                genre.write_text("0\n")
                live.poll_genre()
                live.line("[som-ai] SONG 0x0E x (map 1)")         # cached: no new search
                self.assertEqual(len(fake.searches), 2)
            finally:
                child = live.child
                live.stop()
                self.assertTrue(child is None or child.poll() is not None)   # no zombie

    def test_offline_search_does_not_stop_the_bridge(self):
        def offline(_query):
            raise Exception("DownloadError: unable to download webpage")
        with tempfile.TemporaryDirectory() as folder:
            live, _, _ = self.make_live_bridge(folder, "exec sleep 30", offline)
            live.line("[som-ai] SONG 0x0E x (map 1)")
            self.assertIsNone(live.child)
            live.line("[som-ai] SONG 0x10 x (map 1)")             # still running
            self.assertIsNone(live.child)

    def test_audio_device_refused_gives_up_after_two_quick_failures(self):
        with tempfile.TemporaryDirectory() as folder:
            live, _, fake = self.make_live_bridge(folder, "exit 1")
            live.line("[som-ai] SONG 0x0E x (map 1)")
            live.child.wait()
            live.poll_genre()                                     # 1st quick failure: retry
            self.assertFalse(live.gave_up)
            live.child.wait()
            live.poll_genre()                                     # 2nd: give up
            self.assertTrue(live.gave_up)
            self.assertIsNone(live.child)
            live.line("[som-ai] SONG 0x10 x (map 2)")
            self.assertIsNone(live.child)
            self.assertEqual(len(fake.searches), 1)

    def test_a_finished_track_restarts(self):
        with tempfile.TemporaryDirectory() as folder:
            live, _, _ = self.make_live_bridge(folder, "exit 0")
            live.line("[som-ai] SONG 0x0E x (map 1)")
            live.child.wait()
            live.started -= 60                                    # it played a while
            live.poll_genre()
            self.assertIsNotNone(live.child)
            self.assertEqual(live.quick_failures, 0)
            live.stop()

    def mix_bridge(self, folder, player_body):
        directory = Path(folder)
        (directory / "rate").write_text("8000\n")
        (directory / "genre").write_text("0\n")
        player = directory / "player.sh"
        player.write_text("#!/bin/sh\n" + player_body + "\n")
        player.chmod(0o755)
        live = MusicBridge(self.catalog, directory / "genre", directory / "cache", str(player),
                           mix_control=directory / "play", rate_file=directory / "rate")

        class FakeMusic:
            def search(self, query):
                title = "Secret of Mana %s Lofi Cover" % query.split("Mana ", 1)[1]
                return [("video", ("x%010d" % (abs(hash(query)) % 10**10))[:11], title, "C")]

            def resolve(self, kind, ident):
                return None
        live.music = FakeMusic()
        return live, directory

    def test_mix_mode_decodes_once_then_plays_from_the_cache(self):
        # The fake decoder writes 6 s of 8 kHz stereo into its output file.
        with tempfile.TemporaryDirectory() as folder:
            live, directory = self.mix_bridge(folder, 'head -c 192000 /dev/zero > "$3"')
            live.line("[som-ai] SONG 0x0E x (map 1)")
            self.assertIsNotNone(live.child)                     # decoding
            self.assertEqual(live.child.args[1:3], ["--pcm", "8000"])
            self.assertFalse((directory / "play").exists())       # nothing ready yet
            live.child.wait()
            live.poll_genre()
            played = Path((directory / "play").read_text().strip())
            self.assertTrue(played.is_file() and played.suffix == ".raw")
            self.assertFalse(list((directory / "cache/pcm").glob("*.part")))
            live.line("[som-ai] SONG 0x10 x (map 2)")            # next track decodes...
            self.assertIsNotNone(live.child)
            self.assertEqual(Path((directory / "play").read_text().strip()), played)  # ...old plays
            live.child.wait(); live.poll_genre()
            second = Path((directory / "play").read_text().strip())
            self.assertNotEqual(second, played)
            live.line("[som-ai] SONG 0x0E x (map 1)")            # cached: instant, no decoder
            self.assertIsNone(live.child)
            self.assertEqual(Path((directory / "play").read_text().strip()), played)
            (directory / "genre").write_text("8\n")              # Off
            live.poll_genre()
            self.assertFalse((directory / "play").exists())

    def test_mix_mode_failed_decode_leaves_nothing(self):
        with tempfile.TemporaryDirectory() as folder:
            live, directory = self.mix_bridge(folder, 'head -c 100 /dev/zero > "$3"; exit 1')
            live.line("[som-ai] SONG 0x0E x (map 1)")
            live.child.wait()
            live.started -= 60                                    # not a quick failure
            live.poll_genre()
            self.assertFalse((directory / "play").exists())
            self.assertFalse(list((directory / "cache/pcm").glob("*")))

    def test_query(self):
        self.assertEqual(search_query("Into the Thick of It", 0),
                         "Secret of Mana Into the Thick of It lofi chill remix")


if __name__ == "__main__":
    unittest.main()
