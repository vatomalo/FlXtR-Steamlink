"""Hero sprite poses straight from the ROM: scripts, frames, pieces, pixels.

Written from cidwel/som-decomp docs/graphics.md (sections 2, 3, 5) and
docs/actor-animation-operands.md; nothing is run from that repository.
The video memory only ever holds the hero's current frame, so drawing any
other pose (a villager swinging the boy's axe, a fighter with the girl's
arms) needs the pictures decoded here.

- Tile pools (uncompressed, 8x8 4bpp): boy D5:0000, girl D5:8000, sprite D6:0000.
- Palettes: 15 BGR555 colours at C8:1F00 / 1F1E / 1F3C (index 0 transparent).
- Scripts: attack states D1:3040 + 240*weapon_type, ordinary states D1:37CC;
  entry (anim*3 + dir)*2, dir 0 up, 1 down, 2 side (left = side mirrored).
- Frame descriptors: D1:[word D0:FFD0 + 2*type] + 2*frame; piece lists in D2.

File offset of HiROM address bank:addr = (bank - 0xC0) << 16 | addr (the
Turbo ROM is 4 MiB without a copier header).
"""
import struct
import zlib
from pathlib import Path

HEROES = {"boy": (0x000, 0x150000, 0x81F00), "girl": (0x200, 0x158000, 0x81F1E),
          "sprite": (0x400, 0x160000, 0x81F3C)}      # piece lists, tile pool, palette
WEAPON_TYPES = ("glove", "sword", "axe", "spear", "whip", "bow", "boomerang", "javelin")
ATTACK_SCRIPTS, ATTACK_STRIDE, POSE_SCRIPTS = 0x3040, 240, 0x37CC
DIRS = {"up": 0, "down": 1, "side": 2}


def off(bank, addr):
    return ((bank - 0xC0) << 16) | (addr & 0xFFFF)


def sext7(value):
    return value - 128 if value & 0x40 else value


def op_length(op):
    """Bytes taken by a script operation (first byte has bit 7 set), or None
    for the end of the script."""
    if op == 0xFF:
        return None
    if op == 0xF8:
        return 4
    if op == 0xF9:
        return 3
    if 0xF0 <= op <= 0xF7:
        return 3
    return 1          # 80-AF, B0-EF (checked over every script), FB-FE


