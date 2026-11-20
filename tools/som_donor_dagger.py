"""Import a manually extracted SD3 dagger cutout, with no bundled game assets.

Use one cropped non-interlaced 8-bit RGB/RGBA/indexed PNG with transparent
background. Specify its visible hilt and tip pixels so the blade can be
anchored and rotated to Secret of Mana's existing attack poses.
No Pillow or other dependency is needed.
"""
import collections
import math
import struct
import zlib
from pathlib import Path


def read_png(path):
    data=Path(path).read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("donor dagger must be PNG")
    pos, palette, trans, payload, meta=8, [], b"", bytearray(), None
    while pos<len(data):
        if pos+12>len(data): raise ValueError("truncated PNG")
        length=struct.unpack_from(">I",data,pos)[0]
        name=data[pos+4:pos+8]
        chunk=data[pos+8:pos+8+length]
        if pos+12+length>len(data) or (zlib.crc32(name+chunk)&0xffffffff)!=struct.unpack_from(">I",data,pos+8+length)[0]:
            raise ValueError("corrupt PNG chunk")
        pos+=12+length
        if name==b"IHDR": meta=struct.unpack(">IIBBBBB",chunk)
        elif name==b"PLTE": palette=[tuple(chunk[i:i+3]) for i in range(0,len(chunk),3)]
        elif name==b"tRNS": trans=chunk
        elif name==b"IDAT": payload+=chunk
        elif name==b"IEND": break
    if meta is None: raise ValueError("PNG missing IHDR")
    width,height,depth,colour,compress,filtration,interlace=meta
    if depth!=8 or colour not in (2,3,6) or compress or filtration or interlace:
        raise ValueError("use non-interlaced 8-bit RGB/RGBA/indexed PNG")
    if width<2 or height<2 or width>128 or height>128:
        raise ValueError("crop the dagger to a 128x128-or-smaller PNG")
    channels={2:3,3:1,6:4}[colour]
    raw=zlib.decompress(bytes(payload))
    stride=width*channels
    if len(raw)!=(stride+1)*height: raise ValueError("incorrect PNG rows")
    out=[];prior=bytearray(stride);offset=0
    for _ in range(height):
        filt=raw[offset];encoded=raw[offset+1:offset+1+stride];offset+=stride+1
        row=bytearray(stride)
        for i,value in enumerate(encoded):
            left=row[i-channels] if i>=channels else 0
            above=prior[i]
            corner=prior[i-channels] if i>=channels else 0
            if filt==0: predictor=0
            elif filt==1: predictor=left
            elif filt==2: predictor=above
            elif filt==3: predictor=(left+above)//2
            elif filt==4:
                p=left+above-corner
                vals=(left,above,corner)
                predictor=min(range(3),key=lambda j:abs(p-vals[j]))
                predictor=vals[predictor]
            else: raise ValueError("unsupported PNG filter")
            row[i]=(value+predictor)&255
        if colour==6: rgba=[tuple(row[i:i+4]) for i in range(0,stride,4)]
        elif colour==2: rgba=[tuple(row[i:i+3])+(255,) for i in range(0,stride,3)]
        else:
            if any(i>=len(palette) for i in row):raise ValueError("bad PNG palette index")
            rgba=[palette[i]+(trans[i] if i<len(trans) else 255,) for i in row]
        out.append(rgba);prior=row
    if not any(a<128 for row in out for *_,a in row):
        raise ValueError("donor dagger needs transparent background")
    return out


def import_dagger(path,hilt,tip):
    image=read_png(path)
    height,width=len(image),len(image[0])
    for x,y in (hilt,tip):
        if not(0<=x<width and 0<=y<height and image[y][x][3]>=128):
            raise ValueError("hilt and tip must be opaque pixels in the donor")
    if math.dist(hilt,tip)<4:raise ValueError("dagger must be at least 4 pixels long")
    counter=collections.Counter(tuple(c[:3]) for line in image for c in line if c[3]>=128)
    top=[rgb for rgb,n in counter.most_common(5)]
    top += [top[-1]]*(5-len(top))
    indexed=[[0 if c[3]<128 else min(range(5),key=lambda i:
              sum((c[k]-top[i][k])**2 for k in range(3)))+1
              for c in line] for line in image]
    return {"pixels":indexed,"palette":top,"hilt":hilt,"tip":tip}


def place_dagger(canvas,donor,grip,tip):
    """Rotate donor to the observed native sword direction, resizing ONLY
    the blade length to Tanto proportions. Perpendicular width is unchanged.
    """
    gx,gy=grip
    vx,vy=tip[0]-gx,tip[1]-gy
    reach=math.hypot(vx,vy)
    if reach<1:return
    ux,uy=vx/reach,vy/reach
    hx,hy=donor["hilt"]
    tx,ty=donor["tip"]
    vx,vy=tx-hx,ty-hy
    length=math.hypot(vx,vy)
    dux,duy=vx/length,vy/length
    factor=max(7,min(17,round(reach*0.54)))/length
    for y,row in enumerate(donor["pixels"]):
        for x,index in enumerate(row):
            if not index:continue
            along=(x-hx)*dux+(y-hy)*duy
            across=-(x-hx)*duy+(y-hy)*dux
            ox=round(gx+along*factor*ux-across*uy)
            oy=round(gy+along*factor*uy+across*ux)
            if 0<=oy<len(canvas) and 0<=ox<len(canvas[0]):
                canvas[oy][ox]=index
