import json
from pathlib import Path
import struct
import tempfile
import unittest

from som_compile_atlas import compile_atlas, compile_frames, indexed


def ppm(colours):
    return b"P6\n" + f"{len(colours)} 1\n255\n".encode() + b"".join(bytes.fromhex(c) for c in colours)


def sample():
    return {
        "asset_id": "test", "version": 1, "archetype": "popoi",
        "sources": {
            "body": {"path": "body.ppm", "palette": {"abcdef": 7}},
            "head": {"path": "head.ppm", "palette": {"fedcba": 3},
                     "compatible": ["randi", "primm", "popoi"]},
        },
        "required": ["walk/left"],
        "frames": [{
            "action": "walk", "facing": "left", "duration": 4, "pose": 480,
            "sockets": {"head": [10, 5]},
            "tiles": [{
                "tile": 496, "origin": [8, 4], "layers": [
                    {"source": "body", "rect": [0, 0, 4, 1], "offset": [0, 1]},
                    {"source": "head", "rect": [0, 0, 2, 1], "socket": "head"},
                ],
            }],
        }],
    }


class SpriteAtlasTest(unittest.TestCase):
    def test_chroma_and_palette(self):
        data = ppm(["ff00ff", "ff00fe", "00ff00"])
        _, _, pixels = indexed(data, {(255, 0, 254): 2, (0, 255, 0): 9}, (255, 0, 255))
        self.assertEqual(pixels, bytes((0, 2, 9)))

    def test_hybrid_socket_and_native_mapping(self):
        images = {"body": ppm(["abcdef"]*4), "head": ppm(["fedcba", "ff00ff"])}
        data = compile_frames(sample(), images)
        self.assertEqual(struct.unpack("<4sBBHHH", data[:12]), (b"SAT1", 2, 0, 1, 480, 496))
        self.assertEqual(data[20:24], bytes((7, 7, 3, 7)))
        manifest = sample()
        manifest["required"].append("attack/up")
        with self.assertRaisesRegex(ValueError, "missing animation coverage"):
            compile_frames(manifest, images)
        manifest = sample()
        manifest["sources"]["head"]["compatible"] = ["randi"]
        with self.assertRaisesRegex(ValueError, "incompatible"):
            compile_frames(manifest, images)

    def test_content_addressed_cache(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            manifest = directory / "manifest.json"
            manifest.write_text(json.dumps(sample()))
            (directory / "body.ppm").write_bytes(ppm(["abcdef"]*4))
            head = directory / "head.ppm"
            head.write_bytes(ppm(["fedcba", "ff00ff"]))
            first = compile_atlas(manifest, directory / "cache")
            before = first.stat().st_mtime_ns
            self.assertEqual(first, compile_atlas(manifest, directory / "cache"))
            self.assertEqual(first.stat().st_mtime_ns, before)
            head.write_bytes(ppm(["ff00ff", "fedcba"]))
            self.assertNotEqual(first, compile_atlas(manifest, directory / "cache"))


if __name__ == "__main__":
    unittest.main()
