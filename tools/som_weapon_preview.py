#!/usr/bin/env python3
"""Inspect every Bushido weapon mask before installing it on Steam Link.

Builds a 3-row contact sheet from the SAME local SoM Turbo ROM used for the
weapon pack. Rows are TANTO / native KATANA / DAI KATANA, columns are the
native sword swing frames. Missing replacement frames are rendered in native
form with a red corner. The report includes exact pack coverage and rejected
frames. No game art or extracted SD3 donor PNG is added to Git.
"""
import argparse
import struct
import zlib
from pathlib import Path

import som_hero_gfx as H
import som_weapon_mask as M
import som_villager_work as V

VIEW=96
ZOOM=3
PAD=9
MAX_COLS=12
ROWS=("TANTO","KATANA","DAI KATANA","KUSARIGAMA")
GLYPHS={
    "T":("11111","00100","00100","00100","00100","00100","00100"),
    "A":("01110","10001","10001","11111","10001","10001","10001"),
    "N":("10001","11001","10101","10011","10001","10001","10001"),
    "O":("01110","10001","10001","10001","10001","10001","01110"),
    "K":("10001","10010","10100","11000","10100","10010","10001"),
    "D":("11100","10010","10001","10001","10001","10010","11100"),
    "I":("11111","00100","00100","00100","00100","00100","11111"),
    " ":("00000",)*7,
    "0":("01110","10011","10101","10101","11001","10001","01110"),
    "1":("00100","01100","00100","00100","00100","00100","01110"),
    "2":("01110","10001","00001","00010","00100","01000","11111"),
    "3":("11110","00001","00001","00110","00001","00001","11110"),
    "4":("10010","10010","10010","11111","00010","00010","00010"),
    "5":("11111","10000","10000","11110","00001","00001","11110"),
    "6":("01111","10000","10000","11110","10001","10001","01110"),
    "7":("11111","00001","00010","00100","01000","01000","01000"),
    "8":("01110","10001","10001","01110","10001","10001","01110"),
    "9":("01110","10001","10001","01111","00001","00001","11110"),
}


def unpack(data):
    """Validate all lengths/indices of the work pack before preview or use."""
    if len(data)<10 or data[:8]!=b"SOMWORK1":
        raise ValueError("wrong weapon-mask pack header")
    count=struct.unpack_from("<H",data,8)[0]
    end=len(data)
    ptr=10
    records={}
    for _ in range(count):
        if ptr+44>end:raise ValueError("truncated weapon mask entry")
        kind,row,frame,nframes=struct.unpack_from("<BBBB",data,ptr)
        if kind not in (M.KIND_TANTO,M.KIND_DAI,M.KIND_NATIVE,M.KIND_SHORT_SWORD,M.KIND_KUSARIGAMA) or row not in M.SWORD_ROWS or nframes!=1:
            raise ValueError("not a supported weapon-mask pack")
        palette=struct.unpack_from("<20H",data,ptr+4)
        if any(c&0x8000 for c in palette):
            raise ValueError("invalid BGR555 mask palette")
        off=ptr+44
        if off+5>end:raise ValueError("truncated mask frame header")
        ticks,dx,dy,w,h=struct.unpack_from("<BbbBB",data,off)
        if ticks==0 or not w or not h or off+5+w*h>end:
            raise ValueError("invalid mask frame dimensions")
        pixels=data[off+5:off+5+w*h]
        if any(pixel>10 for pixel in pixels):
            raise ValueError("mask has invalid colour/alpha index")
        key=(kind,row,frame)
        if key in records:raise ValueError("duplicate mask frame")
        records[key]=(dx,dy,w,h,pixels,palette)
        ptr=off+5+w*h
    if ptr!=end:
        raise ValueError("trailing bytes in weapon mask pack")
    return records


