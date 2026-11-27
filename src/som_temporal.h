/* Time effects on monsters: the Monk's bullet time, made per monster slot.
 *
 * How time is bent (measured with bullet time, 2026-11-27): after each
 * emulated frame a "held" monster is put back where it stood and its attack
 * gauge (+0x1ED, counting down) loses that frame's step. Its animation still
 * plays, but it neither moves nor gets to attack sooner. The game, the
 * heroes, the music and the UI run as ever; the emulator is never slowed.
 * SNES Secret of Mana has no per-entity clock, so that is the only reliable
 * lever: a monster already inside an attack still finishes it.
 *
 * World time (bullet time) holds every monster on odd frames of its
 * countdown, exactly as som_bullet_world always did. A slot effect has a
 * speed in percent (0 frozen, 50 half, 100 none), an owner hero, a duration
 * and a priority; the slowest effect on a slot wins, a weaker one never cuts
 * a stronger one short. Freezes are capped: at most SOM_TIME_FREEZE_MAX
 * frozen frames in a row, then the slot is immune to freezing for
 * SOM_TIME_IMMUNE frames, so no combination of perks can lock a monster
 * forever. A recycled slot (another species id) drops its effects.
 * Pure: no I/O, no WRAM; the proxy does the holding. */
#ifndef FLXTR_SOM_TEMPORAL_H
#define FLXTR_SOM_TEMPORAL_H
#include <stdint.h>
#include <string.h>

#define SOM_TIME_SLOTS 3u
#define SOM_TIME_FREEZE_MAX 90u      /* 1.5 s frozen at most in one go */
#define SOM_TIME_IMMUNE 180u         /* then 3 s that cannot be frozen */
#define SOM_TIME_BOSS_FREEZE 20u     /* a boss is frozen a third of a second at most */
#define SOM_TIME_NO_OWNER 0xFFu

typedef struct {
    uint8_t speed;           /* percent: 0 frozen .. 99; 100 = no effect */
    uint8_t owner;           /* hero who cast it */
    uint8_t id;              /* monster species id when cast (slot recycle check) */
    uint16_t frames;         /* left */
    uint16_t frozen_run;     /* frames frozen in a row */
    uint16_t immune;         /* frames left that cannot be frozen */
    uint16_t acc;            /* speed accumulator for partial speeds */
} SomTimeSlot;

typedef struct {
    unsigned world;          /* bullet time frames left (was bullet_frames) */
    SomTimeSlot slot[SOM_TIME_SLOTS];
} SomTemporal;

static inline void som_time_reset(SomTemporal *t){if(t)memset(t,0,sizeof(*t));}

/* Bullet time. Extending never makes it longer than max_total frames. */
static inline void som_time_world(SomTemporal *t,unsigned frames){if(t)t->world=frames;}
static inline unsigned som_time_world_extend(SomTemporal *t,unsigned frames,unsigned max_total){
    if(!t||!t->world)return 0;
    const unsigned before=t->world;
    t->world=t->world+frames>max_total?(t->world>max_total?t->world:max_total):t->world+frames;
    return t->world-before;
}

/* Put an effect on a slot. Returns 1 if it took (or refreshed), 0 if a
 * stronger one already runs or the slot is immune to a freeze. */
static inline int som_time_apply(SomTemporal *t,unsigned k,unsigned speed,unsigned frames,
                                 unsigned owner,uint8_t id,int boss){
    if(!t||k>=SOM_TIME_SLOTS||!frames||speed>=100u)return 0;
    SomTimeSlot *s=&t->slot[k];
    if(s->frames&&s->id!=id)memset(s,0,sizeof(*s));
    if(!speed){
        if(s->immune)return 0;
        if(boss&&frames>SOM_TIME_BOSS_FREEZE)frames=SOM_TIME_BOSS_FREEZE;
        if(frames>SOM_TIME_FREEZE_MAX)frames=SOM_TIME_FREEZE_MAX;
    }
    if(s->frames&&s->speed<speed)return 0;                 /* a slower one runs */
    if(s->frames&&s->speed==speed&&s->frames>=frames)return 1;
    s->speed=(uint8_t)speed;s->frames=(uint16_t)(frames>0xFFFFu?0xFFFFu:frames);
    s->owner=(uint8_t)owner;s->id=id;
    return 1;
}
static inline void som_time_clear(SomTemporal *t,unsigned k){
    if(t&&k<SOM_TIME_SLOTS){const uint16_t imm=t->slot[k].immune;memset(&t->slot[k],0,sizeof(t->slot[k]));
                            t->slot[k].immune=imm;}
}
/* Speed of a slot right now (100 = normal). */
static inline unsigned som_time_speed(const SomTemporal *t,unsigned k,uint8_t id){
    if(!t||k>=SOM_TIME_SLOTS)return 100u;
    const SomTimeSlot *s=&t->slot[k];
    unsigned v=s->frames&&s->id==id?s->speed:100u;
    if(t->world&&v>50u)v=50u;
    return v;
}
static inline int som_time_frozen(const SomTemporal *t,unsigned k,uint8_t id){
    return som_time_speed(t,k,id)==0u;
}
/* Does slot k stay put this frame? Call once per slot per frame, before
 * som_time_tick. World time alone keeps bullet time's own rhythm (held on
 * odd frames left after the countdown), so the Monk feels exactly as it did. */
static inline int som_time_hold(SomTemporal *t,unsigned k,uint8_t id){
    if(!t||k>=SOM_TIME_SLOTS)return 0;
    SomTimeSlot *s=&t->slot[k];
    const int own=s->frames&&s->id==id;
    if(!own){
        if(!t->world)return 0;
        return ((t->world-1u)&1u)!=0u;                     /* bullet time as before */
    }
    unsigned v=s->speed;
    if(t->world&&v>50u)v=50u;
    if(!v)return 1;
    s->acc=(uint16_t)(s->acc+v);
    if(s->acc>=100u){s->acc=(uint16_t)(s->acc-100u);return 0;}
    return 1;
}
/* One frame passes. Returns a bit per slot whose effect just ended. */
static inline unsigned som_time_tick(SomTemporal *t){
    if(!t)return 0;
    unsigned ended=0;
    if(t->world)t->world--;
    for(unsigned k=0;k<SOM_TIME_SLOTS;k++){
        SomTimeSlot *s=&t->slot[k];
        if(s->immune)s->immune--;
        if(!s->frames)continue;
        if(!s->speed){
            if(++s->frozen_run>=SOM_TIME_FREEZE_MAX){s->frames=1;}
        }else s->frozen_run=0;
        if(!--s->frames){
            if(!s->speed)s->immune=SOM_TIME_IMMUNE;
            s->frozen_run=0;s->acc=0;s->speed=100u;
            ended|=1u<<k;
        }
    }
    return ended;
}
static inline int som_time_active(const SomTemporal *t){
    if(!t)return 0;
    if(t->world)return 1;
    for(unsigned k=0;k<SOM_TIME_SLOTS;k++)if(t->slot[k].frames||t->slot[k].immune)return 1;
    return 0;
}
/* Effects owned by a hero still running (Psychic Acceleration). */
static inline int som_time_owned(const SomTemporal *t,unsigned owner){
    if(!t)return 0;
    for(unsigned k=0;k<SOM_TIME_SLOTS;k++)
        if(t->slot[k].frames&&t->slot[k].owner==owner)return 1;
    return 0;
}
#endif
