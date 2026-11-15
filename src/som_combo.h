/* Combo ladder for normal attacks (fighting-game style).
 *
 * Only confirmed hits count (a monster's HP dropped after the swing), never
 * button taps. The chain level climbs with the hits, and switching weapons
 * mid-chain (B primary <-> Y secondary) raises it further:
 *
 *   hits 1-2: no level yet        hits 3: level 1, then +1 every 2 hits
 *   each weapon switch that lands: +1 level (at most +SOM_COMBO_MIX_MAX)
 *
 * The level is capped at SOM_COMBO_LEVEL_MAX (the game's charge levels; the
 * proxy further caps it at what the weapon has learned). Holding B or Y then
 * unleashes that level with that button's weapon through the game's own
 * charged attack. A swing that hits nothing, or a pause longer than
 * SOM_COMBO_GAP, ends the chain. Pure state, no I/O. */
#ifndef FLXTR_SOM_COMBO_H
#define FLXTR_SOM_COMBO_H
#include <stdint.h>
#include <stdio.h>

#define SOM_COMBO_HIT_WINDOW 75ul   /* frames for a swing to land */
#define SOM_COMBO_GAP 180ul         /* frames between hits before it drops */
#define SOM_COMBO_FIRST_LEVEL_HITS 3u
#define SOM_COMBO_HITS_PER_LEVEL 2u
#define SOM_COMBO_MIX_MAX 2u
#define SOM_COMBO_LEVEL_MAX 8u
#define SOM_COMBO_HOLD_FRAMES 12ul  /* hold B/Y this long to unleash */
#define SOM_COMBO_NO_WEAPON 0xFFu

typedef struct {
    unsigned hits, mixes;
    uint8_t last_weapon;        /* 0 = B (primary), 1 = Y (secondary) */
    uint8_t pending_weapon;
    int pending;                /* a swing is waiting to land */
    unsigned long swing_frame, hit_frame;
} SomCombo;

static inline void som_combo_reset(SomCombo *c){
    c->hits=c->mixes=0;c->pending=0;
    c->last_weapon=c->pending_weapon=SOM_COMBO_NO_WEAPON;
    c->swing_frame=c->hit_frame=0;
}
/* bonus: extra finisher levels (perks), mix_max: how many weapon switches
 * count (SOM_COMBO_MIX_MAX, raised by the Weapon Flow perk). */
static inline unsigned som_combo_level_with(const SomCombo *c,unsigned mix_max,unsigned bonus){
    if(c->hits<SOM_COMBO_FIRST_LEVEL_HITS)return 0;
    unsigned level=1u+(c->hits-SOM_COMBO_FIRST_LEVEL_HITS)/SOM_COMBO_HITS_PER_LEVEL;
    level+=(c->mixes>mix_max?mix_max:c->mixes)+bonus;
    return level>SOM_COMBO_LEVEL_MAX?SOM_COMBO_LEVEL_MAX:level;
}
static inline unsigned som_combo_level(const SomCombo *c){
    return som_combo_level_with(c,SOM_COMBO_MIX_MAX,0u);
}
/* A swing started with weapon `which` (0 = B, 1 = Y). */
static inline void som_combo_on_swing(SomCombo *c,unsigned which,unsigned long frame){
    if(c->hits&&frame-c->hit_frame>SOM_COMBO_GAP)som_combo_reset(c);
    c->pending=1;c->pending_weapon=(uint8_t)(which?1u:0u);c->swing_frame=frame;
}
/* A monster lost HP. Returns 1 when it counted for the pending swing. */
static inline int som_combo_on_hit(SomCombo *c,unsigned long frame){
    if(!c->pending)return 0;
    c->pending=0;
    if(c->last_weapon!=SOM_COMBO_NO_WEAPON&&c->last_weapon!=c->pending_weapon)c->mixes++;
    c->last_weapon=c->pending_weapon;
    c->hits++;c->hit_frame=frame;
    return 1;
}
/* Per frame. `held`: an attack button is held (a charge is building), so
 * neither a whiff nor the pause may drop the chain meanwhile. Returns 1
 * when the chain was dropped this frame. */
static inline int som_combo_tick(SomCombo *c,unsigned long frame,int held){
    if(held||(!c->hits&&!c->pending))return 0;
    if((c->pending&&frame-c->swing_frame>SOM_COMBO_HIT_WINDOW)||
       (c->hits&&!c->pending&&frame-c->hit_frame>SOM_COMBO_GAP)){
        som_combo_reset(c);return 1;
    }
    return 0;
}
/* "5 HIT MIX LV3"; empty with no hits. */
static inline void som_combo_text_at(const SomCombo *c,unsigned level,char out[24]){
    out[0]=0;
    if(!c->hits)return;
    if(level)snprintf(out,24,"%u HIT%s LV%u",c->hits,c->mixes?" MIX":"",level);
    else snprintf(out,24,"%u HIT",c->hits);
}
static inline void som_combo_text(const SomCombo *c,char out[24]){
    som_combo_text_at(c,som_combo_level(c),out);
}
#endif
