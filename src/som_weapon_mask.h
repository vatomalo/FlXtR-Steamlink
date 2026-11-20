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
#endif
