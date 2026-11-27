/* Inherited perk runtime: equip rules, the event dispatcher and the effect
 * primitives (docs/SOM_PERK_ARCHITECTURE.md).
 *
 * The proxy reports combat events (a hit, a kill, a block ...) with the
 * context it measured; the runtime runs the perks the hero has equipped for
 * that event and answers with actions (pending damage, a heal, bullet time
 * ...) that the proxy carries out with the levers it already owns. Nothing
 * here touches WRAM, allocates or parses text in combat: per hero, the
 * equipped perks are indexed by event when they are equipped, every queue
 * is fixed size, timing is in frames and randomness (none of the current
 * perks need it) is a seeded xorshift.
 *
 * Safety: an event made by a perk's own effect (a kill by bonus damage, for
 * example) carries depth > 0 and only sets off perks marked
 * generated_can_trigger; depth is capped at SOM_PRT_MAX_DEPTH. There is a
 * budget of activations per frame and per root event, cooldowns, charges,
 * stack caps, a bounded delayed-damage queue and a bounded action queue.
 * Overflow drops the newest and counts it, never grows. */
#ifndef FLXTR_SOM_PERK_RUNTIME_H
#define FLXTR_SOM_PERK_RUNTIME_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "generated/som_perk_registry.h"

#define SOM_PRT_HEROES 3u
#define SOM_PRT_MONSTERS 3u
#define SOM_PRT_ACTIONS 32u
#define SOM_PRT_DELAYED 16u
#define SOM_PRT_TEMP 6u
#define SOM_PRT_MAX_DEPTH 2u
#define SOM_PRT_FRAME_BUDGET 24u      /* activations per frame, all heroes */
#define SOM_PRT_ROOT_BUDGET 8u        /* activations caused by one root event */
#define SOM_PRT_CHAIN_FRAMES 60u      /* "in a row": a hit within a second */
#define SOM_PRT_IDLE_FRAMES 180u
#define SOM_PRT_TARGET_FRAMES 120u    /* hits on one monster count for 2 s */
#define SOM_PRT_SWITCH_FRAMES 120u
#define SOM_PRT_SHIELD_MAX_STACK 3u

/* What the proxy measured, as bits (SomPerkEvent.facts). */
enum {
    SOM_PFACT_BEHIND = 1u<<0, SOM_PFACT_TARGET_STAGGERED = 1u<<1, SOM_PFACT_TARGET_ISOLATED = 1u<<2,
    SOM_PFACT_TARGET_UNDEAD = 1u<<3, SOM_PFACT_TARGET_RECOVERING = 1u<<4, SOM_PFACT_TARGET_UNAWARE = 1u<<5,
    SOM_PFACT_TARGET_LARGE = 1u<<6, SOM_PFACT_TARGET_BOSS = 1u<<7, SOM_PFACT_CHARGING = 1u<<8,
    SOM_PFACT_BULLET_TIME = 1u<<9, SOM_PFACT_OUT_OF_COMBAT = 1u<<10, SOM_PFACT_ALLY_NEAR_TARGET = 1u<<11,
    SOM_PFACT_ALLY_LOW_HP_NEAR = 1u<<12, SOM_PFACT_NO_MONSTER_CLOSE = 1u<<13, SOM_PFACT_SPELL = 1u<<14,
    SOM_PFACT_TARGET_HURT_ALLY = 1u<<15, SOM_PFACT_TARGET_ATTACKING_ALLY = 1u<<16,
    SOM_PFACT_OWNER_GUARDING = 1u<<17, SOM_PFACT_FAMILIAR_ALIVE = 1u<<18, SOM_PFACT_COOP_SAME_TARGET = 1u<<19,
    SOM_PFACT_TARGET_SLOWED = 1u<<20, SOM_PFACT_TARGET_FROZEN = 1u<<21, SOM_PFACT_OWN_TIME_EFFECT = 1u<<22
};
/* SoM weapon types (hero +0x1E4). */
enum { SOM_PW_GLOVE, SOM_PW_SWORD, SOM_PW_AXE, SOM_PW_SPEAR, SOM_PW_WHIP, SOM_PW_BOW, SOM_PW_BOOMERANG,
       SOM_PW_JAVELIN, SOM_PW_NONE = 0xFF };
static inline int som_pw_ranged(unsigned w){return w>=SOM_PW_BOW&&w<=SOM_PW_JAVELIN;}

typedef struct {
    uint8_t type;            /* SomPerkEvent enum */
    uint8_t who;             /* hero the event belongs to (attacker, the hurt one ...) */
    int8_t slot;             /* monster slot involved, -1 none */
    uint8_t target_id;       /* that monster's species id (slot recycle check) */
    uint8_t depth;           /* 0: the game did it; >0: a perk's effect did */
    uint8_t weapon, charge, combo;
    uint16_t amount;         /* damage dealt / HP lost / healed */
    uint16_t hp, hp_max;     /* the hero's, after the event */
    uint16_t target_hp, target_max;
    uint16_t dist;           /* hero to monster, px */
    uint16_t still_frames;   /* frames without D-pad */
    uint8_t energy_pct;      /* class energy 0..100 */
    uint32_t facts;
    uint32_t frame;
} SomPerkEv;

enum SomPerkActionType {
    SOM_PACT_NONE, SOM_PACT_DAMAGE, SOM_PACT_REFUND, SOM_PACT_AREA, SOM_PACT_HEAL, SOM_PACT_PARTY_HEAL,
    SOM_PACT_GIVE_BACK, SOM_PACT_ENERGY, SOM_PACT_GAUGE, SOM_PACT_STAGGER, SOM_PACT_BULLET,
    SOM_PACT_BULLET_EXTEND, SOM_PACT_TIME, SOM_PACT_EXECUTE, SOM_PACT_SAY
};
/* Where a DAMAGE/STAGGER/TIME action goes. */
enum { SOM_PAT_SLOT, SOM_PAT_NEAREST_OTHER, SOM_PAT_BEHIND_IN_LINE, SOM_PAT_AREA };
typedef struct {
    uint8_t type, who, depth, aim;
    int8_t slot;
    uint8_t target_id, exclude, speed;  /* exclude: area skips the slot; speed: TIME % */
    uint8_t bonus;                      /* 1: the hit's own extra damage (perk_id: its only source, or 0) */
    uint16_t amount, radius, frames, perk_id;
} SomPerkAction;

typedef struct { uint8_t mod, charges; int16_t pct; uint32_t until; uint16_t perk_id; } SomPerkTemp;
typedef struct {
    uint8_t used;            /* 0 free, 1 damage, 2 area, 3 heal */
    uint8_t who, target_id, depth, release;  /* release: 0 at frame, 1 at the end of bullet time */
    int8_t slot;
    uint16_t amount, radius, perk_id;
    uint32_t at;
} SomPerkDelayed;

