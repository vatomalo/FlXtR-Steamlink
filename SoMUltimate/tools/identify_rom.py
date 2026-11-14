#!/usr/bin/env python3
"""Read-only SNES ROM fingerprint and header candidate report.

This deliberately does NOT assume the correct mapper or that a 512-byte
copier header is present; both must be confirmed in a debugger/build pipeline.
"""
import argparse
import hashlib
from pathlib import Path

def read_title(buf, offset):
    if offset + 32 > len(buf):
        return None
    raw = buf[offset:offset+21]
    title = ''.join(chr(x) if 32 <= x <= 126 else '.' for x in raw).rstrip()
    map_mode = buf[offset+0x15]
    reset_vector = int.from_bytes(buf[offset+0x3c:offset+0x3e], "little")
    checksum = int.from_bytes(buf[offset+0x1e:offset+0x20], "little")
    inverse = int.from_bytes(buf[offset+0x1c:offset+0x1e], "little")
    return dict(title=title, map_mode=f"0x{map_mode:02X}",
                reset_vector=f"0x{reset_vector:04X}",
                checksum=f"0x{checksum:04X}",
                complement=f"0x{inverse:04X}",
                checksum_pair_valid=(checksum ^ inverse) == 0xffff)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom",type=Path)
    args = ap.parse_args()
    data=args.rom.read_bytes()
    if not data: raise SystemExit("Empty file")
    print(f"file: {args.rom}")
    print(f"bytes: {len(data)}")
    print(f"sha256: {hashlib.sha256(data).hexdigest()}")
    likely_header = len(data)%0x8000==512
    print(f"copier_header_512_size_heuristic: {likely_header}")
    for skip in (0,512):
        if skip and len(data)<0x8000+512: continue
        for mapper, offset in (("LoROM",0x7fc0),("HiROM",0xffc0),("ExHiROM",0x40ffc0)):
            header=read_title(data,skip+offset)
            if header:
                print(f"{mapper} candidate, copier_skip={skip}: {header}")
    if likely_header:
        print("unheadered_sha256:",hashlib.sha256(data[512:]).hexdigest())
    print("NOTE: candidates are not proof of mapping or revision. Verify against known-good header and execution traces.")

if __name__=="__main__":
    main()