class HeroGfx:
    def __init__(self, rom):
        self.rom = bytes(rom)

    @classmethod
    def load(cls, path):
        return cls(Path(path).read_bytes())

    def w16(self, bank, addr):
        return struct.unpack_from("<H", self.rom, off(bank, addr))[0]

    def script(self, weapon, anim, direction, attack=True):
        """[(entry flags, frame index, ticks)] of an animation up to its end."""
        wtype = WEAPON_TYPES.index(weapon) if isinstance(weapon, str) else weapon
        d = DIRS.get(direction, direction)
        table = (ATTACK_SCRIPTS + ATTACK_STRIDE * wtype) if attack else POSE_SCRIPTS
        pointer = self.w16(0xD1, table + (anim * 3 + d) * 2)
        steps = []
        for _ in range(256):
            b0 = self.rom[off(0xD1, pointer)]
            if b0 & 0x80:
                if b0 == 0xF1:                    # jump inside the bank
                    target = self.w16(0xD1, pointer + 1)
                    if target <= pointer:
                        break                     # a loop: one pass is enough
                    pointer = target
                    continue
                length = op_length(b0)
                if length is None:
                    break
                pointer += length
                continue
            b1 = self.rom[off(0xD1, pointer + 1)]
            steps.append((b0, b1, (b0 & 7) + 1 if (b0 & 7) < 4 else 1))
            pointer += 2
        return steps

    def pieces(self, hero, weapon, frame, entry_flags=0, left=False):
        """[(x, y, hflip, vflip, [4 tile byte offsets])], x/y from the foot point
        minus the body height, as docs/graphics.md section 5 steps 2-4."""
        wtype = WEAPON_TYPES.index(weapon) if isinstance(weapon, str) else weapon
        lists, pool, _ = HEROES[hero]
        base = struct.unpack_from("<H", self.rom, 0x10FFD0 + 2 * wtype)[0]
        desc = self.w16(0xD1, base + 2 * frame)
        lo, hi = desc & 0xFF, desc >> 8
        flags = entry_flags ^ (0x20 if left else 0)
        flips = (lo ^ (flags << 1)) & 0xC0
        pointer = self.w16(0xD2, lists + 2 * hi)
        header = self.rom[off(0xD2, pointer)]
        count = (header & 15) or 1
        cursor, sources = pointer + 1, []
        while len(sources) < 4 * count:
            word = self.w16(0xD2, cursor)
            cursor += 2
            start = (word << 1) & 0x7FE0
            if word & 0x8000:
                run = (word & 15) + 1
                sources += ([start + 0x20 * k for k in range(run)] if word & 0x4000
                            else [start] * run)
            else:
                sources.append((word & 0x3FF0) << 1)
        out = []
        for k in range(count):
            word = self.w16(0xD2, cursor)
            cursor += 2
            y, x = sext7(word & 0x7F), sext7((word >> 8) & 0x7F)
            if flips & 0x40:
                x = -x - 16
            if flips & 0x80:
                y = -y - 16
            hflip = ((word >> 15) & 1) ^ ((flips >> 6) & 1)
            out.append((x, y, hflip, (flips >> 7) & 1,
                        [pool + s for s in sources[4 * k:4 * k + 4]]))
        return out

    def tile(self, offset):
        """8x8 colour indices of the 4bpp tile at a file offset."""
        data = self.rom[offset:offset + 32]
        rows = []
        for y in range(8):
            p0, p1, p2, p3 = data[2 * y], data[2 * y + 1], data[16 + 2 * y], data[17 + 2 * y]
            rows.append([((p0 >> (7 - x)) & 1) | (((p1 >> (7 - x)) & 1) << 1) |
                         (((p2 >> (7 - x)) & 1) << 2) | (((p3 >> (7 - x)) & 1) << 3)
                         for x in range(8)])
        return rows

    def palette(self, hero):
        """16 RGB colours (index 0 = transparent)."""
        start = HEROES[hero][2]
        colours = [(0, 0, 0)]
        for k in range(15):
            value = struct.unpack_from("<H", self.rom, start + 2 * k)[0]
            colours.append(tuple(((value >> s) & 31) * 255 // 31 for s in (0, 5, 10)))
        return colours

    def render(self, hero, pieces, size=64, origin=(32, 48)):
        """Colour-index image (rows of ints, 0 transparent) of a frame's pieces."""
        image = [[0] * size for _ in range(size)]
        for x, y, hflip, vflip, tiles in pieces:
            quads = [(0, 0), (8, 0), (0, 8), (8, 8)]   # top-left, top-right, bottom-left, -right
            for (qx, qy), offset in zip(quads, tiles):
                pixels = self.tile(offset)
                for ty in range(8):
                    for tx in range(8):
                        sx = (15 - (qx + tx)) if hflip else qx + tx
                        sy = (15 - (qy + ty)) if vflip else qy + ty
                        px, py = origin[0] + x + sx, origin[1] + y + sy
                        if pixels[ty][tx] and 0 <= px < size and 0 <= py < size:
                            image[py][px] = pixels[ty][tx]
        return image


def write_png(path, rows, palette, scale=4):
    """A small RGBA PNG from colour-index rows (index 0 transparent)."""
    height, width = len(rows) * scale, len(rows[0]) * scale
    raw = bytearray()
    for row in rows:
        line = bytearray()
        for index in row:
            r, g, b = palette[index]
            line += bytes((r, g, b, 0 if index == 0 else 255)) * scale
        for _ in range(scale):
            raw += b"\x00" + line
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" +
                           chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))