typedef struct {
    uint8_t n;                                   /* equipped perks */
    uint8_t def[SOM_PERK_MAX_EQUIPPED];          /* som_perk_defs index */
    uint16_t by_event[SOM_PEV_COUNT];            /* bit e: equipped slot e has this trigger */
    uint32_t ready_at[SOM_PERK_MAX_EQUIPPED];    /* cooldowns */
    uint8_t stacks[SOM_PERK_MAX_EQUIPPED], charges_used[SOM_PERK_MAX_EQUIPPED];
    uint32_t stack_until[SOM_PERK_MAX_EQUIPPED];
    uint16_t counter[SOM_PERK_MAX_EQUIPPED];     /* every_n */
    uint8_t fired_fight[SOM_PERK_MAX_EQUIPPED];  /* once_per_fight */
    SomPerkTemp temp[SOM_PRT_TEMP];
    uint16_t shield;                             /* absorb pool, HP */
    uint32_t shield_until;
    /* what the hero did: for "in a row", "first hit", "after a switch" ... */
    uint32_t last_hit_frame, fight_frame, switch_frame, hits_ring[4];
    uint16_t last_hit_damage;
    uint8_t last_hit_weapon, prev_hit_weapon, weapon, hit_this_fight, hurt_this_fight, ring_at;
    uint8_t weapons_in_combo;                    /* bit per weapon type, cleared with the combo */
    uint8_t target_id[SOM_PRT_MONSTERS], target_hits[SOM_PRT_MONSTERS], target_seen[SOM_PRT_MONSTERS];
    uint32_t target_first[SOM_PRT_MONSTERS];
} SomPerkHero;

typedef struct {
    SomPerkHero hero[SOM_PRT_HEROES];
    SomPerkAction act[SOM_PRT_ACTIONS];
    unsigned n_act;
    SomPerkDelayed delayed[SOM_PRT_DELAYED];
    uint32_t budget_frame;
    unsigned budget_used, root_used;
    uint32_t kill_frame[SOM_PRT_MONSTERS];  /* one kill per monster: no double rewards */
    uint8_t kill_id[SOM_PRT_MONSTERS];
    uint32_t rng;
    int pending_ok;          /* SUPPORTED_PENDING_TEST perks may be equipped (test switch) */
    /* counters, for logs, tests and the benchmark */
    unsigned long events, activations, dropped_actions, dropped_delayed, skipped_depth, skipped_budget;
    uint16_t bonus_perk, temp_perk; unsigned bonus_perks;   /* who made this hit's bonus (one, or 0) */
    uint16_t fired_ids[SOM_PRT_ROOT_BUDGET]; unsigned n_fired; /* the last som_prt_emit's perks, for logs */
} SomPerkRuntime;

/* The progression a hero's unlocks are judged by. Levels are the party's
 * (SomDisciplines.level); held: the classes the hero has had; classless 0
 * (an older save) leaves every class open, as the class menu does. */
typedef struct {
    const uint16_t *level;   /* [16] discipline levels */
    uint16_t held;           /* bit per class this hero has had */
    uint8_t classless;
    uint8_t hero_level;      /* the game's level for universal perks and capacity */
} SomPerkUnlockView;

static inline void som_prt_init(SomPerkRuntime *rt){
    if(!rt)return;
    memset(rt,0,sizeof(*rt));
    rt->rng=0x9E3779B9u;
}
static inline uint32_t som_prt_rand(SomPerkRuntime *rt){
    uint32_t x=rt->rng?rt->rng:1u;x^=x<<13;x^=x>>17;x^=x<<5;return rt->rng=x;
}
static inline const SomPerkDef *som_prt_def(const SomPerkHero *h,unsigned e){
    return e<h->n&&h->def[e]<SOM_PERK_DEF_COUNT?&som_perk_defs[h->def[e]]:NULL;
}

/* ------------------------------------------------------------ equip rules */
static inline unsigned som_prt_capacity(unsigned hero_level){
    const unsigned c=SOM_PERK_CAPACITY_BASE+hero_level/SOM_PERK_CAPACITY_PER_LEVELS;
    return c>SOM_PERK_CAPACITY_MAX?SOM_PERK_CAPACITY_MAX:c;
}
static inline unsigned som_prt_used(const SomPerkRuntime *rt,unsigned who){
    if(!rt||who>=SOM_PRT_HEROES)return 0;
    unsigned u=0;
    for(unsigned e=0;e<rt->hero[who].n;e++){const SomPerkDef *d=som_prt_def(&rt->hero[who],e);if(d)u+=d->cost;}
    return u;
}
static inline int som_prt_unlocked_depth(const SomPerkDef *d,const SomPerkUnlockView *v,unsigned depth){
    if(!d||!v||depth>3u)return 0;
    if(d->flags&SOM_PDF_UNIVERSAL){if(v->hero_level<d->mastery)return 0;}
    else{
        if(d->discipline>=16u||!v->level||v->level[d->discipline]<d->mastery)return 0;
        if(v->classless&&!(v->held>>d->discipline&1u))return 0;
    }
    for(unsigned i=0;i<2u;i++)
        if(d->prereq[i]&&!som_prt_unlocked_depth(som_perk_def_by_id(d->prereq[i]),v,depth+1u))return 0;
    return 1;
}
static inline int som_prt_unlocked(const SomPerkDef *d,const SomPerkUnlockView *v){
    return som_prt_unlocked_depth(d,v,0);
}
/* Only perks the engine really runs can be equipped in ordinary play. */
static inline int som_prt_selectable(const SomPerkRuntime *rt,const SomPerkDef *d){
    return d&&(d->status==SOM_PSTAT_IMPLEMENTED||(rt&&rt->pending_ok&&d->status==SOM_PSTAT_SUPPORTED_PENDING_TEST));
}
enum { SOM_PEQ_OK, SOM_PEQ_UNKNOWN, SOM_PEQ_UNAVAILABLE, SOM_PEQ_LOCKED, SOM_PEQ_DUPLICATE, SOM_PEQ_FULL,
       SOM_PEQ_CAPACITY, SOM_PEQ_GROUP };
static const char *const som_peq_names[]={"ok","unknown perk","not available yet","locked","already equipped",
                                          "no free slot","not enough capacity","excludes an equipped perk"};
