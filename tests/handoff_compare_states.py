"""Compare all active fields of the original plain-ROM DMG fixture.

Gambatte serializes some uninitialized SaveState members for hardware that this
fixture does not possess: CGB palettes, cartridge bank controllers, HuC3 RAM and
inactive sound scheduling. Those bytes are not emulator state for this ROM.
"""
from pathlib import Path
import sys

INACTIVE={'dmgpal','huc3ram','rambank','rombank','schnlct','schnomk'}
def fields(data):
    assert data[:2]==b'\x00\x01'
    cursor=5+int.from_bytes(data[2:5],'big');result={}
    while cursor<len(data):
        end=data.index(0,cursor);name=data[cursor:end].decode('ascii');size=int.from_bytes(data[end+1:end+4],'big');cursor=end+4
        assert cursor+size<=len(data)
        result[name]=data[cursor:cursor+size];cursor+=size
    return result
def compare(reference,actual,cgb=False):
    left=fields(Path(reference).read_bytes());right=fields(Path(actual).read_bytes())
    assert left.keys()==right.keys()
    inactive=INACTIVE-{'dmgpal'} if cgb else INACTIVE
    changed=[key for key in left if key not in inactive and left[key]!=right[key]]
    assert not changed,changed
    assert len(left)>100
    print('Exact CPU/RAM/active PPU state matches across architectures:',len(left)-len(inactive),'fields')
if __name__=='__main__':compare(sys.argv[1],sys.argv[2])
