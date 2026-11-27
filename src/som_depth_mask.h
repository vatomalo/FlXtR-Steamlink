#ifndef FLXTR_SOM_DEPTH_MASK_H
#define FLXTR_SOM_DEPTH_MASK_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "som_bunshin_draw.h"

/* Match the owner's native OAM priority. Snes9x's main-screen sprite depth
 * is 32 + (priority + 1) * 4; foreground tiles with greater depth cover it. */
static inline unsigned som_owner_sprite_depth(const uint8_t *oam,unsigned who){
    /* The hero's sprite layer; while the game's sprite is hidden (our own
     * art plays: Ukemi, flying kick) the last one seen, not the field
     * default: in water the game lifts the hero above the water, and the
     * default hid the whole tumble behind it (player, 2026-11-27: "ukemi in
     * water renders invisible"). */
    static unsigned last[3]={44u,44u,44u};
    if(!oam)return who<3u?last[who]:44u; /* normal field sprite priority when OAM is unavailable */
    for(unsigned i=0;i<128u;i++){
        const uint8_t *piece=oam+4u*i;
        if(som_bun_hero_piece(piece,who)){
            const unsigned d=32u+((((unsigned)piece[3]>>4)&3u)+1u)*4u;
            if(who<3u)last[who]=d;
            return d;
        }
    }
    return who<3u?last[who]:44u;
}

/* Restore original core pixels wherever its final foreground depth beats
 * the native depth of the actor whose extra artwork was drawn. Only pixels
 * our drawing changed are looked at: whole unchanged rows are skipped with
 * one memcmp (on the Steam Link the per-pixel pass over the full frame cost
 * ~15 ms a frame, 2026-11-27). */
/* base_row: the base's own line length (the core's frame can be used as the
 * base directly when nothing was drawn on the copy before, perf 2026-11-27). */
static inline void som_mask_behind_foreground_base(uint8_t *frame,const uint8_t *base,size_t base_row,
                                               unsigned width,unsigned height,size_t row,
                                               unsigned bytes,const uint8_t *depth,
                                               unsigned depth_pitch,unsigned actor_depth){
    if(!frame||!base||!depth||!depth_pitch||!bytes)return;
    const unsigned sx=width>=512u?2u:1u,sy=height>=400u?2u:1u;
    const size_t line=(size_t)width*bytes;
    for(unsigned y=0;y<height;y++){
        uint8_t *f=frame+(size_t)y*row;
        const uint8_t *b=base+(size_t)y*base_row;
        if(!memcmp(f,b,line))continue;
        const uint8_t *d=depth+(size_t)(y/sy)*depth_pitch;
        if(bytes==2u){
            uint16_t *f16=(uint16_t*)(void*)f;const uint16_t *b16=(const uint16_t*)(const void*)b;
            for(unsigned x=0;x<width;x++)
                if(f16[x]!=b16[x]&&d[sx==1u?x:x>>1]>actor_depth)f16[x]=b16[x];
        }else{
            uint32_t *f32=(uint32_t*)(void*)f;const uint32_t *b32=(const uint32_t*)(const void*)b;
            for(unsigned x=0;x<width;x++)
                if(f32[x]!=b32[x]&&d[sx==1u?x:x>>1]>actor_depth)f32[x]=b32[x];
        }
    }
}
static inline void som_mask_behind_foreground(uint8_t *frame,const uint8_t *base,
                                               unsigned width,unsigned height,size_t row,
                                               unsigned bytes,const uint8_t *depth,
                                               unsigned depth_pitch,unsigned actor_depth){
    som_mask_behind_foreground_base(frame,base,row,width,height,row,bytes,depth,depth_pitch,actor_depth);
}
#endif