static inline void som_prt_reindex(SomPerkHero *h){
    memset(h->by_event,0,sizeof(h->by_event));
    for(unsigned e=0;e<h->n;e++){
        const SomPerkDef *d=som_prt_def(h,e);
        if(!d)continue;
        for(unsigned t=0;t<d->n_trig&&t<3u;t++)if(d->trig[t]<SOM_PEV_COUNT)h->by_event[d->trig[t]]|=(uint16_t)(1u<<e);
    }
}
static inline int som_prt_check(const SomPerkRuntime *rt,unsigned who,unsigned id,const SomPerkUnlockView *v){
    if(!rt||who>=SOM_PRT_HEROES)return SOM_PEQ_UNKNOWN;
    const SomPerkDef *d=som_perk_def_by_id(id);
    if(!d)return SOM_PEQ_UNKNOWN;
    const SomPerkHero *h=&rt->hero[who];
    for(unsigned e=0;e<h->n;e++){
        const SomPerkDef *o=som_prt_def(h,e);
        if(o==d)return SOM_PEQ_DUPLICATE;
        if(o&&d->group&&o->group==d->group)return SOM_PEQ_GROUP;
    }
    if(h->n>=SOM_PERK_MAX_EQUIPPED)return SOM_PEQ_FULL;
    if(!som_prt_unlocked(d,v))return SOM_PEQ_LOCKED;
    if(!som_prt_selectable(rt,d))return SOM_PEQ_UNAVAILABLE;
    if(som_prt_used(rt,who)+d->cost>som_prt_capacity(v?v->hero_level:0u))return SOM_PEQ_CAPACITY;
    return SOM_PEQ_OK;
}
static inline int som_prt_equip(SomPerkRuntime *rt,unsigned who,unsigned id,const SomPerkUnlockView *v){
    const int r=som_prt_check(rt,who,id,v);
    if(r!=SOM_PEQ_OK)return r;
    SomPerkHero *h=&rt->hero[who];
    const unsigned e=h->n++;
    h->def[e]=(uint8_t)som_perk_def_index(id);
    h->ready_at[e]=0;h->stacks[e]=0;h->charges_used[e]=0;h->stack_until[e]=0;h->counter[e]=0;h->fired_fight[e]=0;
    som_prt_reindex(h);
    return SOM_PEQ_OK;
}
static inline int som_prt_unequip(SomPerkRuntime *rt,unsigned who,unsigned id){
    if(!rt||who>=SOM_PRT_HEROES)return 0;
    SomPerkHero *h=&rt->hero[who];
    for(unsigned e=0;e<h->n;e++){
        const SomPerkDef *d=som_prt_def(h,e);
        if(!d||d->id!=id)continue;
        for(unsigned k=e;k+1u<h->n;k++){
            h->def[k]=h->def[k+1];h->ready_at[k]=h->ready_at[k+1];h->stacks[k]=h->stacks[k+1];
            h->charges_used[k]=h->charges_used[k+1];h->stack_until[k]=h->stack_until[k+1];
            h->counter[k]=h->counter[k+1];h->fired_fight[k]=h->fired_fight[k+1];
        }
        h->n--;
        for(unsigned t=0;t<SOM_PRT_TEMP;t++)if(h->temp[t].perk_id==id)memset(&h->temp[t],0,sizeof(h->temp[t]));
        som_prt_reindex(h);
        return 1;
    }
    return 0;
}
static inline void som_prt_clear_hero(SomPerkRuntime *rt,unsigned who){
    if(rt&&who<SOM_PRT_HEROES)memset(&rt->hero[who],0,sizeof(rt->hero[who]));
}
/* Drop what is no longer allowed (a loaded loadout, a level that changed,
 * a perk that became unavailable), last equipped first. Returns how many. */
static inline unsigned som_prt_revalidate(SomPerkRuntime *rt,unsigned who,const SomPerkUnlockView *v){
    if(!rt||who>=SOM_PRT_HEROES)return 0;
    uint16_t ids[SOM_PERK_MAX_EQUIPPED];unsigned n=0,dropped=0;
    SomPerkHero *h=&rt->hero[who];
    for(unsigned e=0;e<h->n;e++){const SomPerkDef *d=som_prt_def(h,e);if(d)ids[n++]=d->id;}
    som_prt_clear_hero(rt,who);
    for(unsigned i=0;i<n;i++)if(som_prt_equip(rt,who,ids[i],v)!=SOM_PEQ_OK)dropped++;
    return dropped;
}
/* Put ids as they are (a loaded record): known ids only, no repeats, no
 * rule checked. som_prt_revalidate applies the rules once the hero's level
 * and classes are known. */
static inline unsigned som_prt_set_raw(SomPerkRuntime *rt,unsigned who,const uint16_t *ids,unsigned n){
    if(!rt||who>=SOM_PRT_HEROES)return 0;
    som_prt_clear_hero(rt,who);
    SomPerkHero *h=&rt->hero[who];
    for(unsigned i=0;i<n&&h->n<SOM_PERK_MAX_EQUIPPED;i++){
        const int k=som_perk_def_index(ids[i]);
        if(k<0)continue;
        int dup=0;for(unsigned e=0;e<h->n;e++)if(h->def[e]==(uint8_t)k)dup=1;
        if(!dup)h->def[h->n++]=(uint8_t)k;
    }
    som_prt_reindex(h);
    return h->n;
}
static inline unsigned som_prt_ids(const SomPerkRuntime *rt,unsigned who,uint16_t *ids){
    if(!rt||who>=SOM_PRT_HEROES)return 0;
    unsigned n=0;
    for(unsigned e=0;e<rt->hero[who].n;e++){const SomPerkDef *d=som_prt_def(&rt->hero[who],e);if(d)ids[n++]=d->id;}
    return n;
}
static inline int som_prt_has(const SomPerkRuntime *rt,unsigned who,unsigned id){
    if(!rt||who>=SOM_PRT_HEROES)return 0;
    for(unsigned e=0;e<rt->hero[who].n;e++){const SomPerkDef *d=som_prt_def(&rt->hero[who],e);if(d&&d->id==id)return 1;}
    return 0;
}

/* ------------------------------------------------------------ persistence */
/* One line per hero for the progress record: "pl1 <who> <id>,<id>,...".
 * Older builds skip the line; this build skips ids it does not know. */
static inline size_t som_prt_format(const SomPerkRuntime *rt,unsigned who,char *out,size_t cap){
    if(!rt||who>=SOM_PRT_HEROES||!out||cap<8u)return 0;
    const SomPerkHero *h=&rt->hero[who];
    int w=snprintf(out,cap,"pl1 %u ",who);
    if(w<0||(size_t)w>=cap)return 0;
    size_t n=(size_t)w;
    if(!h->n){if(n+2u>cap)return 0;out[n++]='-';}
    for(unsigned e=0;e<h->n;e++){
        const SomPerkDef *d=som_prt_def(h,e);
        if(!d)continue;
        w=snprintf(out+n,cap-n,e?",%u":"%u",(unsigned)d->id);
        if(w<0||(size_t)w>=cap-n)return 0;
        n+=(size_t)w;
    }
    if(n+2u>cap)return 0;
    out[n++]='\n';out[n]=0;
    return n;
}
/* Ids of one "pl1" line, unknown ones skipped. Returns the hero, or -1. */
static inline int som_prt_parse(const char *line,uint16_t *ids,unsigned *n){
    unsigned who=0;int at=0;
    *n=0;
    if(!line||sscanf(line,"pl1 %u %n",&who,&at)<1||who>=SOM_PRT_HEROES||!at)return -1;
    const char *s=line+at;
    while(*s&&*s!='\n'&&*n<SOM_PERK_MAX_EQUIPPED){
        unsigned long v=0;int digits=0;
        while(*s>='0'&&*s<='9'){v=v*10u+(unsigned long)(*s-'0');s++;if(++digits>6)break;}
        if(digits&&v&&v<=0xFFFFu&&som_perk_def_by_id((unsigned)v)){
            int dup=0;for(unsigned i=0;i<*n;i++)if(ids[i]==v)dup=1;
            if(!dup)ids[(*n)++]=(uint16_t)v;
        }
        while(*s&&*s!=','&&*s!='\n')s++;
        if(*s==',')s++;
    }
    return (int)who;
}

