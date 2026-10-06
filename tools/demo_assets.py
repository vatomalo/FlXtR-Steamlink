"""Original procedural demo artwork. No dependencies; not scraped film posters."""
from pathlib import Path
import math
import struct

ROOT = Path(__file__).resolve().parents[1]
def bmp(path, width, height, pixel):
    stride = (width * 3 + 3) & ~3
    data = bytearray()
    for y in range(height - 1, -1, -1):
        for x in range(width):
            r, g, b = pixel(x, y)
            data += bytes((b, g, r))
        data += bytes(stride - width * 3)
    header = b'BM' + struct.pack('<IHHI', 54 + len(data), 0, 0, 54)
    header += struct.pack('<IiiHHIIiiII', 40, width, height, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    path.write_bytes(header + data)

def cover(k):
    def pixel(x, y):
        c = (4, 10, 9)
        if ((x * 19 + y * 37 + k * 7) % 997 == 0): c = (194, 218, 210)
        cx, cy = 70 + k * 3, 75 + k * 7
        radius = math.hypot(x - cx, y - cy)
        if 38 < radius < 40: c = (104, 227, 131)
        if radius < 36: c = (19 + k * 9, 40 + k * 5, 40 + k * 12)
        ridge = 148 + int(23 * math.sin(x / 28 + k))
        if y > ridge: c = (8, 25 + k * 3, 21)
        if y == ridge or (y > ridge and y % 13 == 0): c = (35, 90, 53)
        if x % 20 == 0 and y > ridge: c = (27, 66, 44)
        return c
    return pixel

if __name__ == '__main__':
    assets = ROOT / 'assets'
    assets.mkdir(exist_ok=True)
    for k in range(6): bmp(assets / f'demo-{k}.bmp', 160, 224, cover(k))
    bmp(assets / 'icon.bmp', 116, 116, lambda x,y: (116,255,132) if (20<x<96 and (20<y<30 or 86<y<96)) or (20<x<30 and 20<y<96) or (62<x<96 and 54<y<64) or (86<x<96 and 54<y<96) else (0,0,0))
    rows = [
        ('BIG BUCK BUNNY', 'DEMO ART / ADD YOUR VIDEO URL', ''),
        ('ORBITAL', 'DEMO ART / ADD YOUR VIDEO URL', ''),
        ('GREEN HORIZON', 'DEMO ART / ADD YOUR VIDEO URL', ''),
        ('AFTER HOURS', 'DEMO ART / ADD YOUR VIDEO URL', ''),
        ('STATIC DREAMS', 'DEMO ART / ADD YOUR VIDEO URL', ''),
        ('DISTANT SIGNAL', 'DEMO ART / ADD YOUR VIDEO URL', ''),
    ]
    (ROOT/'catalog.tsv').write_text('# title\tmetadata\tposter BMP path\tdirect HTTP(S) URL\n' + ''.join(f'{title}\t{meta}\tassets/demo-{i}.bmp\t{url}\n' for i,(title,meta,url) in enumerate(rows)))
