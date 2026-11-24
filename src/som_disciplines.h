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

#define SOM_DISCIPLINE_COUNT 16u
#define SOM_DISCIPLINE_NONE 255u
static const char *const som_discipline_names[SOM_DISCIPLINE_COUNT]={
    "Swordsmanship","Martial Arts","Ninjutsu","Magic","Divine Arts",
    "Dark Arts","Summoning","Support & Dance","Weapon Flow",
    "Roguecraft","Bushido","Archery",
    "Vampire Hunting","Necromancy","Artifice","Psionics"
};
/* The classes added 2026-11-20 (player's ideas): a whip-and-cross vampire
 * hunter, a corpse-raising necromancer, a bomb-and-mine artificer and a
 * mind-controlling mentalist. */
#define SOM_DISC_CLERIC 4u
#define SOM_DISC_HUNTER 12u
#define SOM_DISC_NECRO 13u
#define SOM_DISC_ARTIFICER 14u
#define SOM_DISC_PSIONICS 15u
/* Who gets the dodge roll (the game's somersault; planned, see
 * docs/SOM-DISCIPLINES-CONTROLS.md): swordsman, monk, ninja, rogue,
 * samurai and archer only. */
static inline int som_disc_has_dodge(unsigned discipline){
    switch(discipline){
    case 0: case 1: case 2: case 3: case 9: case 10: case 11: case 12: case 14:
        return 1;                                   /* 3: Blink step */
    default: return 0;
    }
}
/* What X does per class (docs/SOM-CLASS-ASSESSMENT.md). Spells for the
 * casters and the hybrids; Bushido (samurai) has no magic: X is Iaijutsu,
 * a straight charge level 3 strike with the katana (hold X: level 4), one
 * per SOM_IAI_COOLDOWN_FRAMES; Martial Arts (monk): X is the flying kick,
 * a run and a kick (som_ai_proxy.c som_flykick_start); Weapon Flow (weaponsmaster) has no magic:
 * X attacks with a third weapon, som_disc_third_weapon. */
enum {SOM_X_SPELLS, SOM_X_IAI, SOM_X_THIRD_WEAPON, SOM_X_FLYING_KICK,
      SOM_X_HOLY_WATER, SOM_X_RAISE_DEAD, SOM_X_BOMB, SOM_X_MIND_CONTROL, SOM_X_SUMMON};
