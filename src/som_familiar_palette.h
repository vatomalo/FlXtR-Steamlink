/* Secret of Mana experimental familiar-palette transform.
 * Pure color math: no emulator, WRAM, ROM or save writes.
 * SNES little-endian BGR555; index zero is transparent and remains untouched.
 * Call only on a freshly copied, slot-local 0x20-byte palette.
 */
#ifndef SOM_FAMILIAR_PALETTE_H
#define SOM_FAMILIAR_PALETTE_H
#include <stdint.h>
#include <stddef.h>

#define SOM_FAMILIAR_PALETTE_BYTES 32u
#define SOM_FAMILIAR_PALETTE_COLORS 16u

static inline uint16_t som_familiar_bgr555(unsigned r,unsigned g,unsigned b){
    return (uint16_t)((r&31u)|((g&31u)<<5)|((b&31u)<<10));
}
/* Produce a recognizable ivory -> pale blue -> deep blue tonal ramp, while
 * preserving original luminance detail and transparent color zero.
 * This deliberately does NOT change monster hostility, ownership or AI.
 */
static inline void som_familiar_palette_celestial(uint8_t palette[SOM_FAMILIAR_PALETTE_BYTES]){
    if(!palette)return;
    for(size_t i=1;i<SOM_FAMILIAR_PALETTE_COLORS;i++){
        const unsigned word=(unsigned)palette[i*2u]|((unsigned)palette[i*2u+1u]<<8);
        const unsigned r=word&31u,g=(word>>5)&31u,b=(word>>10)&31u;
        /* Weighted source lightness, integer 0..31. */
        const unsigned light=(3u*r+6u*g+b+5u)/10u;
        /* Cyan-white highlights, blue shadows. No flattening into pure white. */
        const unsigned nr=4u+(27u*light+15u)/31u;
        const unsigned ng=9u+(22u*light+15u)/31u;
        const unsigned nb=18u+(13u*light+15u)/31u;
        const uint16_t recolored=som_familiar_bgr555(nr,ng,nb);
        palette[i*2u]=(uint8_t)recolored;
        palette[i*2u+1u]=(uint8_t)(recolored>>8);
    }
}
#endif
