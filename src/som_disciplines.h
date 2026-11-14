/* Classless SoM disciplines, independent of native WRAM and gambits.
 * Selection is a visual/combat preference, NOT a restriction on skill growth.
 * No experience is awarded without a verified, distinct gameplay event.
 */
#ifndef FLXTR_SOM_DISCIPLINES_H
#define FLXTR_SOM_DISCIPLINES_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vendor/libretro.h"
#include "som_overlay.h"
#include "som_class_palette.h"

#define SOM_DISCIPLINE_COUNT 12u
#define SOM_DISCIPLINE_NONE 255u
static const char *const som_discipline_names[SOM_DISCIPLINE_COUNT]={
    "Swordsmanship","Martial Arts","Ninjutsu","Magic","Divine Arts",
    "Dark Arts","Summoning","Support & Dance","Weapon Flow",
    "Roguecraft","Bushido","Archery"
};
/* Who gets the dodge roll (the game's somersault; planned, see
 * docs/SOM-DISCIPLINES-CONTROLS.md): swordsman, monk, ninja, rogue,
 * samurai and archer only. */
static inline int som_disc_has_dodge(unsigned discipline){
    switch(discipline){
    case 0: case 1: case 2: case 9: case 10: case 11: return 1;
    default: return 0;
    }
}
typedef struct {
    int open;
    unsigned cursor;
    uint8_t selected; /* SOM_DISCIPLINE_NONE means no specialization */
    uint16_t level[SOM_DISCIPLINE_COUNT];
    uint32_t experience[SOM_DISCIPLINE_COUNT];
    uint64_t last_event[SOM_DISCIPLINE_COUNT];
} SomDisciplines;
enum {SOM_DISC_NONE, SOM_DISC_OPENED, SOM_DISC_CLOSED, SOM_DISC_SELECTED};
static inline unsigned som_disc_visual_profile(const SomDisciplines *d){
    if(!d)return SOM_PAL_ORIGINAL;
    switch(d->selected){
    case 0: return SOM_PAL_SWORDMASTER_FROST;
    case 1: return SOM_PAL_MONK_CRIMSON_GOLD;
    case 2: return SOM_PAL_NINJA_MIDNIGHT;
    case 6: return SOM_PAL_SUMMONER_WHITE;
    case 9: return SOM_PAL_ROGUE_SHADOW;
    case 10: return SOM_PAL_SAMURAI_INDIGO;
    case 11: return SOM_PAL_ARCHER_FOREST;
    default:return SOM_PAL_ORIGINAL;
    }
}
static inline void som_disc_init(SomDisciplines *d){
    memset(d,0,sizeof(*d));
    d->selected=SOM_DISCIPLINE_NONE;
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++)d->level[i]=1u;
}
static inline uint32_t som_disc_next_xp(unsigned level){
    return 100u+25u*(level>100u?100u:level-1u);
}
/* Verified event IDs must be stable and distinct across events.
 * A single event cannot award experience twice within a discipline. */
static inline int som_disc_award(SomDisciplines *d,unsigned skill,unsigned amount,
                                  uint64_t event_id,int verified){
    if(!d||skill>=SOM_DISCIPLINE_COUNT||!verified||!event_id||!amount||
       d->last_event[skill]==event_id)return 0;
    d->last_event[skill]=event_id;
    if(d->level[skill]>=100u)return 0;
    if(amount>100u)amount=100u;
    d->experience[skill]+=amount;
    while(d->level[skill]<100u&&
          d->experience[skill]>=som_disc_next_xp(d->level[skill])){
        d->experience[skill]-=som_disc_next_xp(d->level[skill]);
        d->level[skill]++;
    }
    return 1;
}
/* User-facing menu only. L/R changes page outside this function. */
static inline int som_disc_press(SomDisciplines *d,uint16_t pressed){
    if(!pressed)return SOM_DISC_NONE;
    if(!d->open){d->open=1;d->cursor=0;return SOM_DISC_OPENED;}
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_UP))
        d->cursor=(d->cursor+SOM_DISCIPLINE_COUNT-1u)%SOM_DISCIPLINE_COUNT;
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN))
        d->cursor=(d->cursor+1u)%SOM_DISCIPLINE_COUNT;
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_B)){
        d->selected=d->selected==d->cursor?SOM_DISCIPLINE_NONE:(uint8_t)d->cursor;
        return SOM_DISC_SELECTED;
    }
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_A)){
        d->open=0;return SOM_DISC_CLOSED;
    }
    return SOM_DISC_NONE;
}
static inline int som_disc_line(const SomDisciplines *d,unsigned line,char out[33]){
    if(line==0){snprintf(out,33,"DISCIPLINES L/R:tab A:close");return 1;}
    if(line>SOM_DISCIPLINE_COUNT)return 0;
    unsigned i=line-1u;
    snprintf(out,33,"%c%c %-17s Lv%3u",
        d->cursor==i?'>':' ',d->selected==i?'*':' ',
        som_discipline_names[i],(unsigned)d->level[i]);
    return 1;
}
static inline void som_disc_draw(uint8_t *pixels,unsigned width,unsigned height,
                                size_t pitch,int format,const SomDisciplines *d){
    if(!d->open||!pixels)return;
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    const unsigned lines=SOM_DISCIPLINE_COUNT+2u;
    const unsigned bw=195u*sx,bh=(lines*10u+8u)*sy;
    if(bw+12u*sx>width||bh+12u*sy>height)return;
    unsigned x0=6u*sx,y0=6u*sy;
    som_overlay_fill(pixels,pitch,format,x0,y0,bw,bh,som_overlay_color(format,245,245,255));
    som_overlay_fill(pixels,pitch,format,x0+sx,y0+sy,bw-2u*sx,bh-2u*sy,
                     som_overlay_color(format,24,21,73));
    char out[33];
    for(unsigned line=0;line<lines-1u;line++){
        if(!som_disc_line(d,line,out))break;
        for(unsigned i=0;out[i];i++){
            unsigned char c=(unsigned char)out[i];
            if(c<32u||c>126u)continue;
            const uint8_t *glyph=som_overlay_font[c-32u];
            for(unsigned row=0;row<7;row++)for(unsigned col=0;col<5;col++){
                if(!(glyph[row]&(0x10u>>col)))continue;
                som_overlay_fill(pixels,pitch,format,
                    x0+(5u+i*6u+col)*sx,y0+(4u+line*10u+row)*sy,sx,sy,
                    som_overlay_color(format,255,255,255));
            }
        }
    }
    /* Visual preview only. Native hero palettes and saves stay untouched. */
    unsigned profile=som_disc_visual_profile(d);
    const unsigned shades[4]={6u,13u,22u,31u};
    for(unsigned i=0;i<4u;i++){
        uint16_t color=som_pal_color(som_pal_rgb(shades[i],shades[i],shades[i]),profile);
        unsigned red=(color&31u)*255u/31u;
        unsigned green=((color>>5)&31u)*255u/31u;
        unsigned blue=((color>>10)&31u)*255u/31u;
        som_overlay_fill(pixels,pitch,format,x0+(6u+i*17u)*sx,
            y0+(4u+(lines-1u)*10u)*sy,14u*sx,7u*sy,
            som_overlay_color(format,(uint8_t)red,(uint8_t)green,(uint8_t)blue));
    }
}
#endif