#define SOM_DISC_MARTIAL_ARTS 1u
#define SOM_DISC_WEAPON_FLOW 8u
#define SOM_DISC_BUSHIDO 10u
#define SOM_IAI_LEVEL 3u
#define SOM_IAI_HELD_LEVEL 4u
#define SOM_IAI_COOLDOWN_FRAMES 180ul        /* 3 s */
#define SOM_NO_WEAPON 0xFFu
static inline unsigned som_disc_x_role(unsigned discipline){
    return discipline==5u||discipline==6u?SOM_X_SUMMON:   /* Dark Arts, Summoning */
           discipline==12u?SOM_X_HOLY_WATER:discipline==13u?SOM_X_RAISE_DEAD:
           discipline==14u?SOM_X_BOMB:discipline==15u?SOM_X_MIND_CONTROL:
           discipline==SOM_DISC_BUSHIDO?SOM_X_IAI:
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
/* cartwheel: the game's own cartwheel, its miss reaction (pose 0x93, 25 px),
 * on combat maps; speed/frames are the hop used where the game refuses it. */
/* cartwheel: the game reaction (E1E5) that plays the dodge natively, 0 =
 * none: 0x13 the miss reaction's cartwheel (pose 0x93), 0x0D the roll of a
 * knock-down (pose 0x8D, "rolling over and getting up", som-decomp
 * hero-poses.md 3), used as Ukemi. */
#define SOM_REACT_CARTWHEEL 0x13
#define SOM_REACT_UKEMI 0x0D
typedef struct { unsigned speed, frames; int forward; const char *name; int cartwheel; } SomDodge;
#define SOM_DODGE_GRACE 8u                 /* untouchable frames after the move */
#define SOM_KAWARIMI_NAME "Kawarimi no Jutsu"
static inline SomDodge som_disc_dodge(unsigned discipline,unsigned weapon){
    SomDodge d={0u,0u,0,"",0};
    (void)weapon;
    switch(discipline){
    /* Short and quick (player, 2026-11-22: 48-64 px went too far, broke
     * the game and looked stiff): about the game's own cartwheel, 24-30 px,
     * at 2 px a frame; a drawn pose then plays to its end in place. */
    case 1: case 2: case 10:           /* monk, ninja, samurai: Ukemi, 28 px tumble */
        d.speed=2u;d.frames=14u;d.name="Ukemi";break;
    case 0: case 9:                    /* swordsman, rogue: cartwheel, else 28 px */
        d.speed=2u;d.frames=14u;d.name="Cartwheel";d.cartwheel=SOM_REACT_CARTWHEEL;break;
    case 11: d.speed=2u;d.frames=13u;d.name="Evade";break;                /* 26 px back */
    case 3: d.speed=6u;d.frames=5u;d.name="Blink step";break;            /* mage: 30 px, hidden */
    case 12: d.speed=2u;d.frames=14u;d.name="Backflip";break;            /* hunter: 28 px back */
    case 14: d.speed=2u;d.frames=12u;d.name="Evade";break;               /* artificer: 24 px */
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
    /* The companions' classes (player, 2026-11-20): party[0] the girl,
     * party[1] the sprite. Levels are the party's, shared with the boy.
     * who: whose class the menu shows (0 boy, 1 girl, 2 sprite). */
    uint8_t party[2];
    uint8_t who;
    /* The player's class order (2026-11-22): menu row r shows class
     * order[r], and L2+R1/L1 step through the classes in this order. The
     * cursor is a row. grab: Y picked the cursor's row up, Up/Down carry it. */
    uint8_t order[SOM_DISCIPLINE_COUNT];
    uint8_t grab;
} SomDisciplines;
#define SOM_PARTY_MEMBERS 3u
#define SOM_GIRL_DEFAULT_CLASS 4u       /* Divine Arts: she heals */
#define SOM_SPRITE_DEFAULT_CLASS 3u     /* Magic: he casts the attack spells */
static const char *const som_party_names[SOM_PARTY_MEMBERS]={"BOY","GIRL","SPRITE"};
enum {SOM_DISC_NONE, SOM_DISC_OPENED, SOM_DISC_CLOSED, SOM_DISC_SELECTED,
      SOM_DISC_WHO, SOM_DISC_PARTY_SELECTED, SOM_DISC_ORDERED};
/* Member 0 the boy, 1 the girl, 2 the sprite: that member's class. */
static inline unsigned som_disc_member_class(const SomDisciplines *d,unsigned who){
    if(!d)return SOM_DISCIPLINE_NONE;
    return who==0u?d->selected:who<SOM_PARTY_MEMBERS?d->party[who-1u]:SOM_DISCIPLINE_NONE;
}
static inline unsigned som_disc_class_profile(unsigned cls){
    switch(cls){
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
    case 12: return SOM_PAL_HUNTER_LEATHER;
    case 13: return SOM_PAL_NECRO_BONE;
    case 14: return SOM_PAL_ARTIFICER_BRASS;
    case 15: return SOM_PAL_PSION_VIOLET;
    default:return SOM_PAL_ORIGINAL;
    }
}
static inline unsigned som_disc_visual_profile(const SomDisciplines *d){
    return d?som_disc_class_profile(d->selected):SOM_PAL_ORIGINAL;
}
/* Short class title shown under the hero on the field; NULL with no class. */
static const char *const som_discipline_titles[SOM_DISCIPLINE_COUNT]={
    "Swordsman","Monk","Ninja","Mage","Cleric","Warlock",
    "Summoner","Dancer","Weaponmaster","Rogue","Samurai","Archer",
    "Hunter","Necromancer","Artificer","Mentalist"
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
/* A party member's HUD portrait (who 0 boy, 1 girl, 2 sprite): tile
 * 0x1E8 + 2*who in palette who + 1 (live OAM 2026-11-20: x 8, 88, 168,
 * y 199 before the lift). The boy alone sits in the middle. */
static inline int som_hud_member_portrait(const uint8_t *ram,unsigned who,int *x,int *y){
    if(!ram||who>2u)return 0;
    for(unsigned i=0;i<128u;i++){
        const uint8_t *e=ram+0x0800u+4u*i;
        const unsigned tile=e[2]|((unsigned)(e[3]&1u)<<8);
        if(e[1]<160u||e[1]>=0xE0u||((e[3]>>1)&7u)!=who+1u||tile<0x1E8u||tile>0x1EFu)continue;
        const unsigned hb=(ram[0x0A00u+i/4u]>>(2u*(i%4u)))&3u;
        if(hb&2u)continue;
        int px=(int)(e[0]|((hb&1u)<<8));if(px>=256)px-=512;
        *x=px;*y=(int)e[1];
        return 1;
    }
    return 0;
}
/* Leave room below the portrait and both native stat rows. */
#define SOM_HUD_LABEL_DY 17
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
    d->party[0]=SOM_GIRL_DEFAULT_CLASS;d->party[1]=SOM_SPRITE_DEFAULT_CLASS;
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++){d->level[i]=1u;d->order[i]=(uint8_t)i;}
}
/* order[] holds every class exactly once. */
static inline int som_disc_order_valid(const uint8_t *order){
    unsigned seen=0;
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++){
        if(order[i]>=SOM_DISCIPLINE_COUNT||(seen&(1u<<order[i])))return 0;
        seen|=1u<<order[i];
    }
    return 1;
}
/* The row that shows class cls (no class: row 0). */
static inline unsigned som_disc_row_of(const SomDisciplines *d,unsigned cls){
    for(unsigned r=0;r<SOM_DISCIPLINE_COUNT;r++)if(d->order[r]==cls)return r;
    return 0u;
}
static inline unsigned som_disc_cursor_class(const SomDisciplines *d){
    return d->order[d->cursor%SOM_DISCIPLINE_COUNT];
}
/* The class after (dir > 0) or before cur in the player's order; from no
 * class, the first (or the last). */
