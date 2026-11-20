/* Class weapon styles (player's design, 2026-11-19).
 *
 * A class's B weapon comes in SOM_STYLES_PER_CLASS styles, e.g. Bushido's
 * Tanto, Katana and Dai Katana. A style is a constant: it never changes with
 * the game's weapon orbs. The game weapon is pinned at its top plain tier
 * (its best art and row), and the proxy writes the hero's attack so the
 * style hits at level 1 like that weapon's tier-0 row; it rises to the top
 * tier's attack at SOM_STYLE_MAX_LEVEL. Each style has its own level and
 * experience, earned only by verified hits made while it is held.
 *
 * What a style changes, all three against the plain weapon:
 *   power_pct   attack, after the level scaling (100 = plain),
 *   speed_pct   how fast the attack cooldown hero +0x1ED runs down
 *               (150 = half again as fast, 70 = slower),
 *   reach_bonus px past the game's own reach: a "long cut" lands on a
 *               monster in line ahead within it when the game's swing did
 *               not reach it (melee weapons only; 0 = none).
 * The fast style is short and weak, the heavy one long and slow; the
 * balanced one is the plain weapon. Weapon types as som_weapon_types
 * (0 glove, 1 sword, 2 axe, 3 spear, 4 whip, 5 bow, 6 boomerang, 7 javelin);
 * the Python table is tools/som_weapon_styles.py. Pure functions, no I/O. */
#ifndef FLXTR_SOM_WEAPON_STYLES_H
#define FLXTR_SOM_WEAPON_STYLES_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "som_disciplines.h"

#define SOM_STYLES_PER_CLASS 3u
#define SOM_STYLE_COUNT (SOM_DISCIPLINE_COUNT*SOM_STYLES_PER_CLASS)
#define SOM_STYLE_MAX_LEVEL 8u
#define SOM_STYLE_DEFAULT 1u                /* the balanced style */
#define SOM_STYLE_XP_HIT 10u
#define SOM_STYLE_CUT_FRAMES 10ul           /* swing start -> long cut check */
#define SOM_STYLE_CUT_SIDE_PX 14            /* off the facing line */

typedef struct {
    const char *name;
    uint8_t weapon;
    int8_t reach_bonus;
    uint8_t speed_pct,power_pct;
} SomWeaponStyle;

enum {SOM_STYLE_FAST,SOM_STYLE_BALANCED,SOM_STYLE_HEAVY};

/* Per class: fast, balanced, heavy (all on the class's B weapon). */
static const SomWeaponStyle som_weapon_styles[SOM_STYLE_COUNT]={
    /* Swordsmanship: sword */
    {"Rapier",1,0,150,75},{"Longsword",1,0,100,100},{"Claymore",1,16,70,135},
    /* Martial Arts: glove */
    {"Hand Wraps",0,0,160,70},{"Cestus",0,0,100,100},{"Tiger Claws",0,10,75,130},
    /* Ninjutsu: sword */
    {"Kunai",1,0,165,65},{"Ninjato",1,0,100,100},{"Kusarigama",1,22,70,120},
    /* Magic: bare hands (casters hold no weapon) */
    {"Quick Hands",0,0,140,80},{"Bare Hands",0,0,100,100},{"Heavy Hands",0,0,75,130},
    /* Divine Arts: spear */
    {"Quick Hands",0,0,140,80},{"Bare Hands",0,0,100,100},{"Heavy Hands",0,0,75,130},
    /* Dark Arts: whip */
    {"Quick Hands",0,0,140,80},{"Bare Hands",0,0,100,100},{"Heavy Hands",0,0,75,130},
    /* Summoning: boomerang */
    {"Quick Hands",0,0,140,80},{"Bare Hands",0,0,100,100},{"Heavy Hands",0,0,75,130},
    /* Support & Dance: whip */
    {"Quick Hands",0,0,140,80},{"Bare Hands",0,0,100,100},{"Heavy Hands",0,0,75,130},
    /* Weapon Flow: sword */
    {"Gladius",1,0,145,80},{"Arming Sword",1,0,100,100},{"Bastard Sword",1,14,75,130},
    /* Roguecraft: sword */
    {"Dagger",1,0,165,65},{"Cutlass",1,0,100,100},{"Estoc",1,12,85,115},
    /* Bushido: sword */
    {"Tanto",1,0,160,70},{"Katana",1,0,100,100},{"Dai Katana",1,20,70,135},
    /* Archery: bow */
    {"Shortbow",5,0,150,75},{"Recurve Bow",5,0,100,100},{"Longbow",5,0,70,135},
    /* Vampire Hunting: whip */
    {"Leather Lash",4,0,150,75},{"Chain Whip",4,0,100,100},{"Thorn Flail",4,0,70,135},
    /* Necromancy, Artifice, Psionics: bare hands / hammer / bare hands */
    {"Quick Hands",0,0,140,80},{"Bare Hands",0,0,100,100},{"Heavy Hands",0,0,75,130},
    {"Tack Hammer",2,0,145,80},{"Forge Hammer",2,0,100,100},{"Sledge",2,14,70,135},
    {"Quick Hands",0,0,140,80},{"Bare Hands",0,0,100,100},{"Heavy Hands",0,0,75,130},
};