/* ------------------------------------------------------------ dispatcher */
static inline void som_prt_push(SomPerkRuntime *rt,const SomPerkAction *a){
    if(rt->n_act>=SOM_PRT_ACTIONS){rt->dropped_actions++;return;}
    rt->act[rt->n_act++]=*a;
}
static inline void som_prt_delay(SomPerkRuntime *rt,const SomPerkDelayed *d){
    for(unsigned i=0;i<SOM_PRT_DELAYED;i++)
        if(!rt->delayed[i].used){rt->delayed[i]=*d;return;}
    rt->dropped_delayed++;
}
/* Facts the runtime keeps itself, taken before this event updates them. */
typedef struct {
    int chain, idle, first_of_fight, weapon_changed, alternated, after_switch, first_on_target;
    unsigned hits_on_target, last_hit_damage, distinct_weapons;
} SomPerkSeen;

static inline int som_prt_cond(const SomPerkRuntime *rt,const SomPerkHero *h,const SomPerkEv *ev,
                               const SomPerkSeen *s,const SomPerkCondDef *c){
    (void)rt;
    const unsigned hp=ev->hp,max=ev->hp_max;
    switch(c->type){
    case SOM_PCOND_EVERY_N: return 1;                     /* counted by the caller */
    case SOM_PCOND_IN_COMBO: return ev->combo>0u;
    case SOM_PCOND_COMBO_AT_LEAST: return ev->combo>=(unsigned)c->arg;
    case SOM_PCOND_CHAIN_HIT: return s->chain;
    case SOM_PCOND_IDLE_ATTACK: return s->idle;
    case SOM_PCOND_FIRST_HIT_OF_FIGHT: return s->first_of_fight;
    case SOM_PCOND_HITS_ON_TARGET: return s->hits_on_target>=(unsigned)c->arg;
    case SOM_PCOND_HITS_WITHIN:{
        unsigned n=0;
        if(ev->type==SOM_PEV_ATTACK_HIT)n++;                /* this hit */
        for(unsigned i=0;i<4u;i++)if(h->hits_ring[i]&&ev->frame-h->hits_ring[i]<=(uint32_t)c->arg2)n++;
        return n>=(unsigned)c->arg;
    }
    case SOM_PCOND_ONCE_PER_FIGHT: return 1;              /* the caller's fired_fight */
    case SOM_PCOND_FULL_HP: return max&&hp>=max;
    case SOM_PCOND_HP_BELOW: return max&&hp*100u<max*(unsigned)c->arg;
    case SOM_PCOND_HP_BELOW_HALF: return max&&hp*2u<max;
    case SOM_PCOND_OWNER_LOW_HP: return max&&hp*10u<max*3u;
    case SOM_PCOND_ENERGY_HALF: return ev->energy_pct>50u;
    case SOM_PCOND_TARGET_LOW_HP: return ev->target_max&&ev->target_hp*4u<ev->target_max;
    case SOM_PCOND_TARGET_HP_BELOW: return ev->target_max&&ev->target_hp*100u<ev->target_max*(unsigned)c->arg;
    case SOM_PCOND_TARGET_ISOLATED: return (ev->facts&SOM_PFACT_TARGET_ISOLATED)!=0;
    case SOM_PCOND_TARGET_STAGGERED: return (ev->facts&SOM_PFACT_TARGET_STAGGERED)!=0;
    case SOM_PCOND_TARGET_RECOVERING: return (ev->facts&SOM_PFACT_TARGET_RECOVERING)!=0;
    case SOM_PCOND_TARGET_UNAWARE: return (ev->facts&SOM_PFACT_TARGET_UNAWARE)!=0;
    case SOM_PCOND_BEHIND_TARGET: return (ev->facts&SOM_PFACT_BEHIND)!=0;
    case SOM_PCOND_TARGET_UNDEAD: return (ev->facts&SOM_PFACT_TARGET_UNDEAD)!=0;
    case SOM_PCOND_TARGET_LARGE: return (ev->facts&(SOM_PFACT_TARGET_LARGE|SOM_PFACT_TARGET_BOSS))!=0;
    case SOM_PCOND_TARGET_FAR: return ev->dist>64u;
    case SOM_PCOND_TARGET_CLOSE: return ev->dist<=24u;
    case SOM_PCOND_TARGET_SLOWED: return (ev->facts&(SOM_PFACT_TARGET_SLOWED|SOM_PFACT_TARGET_FROZEN))!=0;
    case SOM_PCOND_TARGET_FROZEN: return (ev->facts&SOM_PFACT_TARGET_FROZEN)!=0;
    case SOM_PCOND_TARGET_HURT_ALLY: return (ev->facts&SOM_PFACT_TARGET_HURT_ALLY)!=0;
    case SOM_PCOND_TARGET_ATTACKING_ALLY: return (ev->facts&SOM_PFACT_TARGET_ATTACKING_ALLY)!=0;
    case SOM_PCOND_CHARGED_HIT: return ev->charge>=1u;
    case SOM_PCOND_CHARGE_AT_LEAST: return ev->charge>=(unsigned)c->arg;
    case SOM_PCOND_CHARGING: return (ev->facts&SOM_PFACT_CHARGING)!=0;
    case SOM_PCOND_WEAPON_GLOVE: return ev->weapon==SOM_PW_GLOVE;
    case SOM_PCOND_WEAPON_SWORD: return ev->weapon==SOM_PW_SWORD;
    case SOM_PCOND_WEAPON_AXE: return ev->weapon==SOM_PW_AXE;
    case SOM_PCOND_WEAPON_WHIP: return ev->weapon==SOM_PW_WHIP;
    case SOM_PCOND_WEAPON_RANGED: return som_pw_ranged(ev->weapon);
    case SOM_PCOND_WEAPON_CHANGED: return s->weapon_changed;
    case SOM_PCOND_WEAPON_ALTERNATED: return s->alternated;
    case SOM_PCOND_DISTINCT_WEAPONS: return s->distinct_weapons>=(unsigned)c->arg;
    case SOM_PCOND_AFTER_SWITCH: return s->after_switch;
    case SOM_PCOND_OUT_OF_COMBAT: return (ev->facts&SOM_PFACT_OUT_OF_COMBAT)!=0;
    case SOM_PCOND_NO_MONSTER_CLOSE: return (ev->facts&SOM_PFACT_NO_MONSTER_CLOSE)!=0;
    case SOM_PCOND_BULLET_TIME: return (ev->facts&SOM_PFACT_BULLET_TIME)!=0;
    case SOM_PCOND_OWN_TIME_EFFECT: return (ev->facts&SOM_PFACT_OWN_TIME_EFFECT)!=0;
    case SOM_PCOND_STATIONARY: return ev->still_frames>=(unsigned)c->arg;
    case SOM_PCOND_ALLY_NEAR_TARGET: return (ev->facts&SOM_PFACT_ALLY_NEAR_TARGET)!=0;
    case SOM_PCOND_ALLY_LOW_HP_NEAR: return (ev->facts&SOM_PFACT_ALLY_LOW_HP_NEAR)!=0;
    case SOM_PCOND_OWNER_GUARDING: return (ev->facts&SOM_PFACT_OWNER_GUARDING)!=0;
    case SOM_PCOND_FAMILIAR_ALIVE: return (ev->facts&SOM_PFACT_FAMILIAR_ALIVE)!=0;
    case SOM_PCOND_NO_FAMILIAR: return !(ev->facts&SOM_PFACT_FAMILIAR_ALIVE);
    case SOM_PCOND_COOP_SAME_TARGET: return (ev->facts&SOM_PFACT_COOP_SAME_TARGET)!=0;
    case SOM_PCOND_SPELL_KILL: return (ev->facts&SOM_PFACT_SPELL)!=0;
    case SOM_PCOND_NO_DAMAGE_FIGHT: return !h->hurt_this_fight;
    default: return 0;                                   /* not run by this runtime */
    }
}
/* Does an effect's "only" qualifier hold for this event? */
static inline int som_prt_only(const SomPerkFx *f,const SomPerkEv *ev,const SomPerkSeen *s){
    switch(f->only){
    case SOM_PONLY_NONE: return 1;
    case SOM_PONLY_FIRST_HIT_ON_TARGET: return s->first_on_target;
    case SOM_PONLY_LATER_HITS: return !s->first_on_target;
    case SOM_PONLY_BEHIND_TARGET: return (ev->facts&SOM_PFACT_BEHIND)!=0;
    case SOM_PONLY_FRONT: return !(ev->facts&SOM_PFACT_BEHIND);
    case SOM_PONLY_RANGED: return som_pw_ranged(ev->weapon);
    case SOM_PONLY_MELEE: return !som_pw_ranged(ev->weapon);
    case SOM_PONLY_LOW_HP: return ev->hp_max&&ev->hp*4u<ev->hp_max;
    case SOM_PONLY_ABOVE_HALF: return ev->hp_max&&ev->hp*2u>=ev->hp_max;
    case SOM_PONLY_AFTER_SWITCH: return s->after_switch;
    case SOM_PONLY_SAME_WEAPON_TWICE: return !s->weapon_changed;
    case SOM_PONLY_UNDEAD_OR_BOSS: return (ev->facts&(SOM_PFACT_TARGET_UNDEAD|SOM_PFACT_TARGET_BOSS))!=0;
    case SOM_PONLY_OTHER: return !(ev->facts&(SOM_PFACT_TARGET_UNDEAD|SOM_PFACT_TARGET_BOSS));
    default: return 0;
    }
}
static inline unsigned som_prt_base(const SomPerkFx *f,const SomPerkEv *ev,const SomPerkSeen *s){
    switch(f->base){
    case SOM_PBASE_HIT: return ev->amount;
    case SOM_PBASE_LAST_HIT: return s->last_hit_damage?s->last_hit_damage:ev->amount;
    case SOM_PBASE_MAX_HP: case SOM_PBASE_OWNER_MAX_HP: return ev->hp_max;
    case SOM_PBASE_HEAL: return ev->amount;
    default: return 100u;
    }
}
static inline unsigned som_prt_share(const SomPerkFx *f,const SomPerkEv *ev,const SomPerkSeen *s){
    const long v=(long)som_prt_base(f,ev,s)*(long)f->a/100;
    return v<=0?0u:v<1?1u:v>0xFFFF?0xFFFFu:(unsigned)v;
}
static inline void som_prt_temp_add(SomPerkHero *h,const SomPerkDef *d,const SomPerkFx *f,uint32_t frame,
                                    unsigned stacks){
    SomPerkTemp *t=NULL;
    for(unsigned i=0;i<SOM_PRT_TEMP&&!t;i++)if(h->temp[i].perk_id==d->id&&h->temp[i].mod==f->modifier)t=&h->temp[i];
    for(unsigned i=0;i<SOM_PRT_TEMP&&!t;i++)if(!h->temp[i].perk_id||(h->temp[i].until&&h->temp[i].until<frame))t=&h->temp[i];
    if(!t)return;                                         /* full: the oldest still run */
    t->perk_id=d->id;t->mod=f->modifier;
    t->pct=(int16_t)(f->a*(int)(stacks?stacks:1u));
    t->charges=(uint8_t)(f->c>0?(unsigned)f->c:(f->modifier==SOM_PMOD_NEXT_HIT_DAMAGE||f->modifier==SOM_PMOD_CRIT_NEXT?1u:0u));
    t->until=f->frames?frame+f->frames:0u;
}
/* Run one effect. bonus: the event's extra damage in percent (ATTACK_HIT). */
static inline void som_prt_effect(SomPerkRuntime *rt,SomPerkHero *h,unsigned e,const SomPerkDef *d,
                                  const SomPerkFx *f,const SomPerkEv *ev,const SomPerkSeen *s,int *bonus,
                                  unsigned depth){
    const int bonus_before=*bonus;
    SomPerkAction a;memset(&a,0,sizeof(a));
    a.who=ev->who;a.depth=(uint8_t)depth;a.slot=ev->slot;a.target_id=ev->target_id;a.perk_id=d->id;
    if(f->target==SOM_PTGT_NEAREST_OTHER)a.aim=SOM_PAT_NEAREST_OTHER;
    else if(f->target==SOM_PTGT_BEHIND_IN_LINE)a.aim=SOM_PAT_BEHIND_IN_LINE;
    a.radius=(uint16_t)(f->b>0?f->b:0);
    switch(f->type){
    case SOM_PFX_DAMAGE_BONUS:
        if(!som_prt_only(f,ev,s))break;
        if(f->flags&SOM_PFXF_PER_COMBO){                  /* Crescendo: a% per combo hit, c% at most */
            const int v=f->a*(int)ev->combo;*bonus+=f->c&&v>f->c?f->c:v;
        }else *bonus+=f->a;
        break;
    case SOM_PFX_CRIT:
        if(som_prt_only(f,ev,s))*bonus+=100;
        break;
    case SOM_PFX_STACKING_DAMAGE:
        if(f->clear==SOM_PCLEAR_DAMAGE_POST&&ev->type==SOM_PEV_DAMAGE_POST){h->stacks[e]=0;break;}
        if(ev->type==SOM_PEV_DAMAGE_POST)break;
        if(h->stacks[e]<d->max_stacks)h->stacks[e]++;
        if(ev->type==SOM_PEV_ATTACK_HIT)*bonus+=f->a*(int)h->stacks[e];
        else{                                             /* Clean Fight: rides on later hits */
            SomPerkFx t=*f;t.modifier=SOM_PMOD_DAMAGE;t.frames=0;t.c=0;
            som_prt_temp_add(h,d,&t,ev->frame,h->stacks[e]);
        }
        break;
    case SOM_PFX_TEMP_MODIFIER:
        if(d->policy==SOM_PPOL_ADD){
            if(h->stack_until[e]<ev->frame)h->stacks[e]=0;
            if(h->stacks[e]<d->max_stacks)h->stacks[e]++;
            h->stack_until[e]=ev->frame+(f->frames?f->frames:SOM_PRT_CHAIN_FRAMES);
            som_prt_temp_add(h,d,f,ev->frame,h->stacks[e]);
        }else som_prt_temp_add(h,d,f,ev->frame,1u);
        break;
    case SOM_PFX_SECONDARY_STRIKE:
        a.type=SOM_PACT_DAMAGE;a.amount=(uint16_t)som_prt_share(f,ev,s);
        if(ev->slot<0)break;
        som_prt_push(rt,&a);break;
    case SOM_PFX_AREA_DAMAGE:
        if(ev->slot<0)break;
        a.type=SOM_PACT_AREA;a.aim=SOM_PAT_AREA;a.amount=(uint16_t)som_prt_share(f,ev,s);
        a.exclude=(f->flags&SOM_PFXF_EXCLUDE_TARGET)!=0;som_prt_push(rt,&a);break;
    case SOM_PFX_DELAYED_DAMAGE: case SOM_PFX_DELAYED_AREA_DAMAGE: case SOM_PFX_DAMAGE_OVER_TIME:{
        if(ev->slot<0)break;
        SomPerkDelayed q;memset(&q,0,sizeof(q));
        q.used=f->type==SOM_PFX_DELAYED_AREA_DAMAGE?2u:1u;q.who=ev->who;q.slot=ev->slot;q.target_id=ev->target_id;
        q.depth=(uint8_t)depth;q.amount=(uint16_t)som_prt_share(f,ev,s);q.radius=a.radius;q.perk_id=d->id;
        q.release=(f->flags&SOM_PFXF_RELEASE_BULLET)?1u:0u;
        const unsigned ticks=f->type==SOM_PFX_DAMAGE_OVER_TIME?(f->c?(unsigned)f->c:1u):1u;
        for(unsigned t=0;t<ticks;t++){q.at=ev->frame+(f->delay?f->delay:1u)*(t+1u);som_prt_delay(rt,&q);}
        break;
    }
    case SOM_PFX_EXECUTE:
        if(ev->slot<0)break;
        if(!(d->flags&SOM_PDF_BOSS_OK)&&(ev->facts&SOM_PFACT_TARGET_BOSS))break;
        a.type=SOM_PACT_EXECUTE;som_prt_push(rt,&a);break;
    case SOM_PFX_HEAL:
        a.type=SOM_PACT_HEAL;a.amount=(uint16_t)som_prt_share(f,ev,s);som_prt_push(rt,&a);break;
    case SOM_PFX_PARTY_HEAL:
        a.type=SOM_PACT_PARTY_HEAL;a.amount=(uint16_t)f->a;som_prt_push(rt,&a);break;   /* % of each max */
    case SOM_PFX_SHIELD:
        if(ev->type==SOM_PEV_DAMAGE_POST){
            if(!som_prt_only(f,ev,s))break;
            unsigned back=som_prt_share(f,ev,s);
            if(back>ev->amount)back=ev->amount;
            if(back){a.type=SOM_PACT_GIVE_BACK;a.amount=(uint16_t)back;som_prt_push(rt,&a);}
        }else{                                            /* a pool for later hits */
            if(h->shield_until<ev->frame)h->shield=0;
            const unsigned add=som_prt_share(f,ev,s),cap=add*(d->max_stacks?d->max_stacks:1u);
            unsigned v=h->shield+add;if(v>cap)v=cap;
            h->shield=(uint16_t)v;h->shield_until=ev->frame+(f->frames?f->frames:600u);
        }
        break;
    case SOM_PFX_ENERGY:
        a.type=SOM_PACT_ENERGY;a.amount=(uint16_t)(int16_t)f->a;som_prt_push(rt,&a);break;
    case SOM_PFX_ATTACK_SPEED:
        a.type=SOM_PACT_GAUGE;a.amount=(uint16_t)f->a;som_prt_push(rt,&a);break;
    case SOM_PFX_STAGGER:
        if(ev->slot<0)break;
        a.type=SOM_PACT_STAGGER;if(a.radius)a.aim=SOM_PAT_AREA;
        a.exclude=(f->flags&SOM_PFXF_EXCLUDE_TARGET)!=0;som_prt_push(rt,&a);break;
    case SOM_PFX_BULLET_TIME:
        if(f->frames){a.type=SOM_PACT_BULLET;a.frames=f->frames;}
        else{a.type=SOM_PACT_BULLET_EXTEND;a.frames=(uint16_t)f->a;a.amount=(uint16_t)f->c;}
        som_prt_push(rt,&a);break;
    case SOM_PFX_TIME_SCALE:
        if(ev->slot<0)break;
        a.type=SOM_PACT_TIME;a.speed=(uint8_t)(f->a<0?0:f->a>99?99:f->a);a.frames=f->frames;
        a.amount=(uint16_t)f->c;                          /* frames on a boss, 0 = same */
        if(a.radius)a.aim=SOM_PAT_AREA;
        som_prt_push(rt,&a);break;
    default: break;                                       /* not run by this runtime */
    }
    if(*bonus!=bonus_before){rt->bonus_perk=rt->bonus_perks?0u:d->id;rt->bonus_perks++;}
}
/* Temp modifiers that ride on a hit: next-hit damage, timed damage, a sure
 * critical. Charges are spent here. */
