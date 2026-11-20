/* Companion class moves (player's request, 2026-11-20): the girl and the
 * sprite use their class's signature technique on their own, paid from
 * their own class energy (som_energy.h), and dodge with their class's evade
 * paid from their own AP. The game's ally AI still walks and swings for
 * them; the proxy adds the technique when it fits the moment.
 *
 * Pure table and choice here; the proxy moves, damages and heals. */
#ifndef FLXTR_SOM_ALLY_MOVES_H
#define FLXTR_SOM_ALLY_MOVES_H
#include <stdint.h>
#include "som_disciplines.h"

enum {
    SOM_AM_AREA,      /* every monster around her */
    SOM_AM_MULTI,     /* a flurry of hits on the nearest */
    SOM_AM_DASH,      /* travels to the target's far side, cuts on arrival */
    SOM_AM_BOLT,      /* a blast at the target (it and its neighbours) */
    SOM_AM_RANGED,    /* up to three monsters in range */
    SOM_AM_HEAL,      /* the most hurt party member */
    SOM_AM_HEAL_ALL   /* everyone a little */
};
typedef struct {
    const char *name;
    uint8_t kind, energy, reach;   /* reach: px to the target (heals: none) */
    uint8_t base, per_level, hits; /* damage or heal = base + per_level*level, per hit */
} SomAllyMove;
static const SomAllyMove som_ally_moves[SOM_DISCIPLINE_COUNT]={
    {"Whirlwind",     SOM_AM_AREA,     20, 36, 10,2,1},  /* Swordsman */
    {"Hundred Fists", SOM_AM_MULTI,    20, 28,  4,1,5},  /* Monk */
    {"Shunshin",      SOM_AM_DASH,     25, 96, 14,2,1},  /* Ninja */
    {"Arcane Bolt",   SOM_AM_BOLT,     20,128, 12,2,1},  /* Mage */
    {"Holy Light",    SOM_AM_HEAL,     25,  0, 12,3,1},  /* Cleric */
    {"Shadow Bolt",   SOM_AM_BOLT,     20,128, 13,2,1},  /* Warlock */
    {"Spirit Lash",   SOM_AM_BOLT,     20,112, 11,2,1},  /* Summoner */
    {"Healing Waltz", SOM_AM_HEAL_ALL, 30,  0,  6,2,1},  /* Dancer */
    {"Flow Chain",    SOM_AM_MULTI,    20, 32,  8,1,3},  /* Weaponmaster */
    {"Shadowstep",    SOM_AM_DASH,     20, 80, 12,2,1},  /* Rogue */
    {"Issen",         SOM_AM_DASH,     25, 96, 16,2,1},  /* Samurai */
    {"Volley",        SOM_AM_RANGED,   20,120,  9,2,1},  /* Archer */
    {"Grand Cross",   SOM_AM_BOLT,     25, 96, 14,2,1},  /* Hunter */
    {"Soul Drain",    SOM_AM_BOLT,     20,112, 11,2,1},  /* Necromancer */
    {"Bomb",          SOM_AM_BOLT,     20, 96, 13,2,1},  /* Artificer */
    {"Mind Blast",    SOM_AM_RANGED,   20,120, 10,2,1},  /* Mentalist */
};
#define SOM_ALLY_MOVE_COOLDOWN 240u   /* frames between one companion's techniques */
#define SOM_ALLY_HIT_GAP 6u           /* frames between a flurry's hits */
#define SOM_ALLY_HEAL_BELOW_PCT 60u   /* heal someone under this much HP */
#define SOM_ALLY_DODGE_NEAR 18        /* a monster this close: dodge */
#define SOM_ALLY_DODGE_AP 1u
#define SOM_ALLY_DASH_SPEED 6         /* px per frame */

static inline unsigned som_ally_move_amount(const SomAllyMove *m,unsigned level){
    if(level>100u)level=100u;
    return (unsigned)m->base+(unsigned)m->per_level*(level?level-1u:0u)/2u;
}
/* Is this the moment? dist: px to the nearest monster (<0: none);
 * worst_pct: the lowest HP percentage in the party; crowd: monsters within
 * the reach. 1: use it. */
static inline int som_ally_move_fits(const SomAllyMove *m,int dist,unsigned worst_pct,
                                     unsigned crowd){
    switch(m->kind){
    case SOM_AM_HEAL: return worst_pct<SOM_ALLY_HEAL_BELOW_PCT;
    case SOM_AM_HEAL_ALL: return worst_pct<SOM_ALLY_HEAL_BELOW_PCT+15u;
    case SOM_AM_AREA: return dist>=0&&dist<=m->reach&&crowd>=1u;
    case SOM_AM_DASH: return dist>=24&&dist<=m->reach;   /* not when already on it */
    default: return dist>=0&&dist<=m->reach;
    }
}
/* A dodge: the class's evade, away from (mx, my), speed*frames px. */
static inline int som_ally_dodges(unsigned cls){
    return cls<SOM_DISCIPLINE_COUNT&&som_disc_dodge(cls,0u).speed>0u;
}
#endif