static inline unsigned som_disc_order_step(const SomDisciplines *d,unsigned cur,int dir){
    const unsigned n=SOM_DISCIPLINE_COUNT;
    if(cur>=n)return d->order[dir>0?0u:n-1u];
    const unsigned r=som_disc_row_of(d,cur);
    return d->order[(r+(dir>0?1u:n-1u))%n];
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
/* User-facing menu only. L/R changes page outside this function.
 * Y picks the cursor's class up; Up/Down then carry it through the list,
 * and Y, B or A put it down (the order is the caller's to save). */
static inline int som_disc_press(SomDisciplines *d,uint16_t pressed){
    if(!pressed)return SOM_DISC_NONE;
    if(!d->open){d->open=1;d->cursor=0;d->who=0;d->grab=0;return SOM_DISC_OPENED;}
    if(d->who>=SOM_PARTY_MEMBERS)d->who=0;
    const unsigned n=SOM_DISCIPLINE_COUNT;
    if(d->cursor>=n)d->cursor=0;
    if(d->grab){
        if(pressed&((1u<<RETRO_DEVICE_ID_JOYPAD_UP)|(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN))){
            const unsigned to=(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_UP))?
                (d->cursor+n-1u)%n:(d->cursor+1u)%n;
            const uint8_t carried=d->order[d->cursor];
            d->order[d->cursor]=d->order[to];d->order[to]=carried;
            d->cursor=to;
            return SOM_DISC_NONE;
        }
        if(pressed&((1u<<RETRO_DEVICE_ID_JOYPAD_Y)|(1u<<RETRO_DEVICE_ID_JOYPAD_B)|
                    (1u<<RETRO_DEVICE_ID_JOYPAD_A)|(1u<<RETRO_DEVICE_ID_JOYPAD_X))){
            d->grab=0;return SOM_DISC_ORDERED;
        }
        return SOM_DISC_NONE;
    }
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_Y)){d->grab=1;return SOM_DISC_NONE;}
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_X)){     /* X: next member */
        d->who=(uint8_t)((d->who+1u)%SOM_PARTY_MEMBERS);
        return SOM_DISC_WHO;
    }
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_UP))d->cursor=(d->cursor+n-1u)%n;
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_DOWN))d->cursor=(d->cursor+1u)%n;
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_B)){
        const uint8_t cls=d->order[d->cursor];
        if(d->who){
            uint8_t *c=&d->party[d->who-1u];
            *c=*c==cls?SOM_DISCIPLINE_NONE:cls;
            return SOM_DISC_PARTY_SELECTED;
        }
        d->selected=d->selected==cls?SOM_DISCIPLINE_NONE:cls;
        return SOM_DISC_SELECTED;
    }
    if(pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_A)){
        d->open=0;return SOM_DISC_CLOSED;
    }
    return SOM_DISC_NONE;
}
static inline int som_disc_line(const SomDisciplines *d,unsigned line,char out[33]){
    const unsigned who=d->who<SOM_PARTY_MEMBERS?d->who:0u;
    if(line==0){snprintf(out,33,"%s CLASS X:who L/R A:close",som_party_names[who]);return 1;}
    if(line>SOM_DISCIPLINE_COUNT)return 0;
    const unsigned row=line-1u,i=d->order[row]%SOM_DISCIPLINE_COUNT;
    snprintf(out,33,"%c%c %-17s Lv%3u",
        d->cursor==row?(d->grab?'=':'>'):' ',som_disc_member_class(d,who)==i?'*':' ',
        som_discipline_names[i],(unsigned)d->level[i]);
    return 1;
}
/* Beveled silver frames and an open green interior, like the Status screen. */
static inline void som_disc_window(uint8_t *pixels,unsigned width,unsigned height,
                                  size_t pitch,int format,unsigned x,unsigned y,
                                  unsigned w,unsigned h){
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    const uint32_t shades[]={
        som_overlay_color(format,15,26,24),
        som_overlay_color(format,139,149,154),
        som_overlay_color(format,229,231,233),
        som_overlay_color(format,87,101,109)
    };
    for(unsigned i=0;i<4;i++){
        const unsigned right=x+w-1u-i,bottom=y+h-1u-i;
        som_overlay_fill(pixels,pitch,format,(x+i)*sx,(y+i)*sy,(w-2u*i)*sx,sy,shades[i]);
        som_overlay_fill(pixels,pitch,format,(x+i)*sx,bottom*sy,(w-2u*i)*sx,sy,shades[i]);
        som_overlay_fill(pixels,pitch,format,(x+i)*sx,(y+i)*sy,sx,(h-2u*i)*sy,shades[i]);
        som_overlay_fill(pixels,pitch,format,right*sx,(y+i)*sy,sx,(h-2u*i)*sy,shades[i]);
    }
}

