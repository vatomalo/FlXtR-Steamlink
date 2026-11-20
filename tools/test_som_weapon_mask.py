"""ROM-independent tests for per-frame Bushido hard masks and alpha FX."""
import math
import struct
import unittest

import som_weapon_mask as masks


def scene(angle=0, fx=True):
    size = 96
    img = [[0]*size for _ in range(size)]
    body = [[0]*size for _ in range(size)]
    grip = (40, 45)
    for y in range(38, 51):
        for x in range(34, 41):
            body[y][x] = 4
    ux, uy = math.cos(angle), math.sin(angle)
    for i in range(25):
        x, y = round(grip[0]+ux*i), round(grip[1]+uy*i)
        img[y][x] = 2
        if i >= 3:
            img[y+1][x] = 3 if angle == 0 else img[y+1][x] or 3
    # Detached slash sparks: soft alpha must not affect the solid blade.
    if fx:
        for y in range(7, 11):
            for x in range(80, 84):
                img[y][x] = 5
    return img, body, grip


class BladeMaskTests(unittest.TestCase):
    def test_opaque_hilt_short_tanto_extended_dai_no_scaled_pixels(self):
        original, body, (gx, gy) = scene()
        original_pixels = sum(bool(p) for row in original for p in row)
        short = masks.masked_style(original, body, "tanto", foot=(gx, gy+15))
        long = masks.masked_style(original, body, "dai", foot=(gx, gy+15))
        self.assertIsNotNone(short)
        self.assertIsNotNone(long)
        # Handle and first five blade pixels stay bit-for-bit identical.
        for x in range(gx, gx+6):
            self.assertEqual(short[gy][x], original[gy][x])
            self.assertEqual(long[gy][x], original[gy][x])
        self.assertEqual(short[gy][gx+23], 0)
        # The blade may be gently angled because of the native tip offset.
        self.assertTrue(any(long[y][x] for x in range(gx+30, gx+38)
                            for y in range(gy-2, gy+4)))
        self.assertLess(sum(bool(p) for row in short for p in row), original_pixels)
        self.assertGreater(sum(bool(p) for row in long for p in row), original_pixels)
        # Separate flash is translucent, never used to tint the opaque hilt.
        self.assertEqual(short[8][80], 10)
        self.assertEqual(long[8][80], 10)

    def test_diagonal_attack_preserves_hand_connection(self):
        original, body, grip = scene(angle=math.pi/4, fx=False)
        a = masks.masked_style(original, body, "tanto", foot=(grip[0],grip[1]+15))
        b = masks.masked_style(original, body, "dai", foot=(grip[0],grip[1]+15))
        self.assertIsNotNone(a)
        self.assertIsNotNone(b)
        self.assertEqual(a[grip[1]][grip[0]], original[grip[1]][grip[0]])
        self.assertEqual(b[grip[1]][grip[0]], original[grip[1]][grip[0]])

    def test_isolated_spark_is_not_considered_a_blade(self):
        img = [[0]*64 for _ in range(64)]
        body = [[0]*64 for _ in range(64)]
        for y in range(15,19):
            for x in range(15,19): body[y][x] = 1
        for x in range(39,55): img[8][x] = 4
        self.assertIsNone(masks.masked_style(img, body, "dai", foot=(17,30)))

    def test_crop_is_exact_and_never_adds_alpha_to_solid_pixels(self):
        img, body, grip = scene()
        art = masks.masked_style(img, body, "tanto", foot=(grip[0],grip[1]+15))
        self.assertTrue(art)
        # Coordinates stay exactly relative to the foot point, not the hilt.
        # The full 224px canvas is used for the actual ROM pack.
        fixed = [[0]*masks.CANVAS for _ in range(masks.CANVAS)]
        fixed[masks.ORIGIN[1]-18][masks.ORIGIN[0]+10] = 2
        dx,dy,w,h,pixels = masks.cropped(fixed)
        self.assertEqual((dx,dy,w,h,pixels),(10,-18-14,1,1,bytes([2])))

    def test_pack_header_and_palette_are_somwork_compatible(self):
        class Fake:
            def weapon_tiles(self,row): return b""
            def weapon_group(self,row): return 0,bytes([0,0,0,0,0])
            def weapon_palette(self,palette_id): return [(0,0,0)]*16
            def weapon_pieces(self,row,frame,flags): return [(0,0,0,0,[0,0,0,0])]
            def pieces(self,*args): return [(0,0,0,0,[0,0,0,0])]
            def render(self,hero,pieces,image=None,**kwargs):
                # Simulate a blade near the hand on the same fixed foot anchor.
                x0,y0=masks.ORIGIN
                if kwargs.get("source") is not None:
                    for x in range(x0+6,x0+31): image[y0-18][x]=2
                else:
                    for y in range(y0-20,y0-12):
                        for x in range(x0,x0+8):image[y][x]=3
                return image
        old=masks.sword_frames
        try:
            masks.sword_frames=lambda gfx: [(10,0)]
            result=masks.pack(Fake(), rows=(14,))
        finally:
            masks.sword_frames=old
        self.assertEqual(result[:8],b"SOMWORK1")
        self.assertEqual(struct.unpack_from("<H",result,8)[0],2)
        self.assertEqual([result[10],result[result.find(b"SOMWORK1")+10]], [masks.KIND_TANTO]*2)
        # Both masks contain only palette indices 0..10; no soft blade pixels.
        offset=10
        for kind in (masks.KIND_TANTO,masks.KIND_DAI):
            self.assertEqual((result[offset],result[offset+1],result[offset+2]),(kind,14,10))
            w,h=result[offset+47],result[offset+48]
            data=result[offset+49:offset+49+w*h]
            self.assertTrue(data and all(c<=10 for c in data))
            offset+=49+w*h


if __name__ == "__main__":
    unittest.main()
