/* FlXtR's own MP and AP (player's design, 2026-11-20).
 *
 * Energy: every class has its own pool, named its own way (Chakra for
 * Ninjutsu, Chi for Martial Arts, Ki for Bushido, Mana, Faith...). The
 * class's techniques (the moves the proxy makes: Blink, Kawarimi,
 * Shunshin, Issen, Hundred Fists, the class X moves...) spend it; the
 * game's own spells keep the game's MP. Its size grows with the class level
 * and it fills by itself, faster while the hero stands still (charging).
 *
 * AP: action points, SOM_AP_MAX pips shared by the evades (Ninja Storm's
 * substitution gauge): a dodge or a Blink takes pips, one comes back every
 * SOM_AP_REFILL_FRAMES. Pure state here; the proxy draws and charges. */
#ifndef FLXTR_SOM_ENERGY_H
#define FLXTR_SOM_ENERGY_H
#include <stdint.h>
#include "som_disciplines.h"

#define SOM_ENERGY_BASE 50u
#define SOM_ENERGY_PER_LEVEL 2u
#define SOM_ENERGY_CAP 250u
#define SOM_ENERGY_REFILL_FRAMES 20u      /* +1 every 1/3 s */
#define SOM_ENERGY_STILL_FRAMES 6u        /* +1 every 0.1 s standing still */
#define SOM_AP_MAX 4u
#define SOM_AP_REFILL_FRAMES 90u          /* one pip every 1.5 s */

static const char *const som_energy_names[SOM_DISCIPLINE_COUNT]={
    "Spirit",   /* Swordsmanship */
    "Chi",      /* Martial Arts */
    "Chakra",   /* Ninjutsu */
    "Mana",     /* Magic */
    "Faith",    /* Divine Arts */
    "Malice",   /* Dark Arts */
    "Bond",     /* Summoning */
    "Rhythm",   /* Support & Dance */
    "Flow",     /* Weapon Flow */
    "Nerve",    /* Roguecraft */
    "Ki",       /* Bushido */
    "Focus",    /* Archery */
    "Zeal",     /* Vampire Hunting */
    "Souls",    /* Necromancy */
    "Steam",    /* Artifice */
    "Psi",      /* Psionics */
};

typedef struct {
    uint16_t energy[SOM_DISCIPLINE_COUNT];
    uint8_t started[SOM_DISCIPLINE_COUNT];
    uint8_t ap;
    unsigned energy_tick,ap_tick;
} SomEnergy;

static inline unsigned som_energy_max(unsigned level){
    const unsigned m=SOM_ENERGY_BASE+SOM_ENERGY_PER_LEVEL*(level?level-1u:0u);
    return m>SOM_ENERGY_CAP?SOM_ENERGY_CAP:m;
}
static inline const char *som_energy_name(unsigned cls){
    return cls<SOM_DISCIPLINE_COUNT?som_energy_names[cls]:"Energy";
}
/* One frame: refill the class's pool (a new class starts full) and AP. */
static inline void som_energy_tick(SomEnergy *e,unsigned cls,unsigned level,int still){
    if(e->ap>SOM_AP_MAX)e->ap=SOM_AP_MAX;
    if(e->ap<SOM_AP_MAX&&++e->ap_tick>=SOM_AP_REFILL_FRAMES){e->ap++;e->ap_tick=0;}
    if(e->ap>=SOM_AP_MAX)e->ap_tick=0;
    if(cls>=SOM_DISCIPLINE_COUNT)return;
    const unsigned max=som_energy_max(level);
    if(!e->started[cls]){e->started[cls]=1;e->energy[cls]=(uint16_t)max;}
    if(e->energy[cls]>max)e->energy[cls]=(uint16_t)max;
    if(e->energy[cls]<max&&++e->energy_tick>=(still?SOM_ENERGY_STILL_FRAMES:SOM_ENERGY_REFILL_FRAMES)){
        e->energy[cls]++;e->energy_tick=0;
    }
}
static inline void som_energy_init(SomEnergy *e){
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++){e->energy[i]=0;e->started[i]=0;}
    e->ap=SOM_AP_MAX;e->energy_tick=e->ap_tick=0;
}
/* 1 when the class has `energy` and `ap` to spend (no class: free). */
static inline int som_energy_can(const SomEnergy *e,unsigned cls,unsigned energy,unsigned ap){
    if(cls>=SOM_DISCIPLINE_COUNT)return 1;
    const unsigned have=e->started[cls]?e->energy[cls]:SOM_ENERGY_CAP;
    return have>=energy&&e->ap>=ap;
}
static inline void som_energy_take(SomEnergy *e,unsigned cls,unsigned energy,unsigned ap){
    if(cls>=SOM_DISCIPLINE_COUNT)return;
    e->energy[cls]=(uint16_t)(e->energy[cls]>energy?e->energy[cls]-energy:0u);
    e->ap=(uint8_t)(e->ap>ap?e->ap-ap:0u);
}
#endif
