#ifndef FLXTR_SOM_DEPTH_MASK_H
#define FLXTR_SOM_DEPTH_MASK_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "som_bunshin_draw.h"

/* Match the owner's native OAM priority. Snes9x's main-screen sprite depth
 * is 32 + (priority + 1) * 4; foreground tiles with greater depth cover it. */
static inline unsigned som_owner_sprite_depth(const uint8_t *oam,unsigned who){
    if(!oam)return 44u; /* normal field sprite priority when OAM is unavailable */
    for(unsigned i=0;i<128u;i++){
        const uint8_t *piece=oam+4u*i;
        if(som_bun_hero_piece(piece,who))
            return 32u+((((unsigned)piece[3]>>4)&3u)+1u)*4u;
    }
    return 44u;
}

/* Restore original core pixels wherever its final foreground depth beats
 * the native depth of the actor whose extra artwork was drawn. */
static inline void som_mask_behind_foreground(uint8_t *frame,const uint8_t *base,
                                               unsigned width,unsigned height,size_t row,
                                               unsigned bytes,const uint8_t *depth,
                                               unsigned depth_pitch,unsigned actor_depth){
    if(!frame||!base||!depth||!depth_pitch||!bytes)return;
    const unsigned sx=width>=512u?2u:1u,sy=height>=400u?2u:1u;
    for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
        if(depth[(size_t)(y/sy)*depth_pitch+x/sx]>actor_depth)
            memcpy(frame+(size_t)y*row+(size_t)x*bytes,
                   base+(size_t)y*row+(size_t)x*bytes,bytes);
    }
}
#endif
