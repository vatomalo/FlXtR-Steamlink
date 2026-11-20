/* Colour-safe 25% FX blend for runtime Bushido weapon masks.
 * 1..5: opaque blade/hilt pixels, 6..10: FX only at 25% alpha.
 * Pixel format: libretro 0RGB1555=0, XRGB8888=1, RGB565=2.
 * The actual masks are packed from the player's own ROM; no ROM patching. */
#ifndef FLXTR_SOM_WEAPON_MASK_H
#define FLXTR_SOM_WEAPON_MASK_H
#include <stdint.h>

static inline uint32_t som_mask_mix25(uint32_t dst,uint32_t src,int format){
    if(format==1){                           /* XRGB8888, three 8-bit channels */
        uint32_t out=0;
        for(unsigned shift=0;shift<=16u;shift+=8u){
            const unsigned d=(dst>>shift)&255u,s=(src>>shift)&255u;
            out|=((3u*d+s+2u)/4u)<<shift;
        }
        return out;
    }
    const unsigned bits=format==2?6u:5u;
    const unsigned gshift=5u,rshift=format==2?11u:10u;
    const unsigned gmask=(1u<<bits)-1u;
    const unsigned d0=dst&31u,s0=src&31u;
    const unsigned dg=(dst>>gshift)&gmask,sg=(src>>gshift)&gmask;
    const unsigned dr=(dst>>rshift)&31u,sr=(src>>rshift)&31u;
    return ((3u*d0+s0+2u)/4u)|
           (((3u*dg+sg+2u)/4u)<<gshift)|
           (((3u*dr+sr+2u)/4u)<<rshift);
}

/* Draw a ROM-extracted, grip-aligned weapon mask on the FINAL Libretro frame.
 * Solid palette 1..5 keeps native SNES sharpness; indices 6..10 use a
 * deliberate 25% blend. The pack contains no hero pixels or background. */
static inline int som_weapon_mask_draw(const SomNpcWorkState *sprite,const uint8_t *ram,
                                       uint8_t *pixels,unsigned width,unsigned height,
                                       size_t pitch,int format){
    if(!sprite||!sprite->active||!ram||!pixels)return 0;
    const unsigned obj=SOM_NPC_OBJECT(sprite->slot);
    const unsigned cam=(ram[obj+0x0Bu]&0x80u)?0xACu:0xA8u;
    const int xbase=(int16_t)(som_npc_u16(ram,obj+2u)-som_npc_u16(ram,cam));
    const int ybase=(int16_t)(som_npc_u16(ram,obj+4u)-som_npc_u16(ram,cam+2u))-
                     (int)ram[obj+0x45u];
    const unsigned sx=width>=512u?2u:1u,sy=height>=400u?2u:1u;
    const int bw=(int)(width/sx),bh=(int)(height/sy);
    const uint8_t *record=sprite->entry.first;
    const int dx=(int8_t)record[1],dy=(int8_t)record[2];
    const unsigned w=record[3],h=record[4];
    const uint8_t *image=record+5u;
    uint32_t colours[6]={0};
    for(unsigned c=1;c<=5u;c++){
        const unsigned value=sprite->entry.colours[c-1u];
        colours[c]=som_overlay_color(format,(value&31u)*255u/31u,
                                    ((value>>5u)&31u)*255u/31u,
                                    ((value>>10u)&31u)*255u/31u);
    }
    int drawn=0;
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
        const unsigned index=image[(size_t)y*w+x];
        if(index==0||index>10u)continue;
        const int ox=xbase+(sprite->dir==3u?-(dx+(int)x)-1:dx+(int)x);
        const int oy=ybase+dy+(int)y;
        if(ox<0||oy<0||ox>=bw||oy>=bh)continue;
        for(unsigned yy=0;yy<sy;yy++)for(unsigned xx=0;xx<sx;xx++){
            const unsigned px=(unsigned)ox*sx+xx,py=(unsigned)oy*sy+yy;
            uint32_t colour=colours[index>5u?index-5u:index];
            if(index>5u){
                const uint32_t dest=format==1?
                    ((const uint32_t*)(const void*)(pixels+py*pitch))[px]:
                    ((const uint16_t*)(const void*)(pixels+py*pitch))[px];
                colour=som_mask_mix25(dest,colour,format);
            }
            som_overlay_put(pixels,pitch,format,px,py,colour);
        }
        drawn=1;
    }
    return drawn;
}
#endif
