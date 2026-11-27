#ifndef FLXTR_SOM_CREATOR_H
#define FLXTR_SOM_CREATOR_H

#include <stdint.h>
#include "som_overlay.h"
#include "vendor/libretro.h"

typedef struct {
    uint16_t previous;
    uint8_t slot, active, seen, release, external, selected;
} SomCreator;

enum { SOM_CREATOR_PASS, SOM_CREATOR_CAPTURE, SOM_CREATOR_CONFIRM };

static inline int som_creator_slot(const uint8_t *ram){
    if(!ram||ram[0xA20F]!=3u||ram[0xA156]!=3u||ram[0xA22B]>=3u)return -1;
    return ram[0xA22B];
}

static inline int som_creator_input(SomCreator *menu,int slot,uint16_t buttons,
                                    unsigned available,unsigned applied){
    const uint16_t pressed=(uint16_t)(buttons&~menu->previous);
    menu->previous=buttons;
    if(slot<0){
        menu->active=menu->seen=0;
        if(menu->release&&buttons)return SOM_CREATOR_CAPTURE;
        menu->release=0;
        return SOM_CREATOR_PASS;
    }
    if(menu->release){
        if(!buttons)menu->release=0;
        return SOM_CREATOR_CAPTURE;
    }
    if(!menu->seen||menu->slot!=slot||
       (!menu->active&&(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_X)))){
        menu->slot=(uint8_t)slot;
        menu->seen=menu->active=1;
        menu->external=(available>>slot)&1u;
        menu->selected=(applied>>slot)&1u;
        return SOM_CREATOR_CAPTURE;
    }
    if(!menu->active)return SOM_CREATOR_PASS;
    if(menu->external&&(pressed&((1u<<RETRO_DEVICE_ID_JOYPAD_LEFT)|
                                (1u<<RETRO_DEVICE_ID_JOYPAD_RIGHT))))
        menu->selected^=1u;
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_A)){
        menu->active=0;
        menu->release=1;
        return SOM_CREATOR_CAPTURE;
    }
    if(pressed&((1u<<RETRO_DEVICE_ID_JOYPAD_B)|(1u<<RETRO_DEVICE_ID_JOYPAD_START))){
        menu->active=0;
        menu->release=1;
        return SOM_CREATOR_CONFIRM;
    }
    return SOM_CREATOR_CAPTURE;
}

static inline void som_creator_draw(const SomCreator *menu,uint8_t *pixels,unsigned width,
                                    unsigned height,size_t pitch,int format,const uint8_t *tile,
                                    const uint8_t *native,size_t native_pitch){
    const unsigned sx=width>=512u?2u:1u,sy=height>=400u?2u:1u;
    if(width<256u*sx||height<224u*sy)return;
    const uint32_t back=som_overlay_color(format,13,26,32);
    const uint32_t panel=som_overlay_color(format,27,48,53);
    const uint32_t mint=som_overlay_color(format,132,225,181);
    const uint32_t text=som_overlay_color(format,235,239,218);
    som_overlay_fill(pixels,pitch,format,0,0,width,height,back);
    som_overlay_fill(pixels,pitch,format,12*sx,37*sy,88*sx,142*sy,panel);
    som_overlay_fill(pixels,pitch,format,110*sx,37*sy,134*sx,142*sy,panel);
    som_overlay_label(pixels,width,height,pitch,format,"CHARACTER CREATOR",128,17,1,mint);
    som_overlay_label(pixels,width,height,pitch,format,"APPEARANCE",177,52,1,mint);
    static const char *const actors[]={"RANDI SLOT","PRIMM SLOT","POPOI SLOT"};
    som_overlay_label(pixels,width,height,pitch,format,actors[menu->slot],56,52,1,text);
    som_overlay_label(pixels,width,height,pitch,format,menu->selected?"< ATLAS >":"< NATIVE >",
                      177,89,1,text);
    som_overlay_label(pixels,width,height,pitch,format,menu->external?"LEFT / RIGHT":"NATIVE APPEARANCE",
                      177,115,1,mint);
    som_overlay_label(pixels,width,height,pitch,format,"NAME ENTRY NEXT",177,151,1,text);
    if(menu->selected&&tile){
        for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
            const unsigned ink=tile[y*8u+x];
            if(ink){
                const uint32_t c=som_overlay_color(format,70u+8u*ink,120u+6u*ink,100u+5u*ink);
                som_overlay_fill(pixels,pitch,format,(36u+5u*x)*sx,(81u+5u*y)*sy,5u*sx,5u*sy,c);
            }
        }
    }
    if(!menu->selected&&native){
        /* The observed Turbo naming preview sits at x=144, y=16 in
         * base coordinates. Retain the native pixels and scale by 2. */
        const unsigned bytes=format==1?4u:2u;
        for(unsigned y=0;y<32;y++)for(unsigned x=0;x<18;x++){
            uint32_t ink=0;
            memcpy(&ink,native+(16u+y)*sy*native_pitch+(143u+x)*sx*bytes,bytes);
            som_overlay_fill(pixels,pitch,format,(38u+2u*x)*sx,(67u+2u*y)*sy,2u*sx,2u*sy,ink);
        }
    }
    som_overlay_label(pixels,width,height,pitch,format,menu->selected?"ATLAS TILE":"ORIGINAL",
                      56,141,1,text);
    som_overlay_label(pixels,width,height,pitch,format,"B CONTINUE   A CANCEL",128,195,1,mint);
}
#endif