static inline int som_prt_hit_temps(SomPerkRuntime *rt,SomPerkHero *h,uint32_t frame){
    int pct=0;rt->temp_perk=0;
    for(unsigned i=0;i<SOM_PRT_TEMP;i++){
        SomPerkTemp *t=&h->temp[i];
        if(!t->perk_id)continue;
        if(t->until&&t->until<frame){memset(t,0,sizeof(*t));continue;}
        switch(t->mod){
        case SOM_PMOD_NEXT_HIT_DAMAGE: case SOM_PMOD_CRIT_NEXT:
            pct+=t->mod==SOM_PMOD_CRIT_NEXT?100:t->pct;rt->temp_perk=t->perk_id;
            if(t->charges&&!--t->charges)memset(t,0,sizeof(*t));
            break;
        case SOM_PMOD_DAMAGE: pct+=t->pct;rt->temp_perk=t->perk_id;break;
        default: break;
        }
    }
    return pct;
}
static inline void som_prt_seen(const SomPerkHero *h,const SomPerkEv *ev,SomPerkSeen *s){
    memset(s,0,sizeof(*s));
    s->chain=h->last_hit_frame&&ev->frame-h->last_hit_frame<=SOM_PRT_CHAIN_FRAMES;
    s->idle=!h->last_hit_frame||ev->frame-h->last_hit_frame>=SOM_PRT_IDLE_FRAMES;
    s->first_of_fight=!h->hit_this_fight;
    s->weapon_changed=h->last_hit_frame&&h->last_hit_weapon!=ev->weapon;
    s->alternated=h->last_hit_frame&&som_pw_ranged(h->last_hit_weapon)!=som_pw_ranged(ev->weapon);
    s->after_switch=h->switch_frame&&ev->frame-h->switch_frame<=SOM_PRT_SWITCH_FRAMES;
    s->last_hit_damage=h->last_hit_damage;
    unsigned w=h->weapons_in_combo;
    if(ev->weapon<8u)w|=1u<<ev->weapon;
    for(;w;w&=w-1u)s->distinct_weapons++;
    if(ev->slot>=0&&(unsigned)ev->slot<SOM_PRT_MONSTERS){
        const unsigned k=(unsigned)ev->slot;
        const int same=h->target_id[k]==ev->target_id&&h->target_seen[k];
        s->first_on_target=!same;
        const int fresh=same&&ev->frame-h->target_first[k]<=SOM_PRT_TARGET_FRAMES;
        s->hits_on_target=(fresh?h->target_hits[k]:0u)+1u;
    }
}
static inline void som_prt_after_hit(SomPerkHero *h,const SomPerkEv *ev,const SomPerkSeen *s){
    if(ev->slot>=0&&(unsigned)ev->slot<SOM_PRT_MONSTERS){
        const unsigned k=(unsigned)ev->slot;
        if(s->hits_on_target==1u){h->target_first[k]=ev->frame;}
        h->target_hits[k]=(uint8_t)(s->hits_on_target>250u?250u:s->hits_on_target);
        h->target_id[k]=ev->target_id;h->target_seen[k]=1;
    }
    h->prev_hit_weapon=h->last_hit_weapon;h->last_hit_weapon=ev->weapon;
    h->last_hit_frame=ev->frame?ev->frame:1u;h->last_hit_damage=ev->amount;h->hit_this_fight=1;
    h->hits_ring[h->ring_at++&3u]=ev->frame?ev->frame:1u;
    if(!ev->combo)h->weapons_in_combo=0;
    if(ev->weapon<8u)h->weapons_in_combo|=(uint8_t)(1u<<ev->weapon);
}
/* Report one event. Returns how many perks fired. The actions wait in
 * rt->act until som_prt_take; delayed ones are released by som_prt_tick. */
