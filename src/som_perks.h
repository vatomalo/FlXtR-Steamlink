/* Perks: unlocks earned from verified discipline levels (data-driven).
 *
 * Each row is one perk. A perk has ranks: rank = 1 + (level - min_level) /
 * levels_per_rank, at most max_rank, 0 below min_level or while its
 * prerequisite (a second discipline at a level: the synergy hook) is unmet.
 * Effects are numbers the proxy adds to systems it already controls and the
 * harness verifies; every effect has a cap. Ranks follow the levels, so the
 * same event can never stack a bonus twice. Eligibility (som_perk_rank) is
 * separate from use (som_perk_value). Pure, no I/O. */
#ifndef FLXTR_SOM_PERKS_H
#define FLXTR_SOM_PERKS_H
#include <stdint.h>
#include <stddef.h>
#include "som_disciplines.h"

#define SOM_PERK_CLASS 0xFEu      /* "the discipline of the chosen class" */
#define SOM_PERK_NONE 0xFFu

enum SomPerkEffect {
    SOM_EFFECT_DODGE_FRAMES = 1,  /* extra movement frames of a dodge */
    SOM_EFFECT_CLONE_DAMAGE,      /* extra clone explosion damage */
    SOM_EFFECT_FINISHER_LEVEL,    /* extra combo finisher level */
    SOM_EFFECT_FLOW_MIX,          /* extra weapon-switch bonus levels allowed */
    /* Passive skills (2026-11-19, after Baldur's Gate 3, Dragon Age, Yakuza). */
    SOM_EFFECT_FIRST_HIT_CUT,     /* % of the first hit of a fight taken back */
    SOM_EFFECT_REELING_BONUS,     /* % more damage on a monster hit moments ago */
    SOM_EFFECT_BLINK_COOLDOWN_CUT,/* % off the Blink cooldown */
    SOM_EFFECT_GUARD_COUNTER,     /* counter level when a guarded hit lands */
    SOM_EFFECT_LOW_HP_BONUS,      /* % more damage below a quarter of max HP */
    SOM_EFFECT_IAI_COOLDOWN_CUT,  /* % off the Iaijutsu cooldown */
    SOM_EFFECT_KILL_HEAL,         /* % of max HP healed on each kill */
    SOM_EFFECT_FUMA_LEVEL,        /* extra Fuma clone strike levels */
    SOM_EFFECT_COUNT
};

typedef struct {
    uint8_t id;                   /* stable: stored in saves as a bit */
    const char *name;
    uint8_t discipline;           /* index, or SOM_PERK_CLASS */
    uint8_t min_level, levels_per_rank, max_rank;
    uint8_t effect;
    int16_t per_rank, cap;
    uint8_t req_discipline, req_level;   /* SOM_PERK_NONE: no prerequisite */
} SomPerk;

