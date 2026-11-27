#ifndef FLXTR_SOM_CREATOR_H
#define FLXTR_SOM_CREATOR_H

#include <stdint.h>
#include "som_overlay.h"
#include "vendor/libretro.h"

typedef struct {
    uint16_t previous;
    uint8_t slot, active, seen, release, skin, layers;
} SomCreator;

enum { SOM_CREATOR_PASS, SOM_CREATOR_CAPTURE, SOM_CREATOR_CONFIRM };
enum { SOM_CREATOR_SKINS=5, SOM_CREATOR_HAIR_OFF=1, SOM_CREATOR_CLOTHES_OFF=2 };
#define SOM_RGB5(r,g,b) ((r)|((g)<<5)|((b)<<10))
static const uint16_t som_creator_skins[SOM_CREATOR_SKINS][3]={
    {0xffff,0xffff,0xffff},
    {SOM_RGB5(22,12,9),SOM_RGB5(27,17,13),SOM_RGB5(31,25,19)},
    {SOM_RGB5(18,10,6),SOM_RGB5(23,15,9),SOM_RGB5(30,22,13)},
    {SOM_RGB5(14,7,4),SOM_RGB5(18,10,6),SOM_RGB5(25,15,9)},
    {SOM_RGB5(8,5,4),SOM_RGB5(12,7,5),SOM_RGB5(18,11,8)}
};
#undef SOM_RGB5
static const char *const som_creator_skin_names[]={"NATIVE","IVORY","GOLDEN","COPPER","UMBER"};

static inline void som_creator_tint(uint8_t palette[32],unsigned slot,unsigned skin){
    if(!skin||skin>=SOM_CREATOR_SKINS)return;
    for(unsigned shade=slot?1u:0u;shade<3u;shade++){
        const unsigned colour=som_creator_skins[skin][shade];
        const unsigned offset=2u*(5u+shade);
        palette[offset]=(uint8_t)colour;
        palette[offset+1u]=(uint8_t)(colour>>8);
    }
}

static inline int som_creator_slot(const uint8_t *ram){
    if(!ram||ram[0xA20F]!=3u||ram[0xA156]!=3u||ram[0xA22B]>=3u)return -1;
    return ram[0xA22B];
}

static inline int som_creator_input(SomCreator *menu,int slot,uint16_t buttons,
                                    unsigned applied){
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
        menu->skin=(uint8_t)applied;
        menu->layers=0;
        return SOM_CREATOR_CAPTURE;
    }
    if(!menu->active)return SOM_CREATOR_PASS;
    if(menu->skin&&(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_UP)))menu->skin--;
    if(menu->skin+1u<SOM_CREATOR_SKINS&&(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN)))menu->skin++;
    if(slot==0&&(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_L)))menu->layers^=SOM_CREATOR_HAIR_OFF;
    if(slot==0&&(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_R)))menu->layers^=SOM_CREATOR_CLOTHES_OFF;
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
                                    unsigned height,size_t pitch,int format,
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
    som_overlay_label(pixels,width,height,pitch,format,som_creator_skin_names[menu->skin],
                      177,72,1,text);
    som_overlay_fill(pixels,pitch,format,145u*sx,88u*sy,64u*sx,2u*sy,mint);
    for(unsigned i=0;i<SOM_CREATOR_SKINS;i++){
        const unsigned x=146u+i*15u;
        som_overlay_fill(pixels,pitch,format,x*sx,85u*sy,3u*sx,8u*sy,i==menu->skin?text:mint);
    }
    som_overlay_label(pixels,width,height,pitch,format,"UP / DOWN: SKIN",177,96,1,mint);
    if(menu->slot==0){
        som_overlay_label(pixels,width,height,pitch,format,menu->layers&SOM_CREATOR_HAIR_OFF?
                          "L: SHOW HAIR":"L: HIDE HAIR",177,112,1,text);
        som_overlay_label(pixels,width,height,pitch,format,menu->layers&SOM_CREATOR_CLOTHES_OFF?
                          "R: SHOW CLOTHES":"R: HIDE CLOTHES",177,127,1,text);
        som_overlay_label(pixels,width,height,pitch,format,"LAYERS: PREVIEW",177,146,1,mint);
    }
    if(native){
        /* The observed Turbo naming preview sits at x=144, y=16 in
         * base coordinates. Retain the native pixels and scale by 2. */
        const unsigned bytes=format==1?4u:2u;
        for(unsigned y=0;y<32;y++)for(unsigned x=0;x<18;x++){
            uint32_t ink=0;
            memcpy(&ink,native+(16u+y)*sy*native_pitch+(143u+x)*sx*bytes,bytes);
            som_overlay_fill(pixels,pitch,format,(38u+2u*x)*sx,(67u+2u*y)*sy,2u*sx,2u*sy,ink);
        }
    }
    som_overlay_label(pixels,width,height,pitch,format,"NATIVE BODY",56,141,1,text);
    som_overlay_label(pixels,width,height,pitch,format,"B CONTINUE   A CANCEL",128,195,1,mint);
}
#endif