static inline unsigned som_prt_emit(SomPerkRuntime *rt,const SomPerkEv *ev){
    if(!rt||!ev||ev->who>=SOM_PRT_HEROES||ev->type>=SOM_PEV_COUNT)return 0;
    if(ev->type==SOM_PEV_ENEMY_KILLED&&ev->slot>=0&&(unsigned)ev->slot<SOM_PRT_MONSTERS){
        const unsigned k=(unsigned)ev->slot;
        if(rt->kill_frame[k]&&rt->kill_id[k]==ev->target_id&&ev->frame-rt->kill_frame[k]<60u)return 0;
        rt->kill_frame[k]=ev->frame?ev->frame:1u;rt->kill_id[k]=ev->target_id;
    }
    rt->events++;
    SomPerkHero *h=&rt->hero[ev->who];
    if(ev->frame!=rt->budget_frame){rt->budget_frame=ev->frame;rt->budget_used=0;}
    if(!ev->depth)rt->root_used=0;
    /* bookkeeping that holds with or without perks */
    if(ev->type==SOM_PEV_WEAPON_SWITCH){h->switch_frame=ev->frame?ev->frame:1u;h->weapon=ev->weapon;}
    if(ev->type==SOM_PEV_DAMAGE_POST&&!ev->depth){
        h->hurt_this_fight=1;
        if(h->shield&&h->shield_until>=ev->frame&&ev->amount){
            const unsigned back=h->shield<ev->amount?h->shield:ev->amount;
            h->shield=(uint16_t)(h->shield-back);
            SomPerkAction a;memset(&a,0,sizeof(a));
            a.type=SOM_PACT_GIVE_BACK;a.who=ev->who;a.slot=ev->slot;a.amount=(uint16_t)back;som_prt_push(rt,&a);
        }
    }
    if(ev->type==SOM_PEV_COMBAT_END){
        for(unsigned e=0;e<h->n;e++)h->fired_fight[e]=0;
    }
    SomPerkSeen seen;som_prt_seen(h,ev,&seen);
    unsigned fired=0;
    rt->bonus_perk=0;rt->bonus_perks=0;rt->n_fired=0;
    int bonus=0;
    const unsigned depth=ev->depth;
    if(depth>SOM_PRT_MAX_DEPTH){rt->skipped_depth++;goto done;}
    for(uint16_t bits=h->by_event[ev->type];bits;bits&=(uint16_t)(bits-1u)){
        unsigned e=0;while(!(bits>>e&1u))e++;
        const SomPerkDef *d=som_prt_def(h,e);
        if(!d)continue;
        if(depth&&!(d->flags&SOM_PDF_GENERATED_OK)){rt->skipped_depth++;continue;}
        if(ev->frame<h->ready_at[e])continue;
        if((ev->facts&SOM_PFACT_TARGET_BOSS)&&!(d->flags&SOM_PDF_BOSS_OK))continue;
        int ok=1,every=0,once=0;
        for(unsigned c=0;c<d->n_cond&&ok;c++){
            if(d->cond[c].type==SOM_PCOND_EVERY_N)every=d->cond[c].arg;
            if(d->cond[c].type==SOM_PCOND_ONCE_PER_FIGHT)once=1;
            ok=som_prt_cond(rt,h,ev,&seen,&d->cond[c]);
        }
        /* a clearing event (Sword Saint's hurt) runs without the conditions */
        int clearing=0;
        for(unsigned x=0;x<d->n_fx;x++)
            if(d->fx[x].type==SOM_PFX_STACKING_DAMAGE&&d->fx[x].clear==SOM_PCLEAR_DAMAGE_POST&&
               ev->type==SOM_PEV_DAMAGE_POST)clearing=1;
        if(clearing){
            h->stacks[e]=0;
            for(unsigned t=0;t<SOM_PRT_TEMP;t++)if(h->temp[t].perk_id==d->id)memset(&h->temp[t],0,sizeof(h->temp[t]));
            continue;
        }
        /* a broken chain or two of a kind start the stacks over */
        for(unsigned x=0;x<d->n_fx&&ev->type==SOM_PEV_ATTACK_HIT;x++){
            if(d->fx[x].type!=SOM_PFX_STACKING_DAMAGE)continue;
            if((d->fx[x].clear==SOM_PCLEAR_CHAIN_BREAK&&!seen.chain)||
               (d->fx[x].clear==SOM_PCLEAR_SAME_KIND&&!seen.alternated))h->stacks[e]=0;
        }
        if(!ok)continue;
        if(once&&h->fired_fight[e])continue;
        if(every>0){h->counter[e]++;if(h->counter[e]%(unsigned)every)continue;h->counter[e]=0;}
        if(d->charges&&h->charges_used[e]>=d->charges)continue;
        if(rt->budget_used>=SOM_PRT_FRAME_BUDGET||rt->root_used>=SOM_PRT_ROOT_BUDGET){rt->skipped_budget++;continue;}
        rt->budget_used++;rt->root_used++;rt->activations++;fired++;
        if(rt->n_fired<SOM_PRT_ROOT_BUDGET)rt->fired_ids[rt->n_fired++]=d->id;
        for(unsigned x=0;x<d->n_fx;x++)
            if(!d->fx[x].on||d->fx[x].on==ev->type)som_prt_effect(rt,h,e,d,&d->fx[x],ev,&seen,&bonus,depth+1u);
        if(d->cooldown)h->ready_at[e]=ev->frame+d->cooldown;
        if(once)h->fired_fight[e]=1;
        if(d->charges)h->charges_used[e]++;
        if(d->feedback&&*d->feedback){
            SomPerkAction a;memset(&a,0,sizeof(a));a.type=SOM_PACT_SAY;a.who=ev->who;a.perk_id=d->id;som_prt_push(rt,&a);
        }
    }
done:
    if(ev->type==SOM_PEV_ATTACK_HIT){
        if(!depth){
            const int t=som_prt_hit_temps(rt,h,ev->frame);
            if(t){bonus+=t;rt->bonus_perk=rt->bonus_perks?0u:rt->temp_perk;rt->bonus_perks++;}
        }
        if(bonus&&ev->slot>=0&&ev->amount){
            SomPerkAction a;memset(&a,0,sizeof(a));
            a.who=ev->who;a.slot=ev->slot;a.target_id=ev->target_id;a.depth=(uint8_t)(depth+1u);
            const long v=(long)ev->amount*(long)(bonus<0?-bonus:bonus)/100;
            a.amount=(uint16_t)(v<1?1:v>0xFFFF?0xFFFF:v);
            a.type=bonus>0?SOM_PACT_DAMAGE:SOM_PACT_REFUND;
            a.bonus=1;a.perk_id=rt->bonus_perks==1u?rt->bonus_perk:0u;
            som_prt_push(rt,&a);
        }
        if(!depth)som_prt_after_hit(h,ev,&seen);
    }
    if(ev->type==SOM_PEV_COMBAT_END){h->hit_this_fight=0;h->hurt_this_fight=0;h->fight_frame=ev->frame;}
    return fired;
}
/* One frame passes: expire temps, release delayed damage. */
static inline void som_prt_tick(SomPerkRuntime *rt,uint32_t frame,int bullet_time){
    if(!rt)return;
    for(unsigned i=0;i<SOM_PRT_DELAYED;i++){
        SomPerkDelayed *q=&rt->delayed[i];
        if(!q->used)continue;
        const int due=q->release==1u?!bullet_time:frame>=q->at;
        if(!due)continue;
        SomPerkAction a;memset(&a,0,sizeof(a));
        a.type=q->used==3u?SOM_PACT_HEAL:q->used==2u?SOM_PACT_AREA:SOM_PACT_DAMAGE;
        if(q->used==2u)a.aim=SOM_PAT_AREA;
        a.who=q->who;a.slot=q->slot;a.target_id=q->target_id;a.depth=q->depth;a.amount=q->amount;
        a.radius=q->radius;a.perk_id=q->perk_id;
        som_prt_push(rt,&a);
        memset(q,0,sizeof(*q));
    }
}
/* Hand the actions over (copy at most cap) and empty the queue. */
static inline unsigned som_prt_take(SomPerkRuntime *rt,SomPerkAction *out,unsigned cap){
    if(!rt)return 0;
    unsigned n=rt->n_act<cap?rt->n_act:cap;
    if(out&&n)memcpy(out,rt->act,n*sizeof(*out));
    rt->n_act=0;
    return n;
}
/* A monster slot got a new occupant: its delayed damage is void. */
static inline void som_prt_forget_slot(SomPerkRuntime *rt,unsigned slot){
    if(!rt||slot>=SOM_PRT_MONSTERS)return;
    for(unsigned i=0;i<SOM_PRT_DELAYED;i++)if(rt->delayed[i].used&&rt->delayed[i].slot==(int8_t)slot)
        memset(&rt->delayed[i],0,sizeof(rt->delayed[i]));
}
/* After a state load or a reset: nothing in flight survives. Loadouts do. */
static inline void som_prt_flush(SomPerkRuntime *rt){
    if(!rt)return;
    rt->n_act=0;memset(rt->delayed,0,sizeof(rt->delayed));
    memset(rt->kill_frame,0,sizeof(rt->kill_frame));
    for(unsigned w=0;w<SOM_PRT_HEROES;w++){
        SomPerkHero *h=&rt->hero[w];
        memset(h->temp,0,sizeof(h->temp));memset(h->ready_at,0,sizeof(h->ready_at));
        memset(h->stacks,0,sizeof(h->stacks));memset(h->counter,0,sizeof(h->counter));
        memset(h->fired_fight,0,sizeof(h->fired_fight));memset(h->charges_used,0,sizeof(h->charges_used));
        h->shield=0;h->last_hit_frame=0;h->switch_frame=0;memset(h->hits_ring,0,sizeof(h->hits_ring));
        memset(h->target_seen,0,sizeof(h->target_seen));h->hit_this_fight=h->hurt_this_fight=0;
    }
}
/* For the AI: does the hero carry a perk that wants this tactic? */
static inline int som_prt_tactic(const SomPerkRuntime *rt,unsigned who,unsigned tactic){
    if(!rt||who>=SOM_PRT_HEROES||!tactic)return 0;
    for(unsigned e=0;e<rt->hero[who].n;e++){const SomPerkDef *d=som_prt_def(&rt->hero[who],e);if(d&&d->tactic==tactic)return 1;}
    return 0;
}
#endif
