"""Offline regression tests for the Secret of Mana genre-based music bridge."""
import tempfile
from pathlib import Path
import unittest

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

    def test_query(self):
        self.assertEqual(search_query("Into the Thick of It", 0),
                         "Secret of Mana Into the Thick of It lofi chill remix")


if __name__ == "__main__":
    unittest.main()
