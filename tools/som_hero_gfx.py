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
- Weapon pieces (frame list 2, $C0:F1CF): D4:[table word + 2*frame], where the
  table is the third/fourth byte of D4:C000 + 4*g. Their tiles index the 64
  effect tiles the equip refresh ($C0:EA41) expands from D4 (Turbo encoding:
  24 bytes per tile, colours 1-5), and their 5 colours come from
  D1:F600 + 10 * weapon palette id ($C1:859B; id = row byte 4, the
  equip refresh stores it in +0x069). Turbo routes $C1:859B through F0:257B
  and F0:4BCB, which pass each colour through a filter keyed on 7E:CF06; the
  raw table colours are used here. Shapes match the swing; the colours and
  which palette slots they land in are NOT yet checked against a live game.

Any other object kind (villagers 0x83-0xFF, monsters) is read from its
16-byte object-table entry at C0:0000 + 16 * id (docs/monster-3.md section
3): tile pool bank D5 + (byte 1 >> 2 & 7) at (word 0 & 0x3FF) << 6, piece
lists at D2:[word 2 + 2 * h], ordinary scripts word 5, attack scripts
word 7, palette C8:1000 + 30 * id, frame descriptors D1:[D0:FFF0] + 2 * frame.
Hero ids 0x80-0x82 resolve to the same pools and lists as the names. The
empty tile (pool offset 0x7FE0, zero in the hero pools) is skipped, since
villager pools do not end on a 32 KiB window.

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
EMPTY_TILE = 0x7FE0


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

    def actor(self, kind):
        """Graphics of an object kind (int) or hero name: dict of piece-list
        offset, tile-pool file offset, palette file offset and the ordinary /
        attack script tables (bank D1)."""
        if isinstance(kind, str):
            lists, pool, palette = HEROES[kind]
            return {"lists": lists, "pool": pool, "palette": palette,
                    "ordinary": POSE_SCRIPTS, "attack": None}
        e = self.rom[0x100000 + 16 * kind:0x100000 + 16 * kind + 16]
        word0 = e[0] | e[1] << 8
        return {"lists": e[2] | e[3] << 8,
                "pool": (0x15 + ((e[1] >> 2) & 7)) << 16 | (word0 & 0x3FF) << 6,
                "palette": 0x81000 + 30 * kind,
                "ordinary": e[5] | e[6] << 8, "attack": e[7] | e[8] << 8}

    def script(self, weapon, anim, direction, attack=True, table=None):
        """[(entry flags, frame index, ticks)] of an animation up to its end.
        `table` (bank D1) overrides the hero tables, e.g. actor(k)["ordinary"]."""
        wtype = WEAPON_TYPES.index(weapon) if isinstance(weapon, str) else weapon
        d = DIRS.get(direction, direction)
        if table is None:
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
        minus the body height, as docs/graphics.md section 5 steps 2-4.
        `hero` is a hero name or object kind; weapon None selects the
        descriptor table of the other objects (D0:FFF0). Empty tiles are None."""
        sprite = self.actor(hero)
        lists, pool = sprite["lists"], sprite["pool"]
        if weapon is None:
            base = struct.unpack_from("<H", self.rom, 0x10FFF0)[0]
        else:
            wtype = WEAPON_TYPES.index(weapon) if isinstance(weapon, str) else weapon
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
                        [None if s == EMPTY_TILE else pool + s
                         for s in sources[4 * k:4 * k + 4]]))
        return out

    def weapon_group(self, row):
        """(graphics group g, weapon row bytes) as $C0:EA41 reads them."""
        r = self.rom[off(0xD0, 0x1000 + 12 * row):][:12]
        g = r[0] if r[3] < 4 else r[3] + 0x12
        return g, r

    def weapon_tiles(self, row):
        """The 64 effect tiles (32 bytes each, SNES 4bpp) the game expands into
        the hero's buffer 7E:2000 for this weapon row (Turbo's converter)."""
        g, _ = self.weapon_group(row)
        start = 24 * self.w16(0xD4, 0xC000 + 4 * g)
        src, out = self.rom[off(0xD4, start):][:64 * 24], bytearray(64 * 32)
        for t in range(64):
            s, o = src[24 * t:24 * t + 24], 32 * t
            for y in range(8):
                a, b, c = s[2 * y], s[2 * y + 1], s[16 + y]
                keep = ~(b & c) & 0xFF
                out[o + 2 * y], out[o + 2 * y + 1] = a & keep, b & keep
                out[o + 16 + 2 * y] = c & keep
        return bytes(out)

    def weapon_pieces(self, row, frame, entry_flags=0, left=False):
        """[(x, y, hflip, vflip, [4 tile indices or None])] of list 2 for a frame,
        relative to the same point as pieces() (the boy: no nibble offset)."""
        g, _ = self.weapon_group(row)
        table = self.w16(0xD4, 0xC000 + 4 * g + 2)
        pointer = self.w16(0xD4, table + 2 * frame)
        header = self.rom[off(0xD4, pointer)]
        count = header & 15
        if count == 0 or count >= 11:
            return []
        cursor, tiles = pointer + 1, []
        while len(tiles) < 4 * count:
            b0 = self.rom[off(0xD4, cursor)]
            if not b0 & 0x80:
                cursor += 1
                tiles.append(None if b0 & 0x7F == 0x7F else b0 & 0x7F)
                continue
            b1 = self.rom[off(0xD4, cursor + 1)]
            cursor += 2
            run, first = (b0 & 0x3F) + 1, b1 & 0x7F
            if b0 & 0x40:
                tiles += [first + k for k in range(run)]
            else:
                tiles += [None if first == 0x7F else first] * run
        tiles = tiles[:4 * count]
        flips = (((entry_flags ^ (0x20 if left else 0)) << 1) & 0xC0)
        scale = 2 if header & 0x10 else 1
        out = []
        for k in range(count):
            lo, hi = self.rom[off(0xD4, cursor)], self.rom[off(0xD4, cursor + 1)]
            cursor += 2
            y, x = sext7(lo & 0x7F) * scale, sext7(hi & 0x7F) * scale
            if flips & 0x80:
                y = -y - 16
            if flips & 0x40:
                x = -x - 16
            out.append((x, y, (hi >> 7) ^ ((flips >> 6) & 1), (lo >> 7) ^ (flips >> 7),
                        tiles[4 * k:4 * k + 4]))
        return out

    def weapon_palette(self, palette_id):
        """16 RGB colours: 1-5 from D1:F600 + 10 * id, the rest unused."""
        colours = [(0, 0, 0)] * 16
        for k in range(5):
            value = self.w16(0xD1, 0xF600 + 10 * palette_id + 2 * k)
            colours[1 + k] = tuple(((value >> s) & 31) * 255 // 31 for s in (0, 5, 10))
        return colours

    @staticmethod
    def tile_pixels(data):
        """8x8 colour indices of 32 bytes of SNES 4bpp."""
        rows = []
        for y in range(8):
            p0, p1, p2, p3 = data[2 * y], data[2 * y + 1], data[16 + 2 * y], data[17 + 2 * y]
            rows.append([((p0 >> (7 - x)) & 1) | (((p1 >> (7 - x)) & 1) << 1) |
                         (((p2 >> (7 - x)) & 1) << 2) | (((p3 >> (7 - x)) & 1) << 3)
                         for x in range(8)])
        return rows

    def tile(self, offset):
        """8x8 colour indices of the 4bpp tile at a file offset."""
        return self.tile_pixels(self.rom[offset:offset + 32])

    def palette(self, hero):
        """16 RGB colours (index 0 = transparent) of a hero name or kind."""
        start = self.actor(hero)["palette"]
        colours = [(0, 0, 0)]
        for k in range(15):
            value = struct.unpack_from("<H", self.rom, start + 2 * k)[0]
            colours.append(tuple(((value >> s) & 31) * 255 // 31 for s in (0, 5, 10)))
        return colours

    def render(self, hero, pieces, size=64, origin=(32, 48), image=None, source=None,
               base=0):
        """Colour-index image (rows of ints, 0 transparent) of a frame's pieces.
        Tiles are ROM offsets, or indices into `source` (weapon_tiles) drawn
        with colour index + `base` so two palettes can share one image."""
        image = image or [[0] * size for _ in range(size)]
        size = len(image)
        for x, y, hflip, vflip, tiles in pieces:
            quads = [(0, 0), (8, 0), (0, 8), (8, 8)]   # top-left, top-right, bottom-left, -right
            for (qx, qy), offset in zip(quads, tiles):
                if offset is None:
                    continue
                pixels = (self.tile(offset) if source is None else
                          self.tile_pixels(source[32 * offset:32 * offset + 32]))
                for ty in range(8):
                    for tx in range(8):
                        sx = (15 - (qx + tx)) if hflip else qx + tx
                        sy = (15 - (qy + ty)) if vflip else qy + ty
                        px, py = origin[0] + x + sx, origin[1] + y + sy
                        if pixels[ty][tx] and 0 <= px < size and 0 <= py < size:
                            image[py][px] = pixels[ty][tx] + base
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