static const SomPerk som_perks[] = {
    {1, "Dodge Mastery",    SOM_PERK_CLASS, 5, 5, 3, SOM_EFFECT_DODGE_FRAMES,  2,  6, SOM_PERK_NONE, 0},
    {2, "Clone Mastery",    2,              5, 5, 3, SOM_EFFECT_CLONE_DAMAGE,  4, 12, SOM_PERK_NONE, 0},
    {3, "Finisher Mastery", SOM_PERK_CLASS, 3, 5, 2, SOM_EFFECT_FINISHER_LEVEL, 1, 2, SOM_PERK_NONE, 0},
    {4, "Weapon Flow",      8,              5, 5, 2, SOM_EFFECT_FLOW_MIX,       1, 2, SOM_PERK_NONE, 0},
    /* Baldur's Gate 3: the rogue's Uncanny Dodge halves the first hit. */
    {5, "Uncanny Dodge",    9,              3, 5, 2, SOM_EFFECT_FIRST_HIT_CUT, 25, 50, SOM_PERK_NONE, 0},
    /* Dragon Age: Coup de Grace, more damage on a staggered foe. */
    {6, "Coup de Grace",    2,              4, 5, 2, SOM_EFFECT_REELING_BONUS, 25, 50, SOM_PERK_NONE, 0},
    /* Dragon Age specialization: ninja and rogue together. */
    {7, "Shadow Step",      2,              5, 5, 1, SOM_EFFECT_BLINK_COOLDOWN_CUT, 50, 50, 9, 5},
    /* Yakuza: Tiger Drop, the counter on a perfectly timed guard. */
    {8, "Tiger Drop",       1,              3, 5, 2, SOM_EFFECT_GUARD_COUNTER,  1,  2, SOM_PERK_NONE, 0},
    /* Yakuza: Essence of Last Resort. */
    {9, "Last Resort",      SOM_PERK_CLASS, 6, 6, 2, SOM_EFFECT_LOW_HP_BONUS,  25, 50, SOM_PERK_NONE, 0},
    /* Baldur's Gate 3 Extra Attack, as a faster draw. */
    {10, "Iai Focus",       10,             3, 4, 3, SOM_EFFECT_IAI_COOLDOWN_CUT, 15, 45, SOM_PERK_NONE, 0},
    /* Yakuza heat: each kill gives a little back. */
    {11, "Battle Rhythm",   SOM_PERK_CLASS, 4, 6, 3, SOM_EFFECT_KILL_HEAL,      3,  9, SOM_PERK_NONE, 0},
    /* Ninja and samurai together: the Fuma clone cuts harder. */
    {12, "Fuma Mastery",    2,              5, 5, 2, SOM_EFFECT_FUMA_LEVEL,      1,  2, 10, 5},
};
#define SOM_PERK_COUNT (sizeof(som_perks)/sizeof(som_perks[0]))

static inline const SomPerk *som_perk_find(unsigned id){
    for(size_t i=0;i<SOM_PERK_COUNT;i++)if(som_perks[i].id==id)return &som_perks[i];
    return NULL;
}
static inline unsigned som_perk_level(const SomDisciplines *d,unsigned discipline){
    if(discipline==SOM_PERK_CLASS)discipline=d->selected;
    return discipline<SOM_DISCIPLINE_COUNT?d->level[discipline]:0u;
}
static inline unsigned som_perk_rank(const SomPerk *p,const SomDisciplines *d){
    if(!p||!d||!p->levels_per_rank)return 0;
    /* A passive belongs to its class: the boy must have (had) that class. */
    if(d->classless){
        const unsigned own=p->discipline==SOM_PERK_CLASS?d->selected:p->discipline;
        if(own>=SOM_DISCIPLINE_COUNT||!(d->held[0]>>own&1u))return 0;
        if(p->req_discipline!=SOM_PERK_NONE&&!(d->held[0]>>(p->req_discipline&15u)&1u))return 0;
    }
    const unsigned level=som_perk_level(d,p->discipline);
    if(!level||level<p->min_level)return 0;
    if(p->req_discipline!=SOM_PERK_NONE&&som_perk_level(d,p->req_discipline)<p->req_level)return 0;
    const unsigned rank=1u+(level-p->min_level)/p->levels_per_rank;
    return rank>p->max_rank?p->max_rank:rank;
}
/* Sum of one effect over all perks, each capped; 0 when disabled. */
static inline int som_perk_value(unsigned effect,const SomDisciplines *d,int enabled){
    if(!enabled)return 0;
    int total=0;
    for(size_t i=0;i<SOM_PERK_COUNT;i++){
        const SomPerk *p=&som_perks[i];
        if(p->effect!=effect)continue;
        int v=(int)som_perk_rank(p,d)*p->per_rank;
        if(v>p->cap)v=p->cap;
        total+=v;
    }
    return total;
}
/* Bit i+1 set for every perk with rank >= 1 (stored in saves). */
static inline uint32_t som_perk_bits(const SomDisciplines *d){
    uint32_t bits=0;
    for(size_t i=0;i<SOM_PERK_COUNT;i++)
        if(som_perk_rank(&som_perks[i],d)&&som_perks[i].id<32u)bits|=1u<<som_perks[i].id;
    return bits;
}
#endif