typedef struct {
    uint8_t selected[SOM_DISCIPLINE_COUNT];   /* 0..SOM_STYLES_PER_CLASS-1 */
    uint8_t level[SOM_STYLE_COUNT];           /* 1..SOM_STYLE_MAX_LEVEL */
    uint32_t experience[SOM_STYLE_COUNT];
} SomStyles;

static inline void som_styles_init(SomStyles *s){
    memset(s,0,sizeof(*s));
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++)s->selected[i]=SOM_STYLE_DEFAULT;
    for(unsigned i=0;i<SOM_STYLE_COUNT;i++)s->level[i]=1u;
}
static inline unsigned som_style_index(unsigned cls,unsigned which){
    return cls<SOM_DISCIPLINE_COUNT&&which<SOM_STYLES_PER_CLASS?
        cls*SOM_STYLES_PER_CLASS+which:SOM_STYLE_COUNT;
}
/* The class's held style, or NULL with no class. */
static inline const SomWeaponStyle *som_style_of(const SomStyles *s,unsigned cls,unsigned *index){
    if(!s||cls>=SOM_DISCIPLINE_COUNT)return NULL;
    const unsigned i=som_style_index(cls,s->selected[cls]%SOM_STYLES_PER_CLASS);
    if(index)*index=i;
    return &som_weapon_styles[i];
}
static inline uint32_t som_style_next_xp(unsigned level){
    return 150u+100u*(level?level-1u:0u);
}
/* Returns 1 when the style levelled up. */
static inline int som_style_award(SomStyles *s,unsigned index,unsigned amount){
    if(!s||index>=SOM_STYLE_COUNT||!amount||s->level[index]>=SOM_STYLE_MAX_LEVEL)return 0;
    const unsigned before=s->level[index];
    s->experience[index]+=amount;
    while(s->level[index]<SOM_STYLE_MAX_LEVEL&&
          s->experience[index]>=som_style_next_xp(s->level[index])){
        s->experience[index]-=som_style_next_xp(s->level[index]);
        s->level[index]++;
    }
    if(s->level[index]>=SOM_STYLE_MAX_LEVEL)s->experience[index]=0;
    return s->level[index]!=before;
}
/* The weapon's attack for this style: the tier-0 row's attack (atk_low) at
 * level 1, the top tier's (atk_high) at the max level, times power_pct. */
static inline unsigned som_style_attack(const SomWeaponStyle *st,unsigned level,
                                        unsigned atk_low,unsigned atk_high){
    if(!st)return atk_low;
    if(level<1u)level=1u;
    if(level>SOM_STYLE_MAX_LEVEL)level=SOM_STYLE_MAX_LEVEL;
    if(atk_high<atk_low)atk_high=atk_low;
    const unsigned plain=atk_low+(atk_high-atk_low)*(level-1u)/(SOM_STYLE_MAX_LEVEL-1u);
    const unsigned attack=plain*st->power_pct/100u;
    return attack?attack:1u;
}
/* One frame of the attack cooldown +0x1ED under the style's speed: the game
 * counts it down to 0 (0 = ready, verified: clearing it makes the next
 * swing hit at full). *carry keeps the fraction between frames. Returns
 * the new cooldown value. */