static inline void som_ring_classes_title(uint8_t *pixels,unsigned width,unsigned height,
                                          size_t pitch,int format){
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    if(width<256u*sx||height<224u*sy)return;
    som_overlay_fill(pixels,pitch,format,16u*sx,15u*sy,224u*sx,15u*sy,
                     som_overlay_color(format,0,0,88));
    som_overlay_label(pixels,width,height,pitch,format,"Classes",19,19,0,
                      som_overlay_color(format,255,255,255));
}

/* The native Status screen uploads these 32 tiles unchanged to VRAM and
 * renders them in high resolution using the adjacent BGR555 palette. */
typedef struct {
    uint8_t pixels[32][64];
    uint16_t palette[16];
} SomStatusTexture;

static inline int som_status_texture_load(SomStatusTexture *texture,const uint8_t *rom,size_t size){
    const unsigned palette_base=0x07FBE0u,tile_base=0x07FC00u;
    const uint8_t prefix[]={0x00,0x00,0x2C,0x26,0x2A,0x1E,0xC7,0x19};
    const uint8_t tile_rows[]={0,2,1,3};
    if(size<tile_base+32u*32u||memcmp(rom+palette_base,prefix,sizeof(prefix)))return 0;
    for(unsigned i=0;i<16u;i++)
        texture->palette[i]=(uint16_t)(rom[palette_base+2u*i]|(rom[palette_base+2u*i+1u]<<8));
    for(unsigned y=0;y<32u;y++)
        for(unsigned x=0;x<64u;x++){
            const uint8_t *tile=rom+tile_base+32u*(tile_rows[y/8u]*8u+x/8u)+2u*(y&7u);
            const unsigned shift=7u-(x&7u);
            texture->pixels[y][x]=(uint8_t)(((tile[0]>>shift)&1u)|(((tile[1]>>shift)&1u)<<1)|
                (((tile[16]>>shift)&1u)<<2)|(((tile[17]>>shift)&1u)<<3));
        }
    return 1;
}

