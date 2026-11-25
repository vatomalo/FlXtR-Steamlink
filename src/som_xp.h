/* Discipline experience from verified play (classless, learn by doing).
 *
 * The proxy calls som_disc_award (src/som_disciplines.h) only for events it
 * has confirmed in WRAM, never for button presses:
 *   - a weapon hit: a live monster lost HP after the hero's swing; it trains
 *     the discipline that owns the weapon (the chosen class first),
 *   - a hit that switched weapons mid-chain: Weapon Flow as well,
 *   - a dodge with a monster close enough to have hit (a real near miss),
 *   - a hostile spell whose target lost HP,
 *   - a shadow clone explosion that caught a monster, a substitution,
 *   - a familiar strike (Summoning).
 * Empty space and invulnerable targets lose no HP, so they teach nothing.
 * Farming one kind of monster fades: each species keeps a recent count and
 * the award shrinks as 8 / (8 + count) until it stops (count 64); a minute
 * without that species resets it. Pure functions, no I/O. */
#ifndef FLXTR_SOM_XP_H
#define FLXTR_SOM_XP_H
#include <stdint.h>
#include "som_disciplines.h"

#define SOM_XP_HIT 10u
#define SOM_XP_FLOW 8u
#define SOM_XP_DODGE 15u
#define SOM_XP_SPELL 15u
#define SOM_XP_CLONE 20u
#define SOM_XP_FAMILIAR 4u
#define SOM_XP_SPECIES 8u
#define SOM_XP_RESET_FRAMES 3600ul      /* a minute without that species */
#define SOM_XP_STOP_COUNT 64u
#define SOM_XP_SPELL_WINDOW 240ul       /* frames for a cast to land */
#define SOM_XP_NEAR_MISS_PX 24          /* monster this close during a dodge */

enum {SOM_DISC_SWORD=0,SOM_DISC_MARTIAL=1,SOM_DISC_NINJUTSU=2,SOM_DISC_MAGIC=3,
      SOM_DISC_SUMMONING=6,SOM_DISC_SUPPORT=7,SOM_DISC_FLOW=8,SOM_DISC_ARCHERY=11};

/* Which discipline a weapon trains: the chosen class when it uses that
 * weapon (class_weapons = its two weapon types), else the weapon's home. */
static inline unsigned som_xp_weapon_discipline(unsigned weapon,unsigned selected,
                                                const uint8_t class_weapons[2]){
    if(selected<SOM_DISCIPLINE_COUNT&&class_weapons&&
       (class_weapons[0]==weapon||class_weapons[1]==weapon))return selected;
    static const uint8_t home[8]={
        SOM_DISC_MARTIAL,  /* glove */
        SOM_DISC_SWORD,    /* sword */
        SOM_DISC_SWORD,    /* axe */
        SOM_DISC_MARTIAL,  /* spear */
        SOM_DISC_SUPPORT,  /* whip */
        SOM_DISC_ARCHERY,  /* bow */
        SOM_DISC_NINJUTSU, /* boomerang / shuriken */
        SOM_DISC_ARCHERY,  /* javelin */
    };
    return weapon<8u?home[weapon]:SOM_DISC_SWORD;
}
/* Spells train the chosen class when it casts, else Magic. */
static inline unsigned som_xp_spell_discipline(unsigned selected){
    return selected<SOM_DISCIPLINE_COUNT?selected:SOM_DISC_MAGIC;
}

typedef struct {
    uint8_t species[SOM_XP_SPECIES];
    uint8_t count[SOM_XP_SPECIES];
    unsigned long last[SOM_XP_SPECIES];
} SomXpDecay;

/* The award for `base` against monster `species` now; updates the count. */
static inline unsigned som_xp_scaled(SomXpDecay *d,unsigned species,unsigned base,
                                     unsigned long frame){
    unsigned slot=SOM_XP_SPECIES,oldest=0;
    for(unsigned i=0;i<SOM_XP_SPECIES;i++){
        if(d->last[i]&&d->species[i]==(uint8_t)species){slot=i;break;}
        if(!d->last[i]||d->last[i]<d->last[oldest])oldest=i;
    }
    if(slot==SOM_XP_SPECIES){
        slot=oldest;d->species[slot]=(uint8_t)species;d->count[slot]=0;
    }else if(frame-d->last[slot]>SOM_XP_RESET_FRAMES)d->count[slot]=0;
    const unsigned count=d->count[slot];
    d->last[slot]=frame?frame:1u;
    if(count<255u)d->count[slot]=(uint8_t)(count+1u);
    if(count>=SOM_XP_STOP_COUNT)return 0;
    const unsigned amount=base*8u/(8u+count);
    return amount?amount:1u;
}
/* Distinct, non-zero event ids per frame and kind for som_disc_award. */
static inline uint64_t som_xp_event(unsigned long frame,unsigned kind){
    return (((uint64_t)frame+1u)<<4)|(uint64_t)(kind&15u);
}
#endif
