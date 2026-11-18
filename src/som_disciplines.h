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
/* What X does per class (docs/SOM-CLASS-ASSESSMENT.md). Spells for the
 * casters and the hybrids; Bushido (samurai) has no magic: X is Iaijutsu,
 * a straight charge level 3 strike with the katana (hold X: level 4), one
 * per SOM_IAI_COOLDOWN_FRAMES; Martial Arts (monk): X is the flying kick,
 * a run and a kick (som_ai_proxy.c som_flykick_start); Weapon Flow (weaponsmaster) has no magic:
 * X attacks with a third weapon, som_disc_third_weapon. */
enum {SOM_X_SPELLS, SOM_X_IAI, SOM_X_THIRD_WEAPON, SOM_X_FLYING_KICK};
#define SOM_DISC_MARTIAL_ARTS 1u
#define SOM_DISC_WEAPON_FLOW 8u
#define SOM_DISC_BUSHIDO 10u
#define SOM_IAI_LEVEL 3u
#define SOM_IAI_HELD_LEVEL 4u
#define SOM_IAI_COOLDOWN_FRAMES 180ul        /* 3 s */
#define SOM_NO_WEAPON 0xFFu
static inline unsigned som_disc_x_role(unsigned discipline){
    return discipline==SOM_DISC_BUSHIDO?SOM_X_IAI:
           discipline==SOM_DISC_MARTIAL_ARTS?SOM_X_FLYING_KICK:
           discipline==SOM_DISC_WEAPON_FLOW?SOM_X_THIRD_WEAPON:SOM_X_SPELLS;
}
/* Weapon Flow: sword (B), spear (Y), bow (X): short, reach, long. */
static inline unsigned som_disc_third_weapon(unsigned discipline){
    return discipline==SOM_DISC_WEAPON_FLOW?5u:SOM_NO_WEAPON;
}
/* Dodges, driven by the proxy. The game's own poses (back roll 0x8D, leap
 * 0x9E, hop 0x95) do not play in field play: written as +0x1C/+0x11 they
 * end within 6 frames without moving (headless Steam Link test,
 * 2026-11-15; their scripts run under the event VM). So the proxy carries
 * the hero `speed` px per frame for `frames` frames onto open ground,
 * keeps him untouchable a little longer, and draws an after-image trail.
 * forward: toward the facing (else away from it) when no direction is
 * held. speed 0: this class has no dodge.
 *
 * Kawarimi no Jutsu (substitution) is no longer the evade: the AI arms it
 * with its own command (som_ai_proxy.c som_kawarimi_arm) and it then fires
 * when a monster touches or hits the hero, carrying him speed * frames px
 * away from the monster. Shunshin no Jutsu (ninja + sword) has its own
 * command too (som_shunshin_start). */
/* Weapons by range (7E:E1E4 ids, som_weapon_types order; the bridge's
 * table is tools/som_weapon_ranges.py). Short: glove 0, sword 1, axe 2,
 * spear 3. Long: whip 4, bow 5, boomerang 6, javelin 7. */
#define SOM_WEAPON_TYPES 8u
static inline int som_weapon_long_range(unsigned weapon){return weapon>=4u&&weapon<SOM_WEAPON_TYPES;}
static inline int som_weapon_short_range(unsigned weapon){return weapon<4u;}
/* cartwheel: the proxy draws the hero turning a cartwheel (tools/som_cartwheel.py
 * frames) over the hidden hero instead of the after-image hop. */
typedef struct { unsigned speed, frames; int forward; const char *name; int cartwheel; } SomDodge;
#define SOM_DODGE_GRACE 8u                 /* untouchable frames after the move */
#define SOM_KAWARIMI_NAME "Kawarimi no Jutsu"
static inline SomDodge som_disc_dodge(unsigned discipline,unsigned weapon){
    SomDodge d={0u,0u,0,"",0};
    (void)weapon;
    switch(discipline){
    case 0: case 10: d.speed=3u;d.frames=16u;d.name="Evade";break;        /* 48 px roll */
    case 1: case 9:                    /* monk, rogue: 64 px cartwheel */
        d.speed=4u;d.frames=16u;d.name="Cartwheel";d.cartwheel=1;break;
    case 11: d.speed=3u;d.frames=18u;d.name="Evade";break;                /* 54 px back */
    case 2: d.speed=3u;d.frames=16u;d.name="Evade";break;                 /* 48 px */
    default:break;
    }
    return d;
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
    case 3: return SOM_PAL_MAGE_ARCANE;
    case 4: return SOM_PAL_DIVINE_GOLD;
    case 5: return SOM_PAL_DARK_CRIMSON;
    case 6: return SOM_PAL_SUMMONER_WHITE;
    case 7: return SOM_PAL_DANCER_ROSE;
    case 8: return SOM_PAL_FLOW_TEAL;
    case 9: return SOM_PAL_ROGUE_SHADOW;
    case 10: return SOM_PAL_SAMURAI_INDIGO;
    case 11: return SOM_PAL_ARCHER_FOREST;
    default:return SOM_PAL_ORIGINAL;
    }
}
/* Short class title shown under the hero on the field; NULL with no class. */
static const char *const som_discipline_titles[SOM_DISCIPLINE_COUNT]={
    "Swordsman","Monk","Ninja","Mage","Cleric","Warlock",
    "Summoner","Dancer","Weaponmaster","Rogue","Samurai","Archer"
};
static inline const char *som_disc_title(const SomDisciplines *d){
    return d&&d->selected<SOM_DISCIPLINE_COUNT?som_discipline_titles[d->selected]:NULL;
}
/* The boy's HUD portrait: a 16x16 OBJ in the bottom HUD, palette 1 (his
 * colours), tile 0x1E8-0x1EF (seen at x 88, y 199 alone in the party).
 * Returns 1 with its top-left in *x, *y. No portrait (HUD hidden, menus,
 * events): 0, and no class label. */
static inline int som_hud_portrait(const uint8_t *ram,int *x,int *y){
    if(!ram)return 0;
    for(unsigned i=0;i<128u;i++){
        const uint8_t *e=ram+0x0800u+4u*i;
        const unsigned tile=e[2]|((unsigned)(e[3]&1u)<<8);
        if(e[1]<160u||e[1]>=0xE0u||((e[3]>>1)&7u)!=1u||tile<0x1E8u||tile>0x1EFu)continue;
        const unsigned hb=(ram[0x0A00u+i/4u]>>(2u*(i%4u)))&3u;
        if(hb&2u)continue;
        int px=(int)(e[0]|((hb&1u)<<8));if(px>=256)px-=512;
        *x=px;*y=(int)e[1];
        return 1;
    }
    return 0;
}
/* The class title sits right of the portrait, under the HP numbers. */
#define SOM_HUD_LABEL_DX 18
#define SOM_HUD_LABEL_DY 9
/* Pixels below the hero's position (base SNES pixels) for the label top. */
#define SOM_CLASS_LABEL_DROP_PX 3
/* The class title, centred under the hero at base screen position
 * (hero_x, hero_y). Returns 1 when drawn. */
static inline int som_disc_draw_label(uint8_t *pixels,unsigned width,unsigned height,
                                      size_t pitch,int format,const char *text,
                                      int hero_x,int hero_y){
    if(hero_x<0||hero_y<0)return 0;
    return som_overlay_label(pixels,width,height,pitch,format,text,
                             hero_x,hero_y+SOM_CLASS_LABEL_DROP_PX,1,
                             som_overlay_color(format,255,255,255));
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