static inline void som_status_texture_draw(const SomStatusTexture *texture,uint8_t *pixels,
                                           size_t pitch,int format,unsigned sx,unsigned sy){
    uint32_t palette[16];
    for(unsigned i=0;i<16u;i++){
        const uint16_t color=texture->palette[i];
        palette[i]=som_overlay_color(format,(color&31u)<<3,((color>>5)&31u)<<3,((color>>10)&31u)<<3);
    }
    for(unsigned y=0;y<224u*sy;y++)
        for(unsigned x=0;x<256u*sx;x++){
            const unsigned index=texture->pixels[(y/sy+6u)&31u][(x*(2u/sx)+10u)&63u];
            som_overlay_put(pixels,pitch,format,x,y,palette[index]);
        }
}

static inline void som_disc_draw(uint8_t *pixels,unsigned width,unsigned height,
                                size_t pitch,int format,const SomDisciplines *d,
                                const SomStatusTexture *texture){
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    if(!d->open||!pixels||width<256u*sx||height<224u*sy)return;
    const unsigned who=d->who<SOM_PARTY_MEMBERS?d->who:0u;
    const unsigned equipped=som_disc_member_class(d,who);
    const uint32_t white=som_overlay_color(format,255,255,255);
    const uint32_t gold=som_overlay_color(format,255,223,143);
    som_status_texture_draw(texture,pixels,pitch,format,sx,sy);

    char text[32];
    som_disc_window(pixels,width,height,pitch,format,3,10,101,85);
    som_disc_window(pixels,width,height,pitch,format,3,99,101,99);
    som_disc_window(pixels,width,height,pitch,format,107,10,146,188);
    som_overlay_label(pixels,width,height,pitch,format,som_party_names[who],53,23,1,white);
    som_overlay_label(pixels,width,height,pitch,format,"Current Class",53,43,1,gold);
    som_overlay_label(pixels,width,height,pitch,format,
                      equipped<SOM_DISCIPLINE_COUNT?som_discipline_names[equipped]:"None",
                      53,57,1,white);
    if(equipped<SOM_DISCIPLINE_COUNT){
        snprintf(text,sizeof(text),"Level %u",(unsigned)d->level[equipped]);
        som_overlay_label(pixels,width,height,pitch,format,text,53,73,1,white);
    }
    som_overlay_label(pixels,width,height,pitch,format,"B Equip",13,113,0,white);
    som_overlay_label(pixels,width,height,pitch,format,"X Party",13,127,0,white);
    som_overlay_label(pixels,width,height,pitch,format,d->grab?"Y Drop":"Y Reorder",13,141,0,white);
    som_overlay_label(pixels,width,height,pitch,format,"L/R Music",13,155,0,white);
    som_overlay_label(pixels,width,height,pitch,format,"Select Guide",13,169,0,white);
    som_overlay_label(pixels,width,height,pitch,format,"A Back",13,183,0,white);
    som_overlay_label(pixels,width,height,pitch,format,"Classes",115,21,0,gold);
    som_overlay_label(pixels,width,height,pitch,format,"Lv",235,21,0,gold);
    for(unsigned row=0;row<SOM_DISCIPLINE_COUNT;row++){
        const unsigned cls=d->order[row]%SOM_DISCIPLINE_COUNT;
        snprintf(text,sizeof(text),"%c%c %-16s %3u",
                 row==d->cursor?(d->grab?'=':'>'):' ',cls==equipped?'*':' ',
                 som_discipline_names[cls],(unsigned)d->level[cls]);
        som_overlay_label(pixels,width,height,pitch,format,text,112,36+(int)row*9,0,
                          row==d->cursor?gold:white);
    }
}
#endif