static inline uint8_t som_style_cooldown(uint8_t cooldown,unsigned speed_pct,int *carry){
    if(!carry)return cooldown;
    if(!cooldown){*carry=0;return 0;}
    *carry+=(int)speed_pct-100;
    while(*carry>=100&&cooldown){*carry-=100;cooldown--;}
    while(*carry<=-100){*carry+=100;if(cooldown<0xFFu)cooldown++;}
    return cooldown;
}
/* Is a monster at (dx, dy) from the hero, facing `facing` (+0x010: 0x00 up,
 * 0x01 down, 0x02 right, 0x82 left), inside the long cut: past `reach` and
 * within reach + bonus ahead, close to the facing line? */
static inline int som_style_in_cut(int dx,int dy,unsigned facing,int reach,int bonus){
    int ahead,side;
    switch(facing){
    case 0x00u: ahead=-dy;side=dx;break;
    case 0x01u: ahead=dy;side=dx;break;
    case 0x02u: ahead=dx;side=dy;break;
    case 0x82u: ahead=-dx;side=dy;break;
    default: return 0;
    }
    if(side<0)side=-side;
    return bonus>0&&ahead>reach&&ahead<=reach+bonus&&side<=SOM_STYLE_CUT_SIDE_PX;
}
/* File line: "s2 <classes> <sel x classes> <level xp> x classes*3"; "s1"
 * files are the 12-class layout (the classes added later start fresh). */
#define SOM_STYLES_V1_CLASSES 12u
static inline int som_styles_format(const SomStyles *s,char *out,size_t size){
    size_t n=0;
    int w=snprintf(out,size,"s2 %u",(unsigned)SOM_DISCIPLINE_COUNT);
    if(w<0||(size_t)w>=size)return 0;
    n=(size_t)w;
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++){
        w=snprintf(out+n,size-n," %u",(unsigned)s->selected[i]);
        if(w<0||(size_t)w>=size-n)return 0;
        n+=(size_t)w;
    }
    for(unsigned i=0;i<SOM_STYLE_COUNT;i++){
        w=snprintf(out+n,size-n," %u %lu",(unsigned)s->level[i],(unsigned long)s->experience[i]);
        if(w<0||(size_t)w>=size-n)return 0;
        n+=(size_t)w;
    }
    return 1;
}
static inline int som_styles_parse(SomStyles *s,const char *text){
    SomStyles loaded;som_styles_init(&loaded);
    if(!text)return 0;
    unsigned classes=0;
    const char *p=text+2;
    if(!strncmp(text,"s1",2))classes=SOM_STYLES_V1_CLASSES;
    else if(!strncmp(text,"s2",2)){
        int used=0;
        if(sscanf(p," %u%n",&classes,&used)!=1||!classes||classes>SOM_DISCIPLINE_COUNT)return 0;
        p+=used;
    }else return 0;
    for(unsigned i=0;i<classes;i++){
        unsigned v;int used=0;
        if(sscanf(p," %u%n",&v,&used)!=1||v>=SOM_STYLES_PER_CLASS)return 0;
        loaded.selected[i]=(uint8_t)v;p+=used;
    }
    for(unsigned i=0;i<classes*SOM_STYLES_PER_CLASS;i++){
        unsigned level;unsigned long xp;int used=0;
        if(sscanf(p," %u %lu%n",&level,&xp,&used)!=2||level<1u||level>SOM_STYLE_MAX_LEVEL)
            return 0;
        loaded.level[i]=(uint8_t)level;loaded.experience[i]=(uint32_t)xp;p+=used;
    }
    *s=loaded;
    return 1;
}
#endif
