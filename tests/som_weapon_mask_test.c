/* Runtime Bushido weapon masking: true opaque pixels and quarter-alpha FX. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "som_overlay.h"
#include "som_npc_work.h"
#include "som_weapon_mask.h"

static unsigned channel(uint32_t p,unsigned shift,unsigned mask){
    return (p>>shift)&mask;
}
int main(void){
    for(int fmt=0;fmt<=2;fmt++){
        const uint32_t bg=som_overlay_color(fmt,80u,120u,160u);
        const uint32_t fg=som_overlay_color(fmt,248u,32u,64u);
        const uint32_t mix=som_mask_mix25(bg,fg,fmt);
        const unsigned red_shift=fmt==1?16u:(fmt==2?11u:10u);
        const unsigned green_shift=fmt==1?8u:5u;
        const unsigned blue_mask=fmt==1?255u:31u;
        const unsigned green_mask=fmt==1?255u:(fmt==2?63u:31u);
        const unsigned red_mask=fmt==1?255u:31u;
        const unsigned shifts[3]={0u,green_shift,red_shift};
        const unsigned masks[3]={blue_mask,green_mask,red_mask};
        for(unsigned c=0;c<3u;c++){
            const unsigned a=channel(bg,shifts[c],masks[c]),b=channel(fg,shifts[c],masks[c]);
            assert(channel(mix,shifts[c],masks[c])==(3u*a+b+2u)/4u);
        }
        uint8_t buffer[48*48*4];
        memset(buffer,0,sizeof(buffer));
        const size_t pitch=fmt==1?48u*4u:48u*2u;
        uint8_t ram[0x20000];
        memset(ram,0,sizeof(ram));
        ram[SOM_NPC_OBJECT(0)+2]=20u;
        ram[SOM_NPC_OBJECT(0)+4]=20u;
        /* Two consecutive mask pixels: fully opaque blade, then 25% FX. */
        const uint8_t record[]={1,0,0,2,1,1,6};
        SomNpcWorkState sprite;
        memset(&sprite,0,sizeof(sprite));
        sprite.active=1;sprite.entry.first=record;
        sprite.entry.colours[0]=0x001Fu; /* red in BGR555 */
        const uint32_t under=som_overlay_color(fmt,0,0,120);
        som_overlay_put(buffer,pitch,fmt,20,20,under);
        som_overlay_put(buffer,pitch,fmt,21,20,under);
        assert(som_weapon_mask_draw(&sprite,ram,buffer,48,48,pitch,fmt));
        const uint32_t solid=fmt==1?
             ((uint32_t*)(void*)(buffer+20*pitch))[20]:
             ((uint16_t*)(void*)(buffer+20*pitch))[20];
        const uint32_t faded=fmt==1?
             ((uint32_t*)(void*)(buffer+20*pitch))[21]:
             ((uint16_t*)(void*)(buffer+20*pitch))[21];
        assert(solid==som_overlay_color(fmt,255,0,0));
        assert(faded==som_mask_mix25(under,solid,fmt));
    }
    puts("Bushido mask compositor: PASS");
    return 0;
}
