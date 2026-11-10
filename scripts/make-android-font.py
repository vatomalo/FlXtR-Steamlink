from pathlib import Path
import re,sys
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
root=Path(__file__).resolve().parent.parent
rows={m.group(1):[int(n) for n in m.group(2).split(',')] for m in re.finditer(r"\{'(.)',\{([^}]+)\}\}", (root/'src/font.h').read_text())}
rows[' ']=[0]*7
extra={',':[0,0,0,0,0,6,4],"'":[4,4,0,0,0,0,0],'(':[2,4,8,8,8,4,2],')':[8,4,2,2,2,4,8],'=':[0,0,31,0,31,0,0],'|':[4]*7,'&':[12,18,20,8,21,18,13],'%':[17,2,4,4,8,16,17]}
rows.update(extra)
names=['.notdef']+['u%04X'%ord(c) for c in rows]
fb=FontBuilder(1000,isTTF=True);fb.setupGlyphOrder(names);glyphs={};metrics={}
for name in names:
 pen=TTGlyphPen(None)
 mask=rows.get(chr(int(name[1:],16)),[31,17,17,17,17,17,31]) if name!='.notdef' else [31,17,17,17,17,17,31]
 for y,row in enumerate(mask):
  for x in range(5):
   if row&(1<<(4-x)):
    l=x*112;b=(6-y)*112;pen.moveTo((l,b));pen.lineTo((l+112,b));pen.lineTo((l+112,b+112));pen.lineTo((l,b+112));pen.closePath()
 glyphs[name]=pen.glyph();metrics[name]=(672,0)
map={ord(c):'u%04X'%ord(c) for c in rows}
for c in 'abcdefghijklmnopqrstuvwxyz':map[ord(c)]=map[ord(c.upper())]
fb.setupCharacterMap(map);fb.setupGlyf(glyphs);fb.setupHorizontalMetrics(metrics);fb.setupHorizontalHeader(ascent=900,descent=-100);fb.setupNameTable({'familyName':'FlXtR Pixel','styleName':'Regular','uniqueFontIdentifier':'FlXtRPixelOriginal','fullName':'FlXtR Pixel','psName':'FlXtRPixel'});fb.setupOS2(sTypoAscender=900,sTypoDescender=-100,usWinAscent=900,usWinDescent=100);fb.setupPost();fb.setupMaxp()
target=root/'android/app/src/main/assets/fonts/flxtr-pixel.ttf';target.parent.mkdir(parents=True,exist_ok=True);fb.save(target)
print('Built original pixel font:',target.stat().st_size,'bytes')
