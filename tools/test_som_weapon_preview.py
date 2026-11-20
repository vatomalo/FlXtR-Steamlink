"""Preview-sheet, donor-PNG and pack validation without distributing either ROM."""
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

import som_donor_dagger as donor
import som_weapon_preview as preview


def _chunk(kind,payload):
    val=kind+payload
    return struct.pack(">I",len(payload))+val+struct.pack(">I",zlib.crc32(val)&0xffffffff)


def _png_rgba():
    # A 6x6 opaque dagger diagonal surrounded by transparent pixels.
    width=height=8
    rows=bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            opaque=(x==2 and y>=2 and y<=6) or (y==5 and 1<=x<=3)
            rows.extend((220,220,200,255) if opaque else (0,0,0,0))
    return (b"\x89PNG\r\n\x1a\n"+
            _chunk(b"IHDR",struct.pack(">IIBBBBB",width,height,8,6,0,0,0))+
            _chunk(b"IDAT",zlib.compress(rows))+_chunk(b"IEND",b""))


def fake_pack(kind=0xD4,row=14,frame=10):
    header=b"SOMWORK1"+struct.pack("<H",1)
    colours=(0x001f,)*10+(0,)*10
    record=struct.pack("<4B20H",kind,row,frame,1,*colours)
    # ticks, dx, dy, width, height, then opaque and translucent pixels
    pixels=struct.pack("<BbbBB",1,-5,-12,2,1)+bytes((1,6))
    return header+record+pixels


class WeaponPreviewTests(unittest.TestCase):
    def test_valid_pack_and_alpha_channels(self):
        records=preview.unpack(fake_pack())
        self.assertIn((0xD4,14,10),records)
        self.assertEqual(records[(0xD4,14,10)][:5],(-5,-12,2,1,bytes((1,6))))
        self.assertEqual(preview.rgb555(0x001f),(255,0,0))
        self.assertEqual(preview.blend((0,0,0),(240,120,60),64),(60,30,15))

    def test_bad_pack_is_rejected_before_unsafe_preview(self):
        a=fake_pack()
        for data in (a[:-1],a+b"garbage",a[:10]+a[10:54]+bytes((1,0,0,1,1,255)),
                     a.replace(b"SOMWORK1",b"BADWORK1")):
            with self.assertRaises(ValueError):preview.unpack(data)

    def test_duplicate_frames_are_rejected(self):
        a=fake_pack()
        with self.assertRaisesRegex(ValueError,"duplicate"):
            preview.unpack(a[:8]+struct.pack("<H",2)+a[10:]+a[10:])

    def test_preview_png_has_correct_dimensions(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/"preview.png"
            preview.png(path,[[(1,2,3),(4,5,6)],[(7,8,9),(10,11,12)]])
            data=path.read_bytes()
            self.assertTrue(data.startswith(b"\x89PNG\r\n\x1a\n"))
            self.assertEqual(struct.unpack_from(">II",data,16),(2,2))

    def test_import_dagger_png_and_anchor(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/"dagger.png"
            path.write_bytes(_png_rgba())
            png=donor.import_dagger(path,(2,6),(2,2))
            self.assertEqual(png["hilt"],(2,6))
            self.assertEqual(png["tip"],(2,2))
            self.assertEqual(len(png["palette"]),5)
            self.assertEqual(png["pixels"][0][0],0)
            self.assertTrue(png["pixels"][6][2])
            # The source dagger has no opaque pixels outside its cutout.
            canvas=[[0]*48 for _ in range(48)]
            donor.place_dagger(canvas,png,(20,30),(20,10))
            self.assertTrue(any(x for row in canvas for x in row))
            with self.assertRaises(ValueError):
                donor.import_dagger(path,(0,0),(2,2))


if __name__=="__main__":
    unittest.main()