def rgb555(value):
    return tuple(((value>>s)&31)*255//31 for s in (0,5,10))


def blend(old,new,alpha):
    return tuple((old[i]*(255-alpha)+new[i]*alpha+127)//255 for i in range(3))


def paste(canvas,colours,indices,offset=(0,0),index_start=1,alpha=255):
    height=len(canvas);width=len(canvas[0])
    for y,row in enumerate(indices):
        for x,c in enumerate(row):
            if c<index_start or c>=index_start+len(colours):continue
            xx=x+offset[0];yy=y+offset[1]
            if 0<=xx<width and 0<=yy<height:
                canvas[yy][xx]=blend(canvas[yy][xx],colours[c-index_start],alpha)


def render_cell(gfx,row,frame,flags,record=None):
    width=M.CANVAS
    image=[[(25,25,42) if (x//8+y//8)%2 else (30,31,47)
            for x in range(width)] for y in range(width)]
    body=gfx.render("boy",gfx.pieces("boy","sword",frame,flags),
                    size=width,origin=M.ORIGIN)
    hero=gfx.palette("boy")
    paste(image,hero[1:],body,index_start=1)
    if record is None:
        weapon=[[0]*width for _ in range(width)]
        gfx.render("boy",gfx.weapon_pieces(row,frame,flags),image=weapon,
                   origin=M.ORIGIN,source=gfx.weapon_tiles(row))
        blade=gfx.weapon_palette(gfx.weapon_group(row)[1][4])[1:6]
        paste(image,blade,weapon,index_start=1)
        return image
    dx,dy,w,h,pixels,pal=record
    weapon=[pixels[i*w:(i+1)*w] for i in range(h)]
    # Art is recorded relative to hero feet; undo vertical hero-height offset
    x0=M.ORIGIN[0]+dx
    y0=M.ORIGIN[1]+V.HERO_HEIGHT+dy
    blade=[rgb555(c) for c in pal[:5]]
    if flags & 0x20:
        weapon = [line[::-1] for line in weapon]
        x0 = 2*M.ORIGIN[0]-x0-w
    paste(image,blade,weapon,offset=(x0,y0),index_start=1)
    paste(image,blade,weapon,offset=(x0,y0),index_start=6,alpha=64)
    return image


def png(path,rgb):
    height=len(rgb);width=len(rgb[0])
    stream=bytearray()
    for line in rgb:
        stream+=b"\0"
        for r,g,b in line:stream+=bytes((r,g,b))
    def chunk(kind,body):
        packet=kind+body
        return struct.pack(">I",len(body))+packet+struct.pack(">I",zlib.crc32(packet)&0xffffffff)
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n"+
        chunk(b"IHDR",struct.pack(">IIBBBBB",width,height,8,2,0,0,0))+
        chunk(b"IDAT",zlib.compress(stream,6))+chunk(b"IEND",b""))


def stamp(pic,x0,y0,label,colour=(240,238,215)):
    px=x0
    for letter in label.upper():
        for y,row in enumerate(GLYPHS.get(letter,GLYPHS[" "])):
            for x,v in enumerate(row):
                if v=="1" and 0<=y0+y<len(pic) and 0<=px+x<len(pic[0]):
                    pic[y0+y][px+x]=colour
        px+=6


def contact_sheet(gfx,records,row,frames):
    count=len(frames)
    if count<1:raise ValueError("no compatible sword frames")
    panel=VIEW*ZOOM
    sheet_w=PAD+(panel+PAD)*count
    sheet_h=24+(panel+22)*len(ROWS)+PAD
    pic=[[(15,17,29)]*sheet_w for _ in range(sheet_h)]
    for col,(frame,flags) in enumerate(frames):
        for which,title in enumerate(ROWS):
            x0=PAD+(panel+PAD)*col
            y0=24+which*(panel+22)
            stamp(pic,x0,y0-14,title,(235,225,196))
            stamp(pic,x0+panel-20,y0-14,f"{frame:02d}")
            kind=(M.KIND_TANTO if which==0 else M.KIND_DAI if which==2 else
                  M.KIND_KUSARIGAMA if which==3 else None)
            rec=records.get((kind,row,frame)) if kind is not None else None
            img=render_cell(gfx,row,frame,flags,rec)
            # Red missing-frame marker means the native weapon was used.
            if which!=1 and rec is None:
                for i in range(8):
                    for j in range(i+1):pic[y0+i][x0+j]=(235,55,75)
            base_x=M.ORIGIN[0]-VIEW//2
            base_y=M.ORIGIN[1]-VIEW*2//3
            for sy in range(VIEW):
                for sx in range(VIEW):
                    yy=y0+sy*ZOOM
                    xx=x0+sx*ZOOM
                    colour=img[base_y+sy][base_x+sx]
                    for dy in range(ZOOM):
                        for dx in range(ZOOM):pic[yy+dy][xx+dx]=colour
    return pic


def direction_coverage(gfx, records, row, poses):
    """Count replacement and emergency fallback for every visible script pose."""
    counts = {}
    for direction, frame, flags in poses:
        if not gfx.weapon_pieces(row, frame, flags):
            continue
        row_counts = counts.setdefault(direction, [0, 0, 0, 0])
        row_counts[0] += 1
        for i, kind in enumerate((M.KIND_TANTO, M.KIND_DAI, M.KIND_NATIVE), 1):
            row_counts[i] += (kind, row, frame) in records
    if "side" in counts:
        counts["left (mirrored side)"] = counts["side"][:]
    return counts


def main():
    parser=argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("rom",help="original local Turbo ROM")
    parser.add_argument("mask",help="generated som_weapon_masks.bin")
    parser.add_argument("png",help="output contact-sheet PNG")
    parser.add_argument("--row",type=int,default=14,help="sword row (9..17)")
    parser.add_argument("--max-frames",type=int,default=10)
    parser.add_argument("--direction",choices=("up","down","side"),default="side")
    args=parser.parse_args()
    if args.row not in M.SWORD_ROWS:parser.error("row must be 9..17")
    if args.max_frames<1 or args.max_frames>MAX_COLS:parser.error("max-frames must be 1..12")
    gfx=H.HeroGfx.load(args.rom)
    records=unpack(Path(args.mask).read_bytes())
    # Include both modified and rejected native poses in the small sheet.
    poses=M.sword_poses(gfx)
    frames=list(dict.fromkeys((frame,flags) for direction,frame,flags in poses
                              if direction==args.direction and gfx.weapon_pieces(args.row,frame,flags)))
    covered=[pair for pair in frames if (M.KIND_TANTO,args.row,pair[0]) in records]
    rejected=[pair for pair in frames if (M.KIND_TANTO,args.row,pair[0]) not in records]
    if covered and rejected and args.max_frames>1:
        frames=[covered[0],rejected[0],*covered[1:],*rejected[1:]]
    frames=frames[:args.max_frames]
    pic=contact_sheet(gfx,records,args.row,frames)
    png(args.png,pic)
    lines=["Weapon masks, native frame numbers, sword grade row "+str(args.row),
           "Rows: Tanto, native Katana, Dai Katana, Kusarigama whip overlay",
           "Red marker: mask unavailable, show native sword; no red marker: masked replacement",
           "Only the weapon silhouette is replaced. Hero body stays native.",
           ""]
    for frame,flags in frames:
        a=(M.KIND_TANTO,args.row,frame) in records
        b=(M.KIND_DAI,args.row,frame) in records
        k=(M.KIND_KUSARIGAMA,args.row,frame) in records
        lines.append(f"frame {frame}, flags {flags:#04x}: Tanto {'yes' if a else 'native fallback'}, Dai {'yes' if b else 'native fallback'}, Kusarigama {'yes' if k else 'native fallback'}")
    lines.append("")
    for direction, (total, tanto, dai, native) in direction_coverage(
            gfx, records, args.row, poses).items():
        lines.append(f"{direction}: {total} visible poses, Tanto {tanto}, Dai {dai}, "
                     f"native fallback {native}")
    lines.append(f"Pack entries: {len(records)}; previewed frames: {len(frames)}")
    Path(str(args.png)+".txt").write_text("\n".join(lines)+"\n")
    print("\n".join(lines))
    print(f"Preview: {args.png}")


if __name__=="__main__":
    main()
