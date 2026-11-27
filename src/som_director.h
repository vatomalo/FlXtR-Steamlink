#ifndef FLXTR_SOM_DIRECTOR_H
#define FLXTR_SOM_DIRECTOR_H
/* The camera director (player, 2026-11-27: "this game must look like its
 * cinematics were planned, use a WFC state machine for camera?").
 *
 * Time is cut into beats (SOM_DIR_BEAT frames). The director keeps a plan of
 * the next SOM_DIR_HORIZON beats and solves it like wave function collapse
 * in one dimension: every beat starts as the set of all tiles, a tile being
 * (shot, how many beats that shot has already run). Three kinds of rule
 * narrow the sets:
 *   - what the scene allows (a dialogue, a fight, peace; what fits on screen;
 *     whether 1.5x is affordable),
 *   - pins from events (a whack is a punch-in now, a map entry an
 *     establishing pull-out, a hard hit on the hero a reaction close-up),
 *   - film grammar between neighbouring beats: which shot may follow which
 *     (no cut from one close-up to another, a punch-in always ends in a
 *     pull-out, a dialogue alternates two-shot and speaker), and how long a
 *     shot holds (its minimum and maximum beats).
 * Arc consistency is propagated over the timeline, then the most constrained
 * beat is collapsed by weighted choice (scene weights, the old plan kept,
 * variety: recently used shots weigh less), propagated again, and so on.
 * A contradiction drops the scene rules and keeps the grammar; the last
 * resort is a wide shot. The plan is solved again at every beat and at once
 * when an event arrives; som_camera.h plays the beat (state machine).
 * Pure C, no I/O, no WRAM: tested on its own (tests/som_director_test.c). */
#include <stdint.h>
#include <string.h>

#define SOM_DIR_BEAT 24u               /* frames: 0.4 s */
#define SOM_DIR_HORIZON 8u             /* beats planned ahead: 3.2 s */
#define SOM_DIR_PHASES 5u              /* run counter 0..4, 4 = "4 or more" */

enum {
    SOM_DIR_WIDE,       /* 1x, the game's own view */
    SOM_DIR_ESTABLISH,  /* arriving: from 2x on the hero, a slow pull-out */
    SOM_DIR_MEDIUM,     /* 1.5x, the hero and the foes */
    SOM_DIR_LEAD,       /* 1.5x, the hero on a third, room toward the foe */
    SOM_DIR_CLOSE,      /* 2x on the hero */
    SOM_DIR_FOE,        /* 2x on the foe he fights */
    SOM_DIR_PARTY,      /* 1.5x, the party together */
    SOM_DIR_TWO,        /* dialogue: the hero and the speaker */
    SOM_DIR_SPEAKER,    /* dialogue: the speaker close */
    SOM_DIR_PUNCH,      /* 2x on the event's spot, shake */
    SOM_DIR_PULL,       /* a quick pull back to 1x */
    SOM_DIR_IDLE,       /* 2x on the hero standing still */
    SOM_DIR_SHOTS
};
static const char *const som_dir_names[SOM_DIR_SHOTS]={
    "wide","establish","medium","lead room","close","foe","party","two-shot","speaker",
    "punch-in","pull-out","idle"};
#define SOM_DIR_BIT(s) (1u<<(s))
#define SOM_DIR_TILES (SOM_DIR_SHOTS*SOM_DIR_PHASES)
typedef uint64_t SomDirSet;
#define SOM_DIR_TILE(s,k) ((s)*SOM_DIR_PHASES+(k))
#define SOM_DIR_SHOT_OF(t) ((t)/SOM_DIR_PHASES)
#define SOM_DIR_RUN_OF(t) ((t)%SOM_DIR_PHASES)

/* Beats a shot holds at least / at most (0 = no limit). */
static const uint8_t som_dir_min[SOM_DIR_SHOTS]={1,3,3,2,2,2,2,2,2,2,1,3};
static const uint8_t som_dir_max[SOM_DIR_SHOTS]={0,4,0,5,4,3,0,0,5,3,2,0};
/* What may come after a shot has run its course. */
static const uint16_t som_dir_follow[SOM_DIR_SHOTS]={
    /* wide      */ (uint16_t)(0xFFFFu&~SOM_DIR_BIT(SOM_DIR_PULL)),   /* nothing to pull out of */
    /* establish */ SOM_DIR_BIT(SOM_DIR_WIDE)|SOM_DIR_BIT(SOM_DIR_PARTY)|SOM_DIR_BIT(SOM_DIR_MEDIUM)|
                    SOM_DIR_BIT(SOM_DIR_LEAD)|SOM_DIR_BIT(SOM_DIR_TWO),
    /* medium    */ SOM_DIR_BIT(SOM_DIR_WIDE)|SOM_DIR_BIT(SOM_DIR_LEAD)|SOM_DIR_BIT(SOM_DIR_FOE)|
                    SOM_DIR_BIT(SOM_DIR_PARTY)|SOM_DIR_BIT(SOM_DIR_CLOSE)|SOM_DIR_BIT(SOM_DIR_PUNCH),
    /* lead      */ SOM_DIR_BIT(SOM_DIR_MEDIUM)|SOM_DIR_BIT(SOM_DIR_FOE)|SOM_DIR_BIT(SOM_DIR_WIDE)|
                    SOM_DIR_BIT(SOM_DIR_PUNCH),
    /* close     */ SOM_DIR_BIT(SOM_DIR_MEDIUM)|SOM_DIR_BIT(SOM_DIR_WIDE)|SOM_DIR_BIT(SOM_DIR_PARTY)|
                    SOM_DIR_BIT(SOM_DIR_PULL)|SOM_DIR_BIT(SOM_DIR_LEAD),
    /* foe       */ SOM_DIR_BIT(SOM_DIR_MEDIUM)|SOM_DIR_BIT(SOM_DIR_LEAD)|SOM_DIR_BIT(SOM_DIR_WIDE)|
                    SOM_DIR_BIT(SOM_DIR_PULL)|SOM_DIR_BIT(SOM_DIR_PUNCH),
    /* party     */ SOM_DIR_BIT(SOM_DIR_WIDE)|SOM_DIR_BIT(SOM_DIR_MEDIUM)|SOM_DIR_BIT(SOM_DIR_CLOSE)|
                    SOM_DIR_BIT(SOM_DIR_LEAD),
    /* two       */ SOM_DIR_BIT(SOM_DIR_SPEAKER)|SOM_DIR_BIT(SOM_DIR_WIDE),
    /* speaker   */ SOM_DIR_BIT(SOM_DIR_TWO)|SOM_DIR_BIT(SOM_DIR_WIDE)|SOM_DIR_BIT(SOM_DIR_PULL),
    /* punch     */ SOM_DIR_BIT(SOM_DIR_PULL)|SOM_DIR_BIT(SOM_DIR_MEDIUM),
    /* pull      */ SOM_DIR_BIT(SOM_DIR_WIDE)|SOM_DIR_BIT(SOM_DIR_MEDIUM)|SOM_DIR_BIT(SOM_DIR_PARTY)|
                    SOM_DIR_BIT(SOM_DIR_LEAD),
    /* idle      */ SOM_DIR_BIT(SOM_DIR_WIDE)|SOM_DIR_BIT(SOM_DIR_PULL),
};
/* Shots that may hold as long as the scene lasts without tiring. */
#define SOM_DIR_STEADY (SOM_DIR_BIT(SOM_DIR_WIDE)|SOM_DIR_BIT(SOM_DIR_IDLE)|SOM_DIR_BIT(SOM_DIR_TWO))
/* Shots only an event may start (pinned), never chosen freely. */
#define SOM_DIR_PINNED_ONLY (SOM_DIR_BIT(SOM_DIR_PUNCH)|SOM_DIR_BIT(SOM_DIR_ESTABLISH))

/* The scene, filled by the camera every frame. */
enum {SOM_DIR_PEACE,SOM_DIR_FIGHT,SOM_DIR_TALK,SOM_DIR_HOLD};   /* HOLD: menus, choices */
typedef struct {
    int scene;
    uint16_t fits;          /* shots whose subjects fit on screen at their zoom now */
    int still;              /* the hero has stood still SOM_CAM_IDLE frames */
    int intensity;          /* fight: 0 calm .. 3 hot (foes near, hits) */
} SomDirScene;
enum {SOM_DIR_EV_NONE,SOM_DIR_EV_WHACK,SOM_DIR_EV_FINISHER,SOM_DIR_EV_HURT,SOM_DIR_EV_ARRIVE,
      SOM_DIR_EV_FIGHT,SOM_DIR_EV_KILL,SOM_DIR_EV_LINE,SOM_DIR_EV_COMBO,SOM_DIR_EV_KO};

typedef struct {
    uint8_t plan[SOM_DIR_HORIZON];     /* tiles; plan[0] plays now */
    uint8_t valid;
    uint8_t now;                       /* tile playing */
    unsigned long beat_start;          /* frame the beat began */
    uint8_t pin[SOM_DIR_HORIZON];      /* shot+1 pinned per beat, 0 none */
    uint16_t recent[6];                /* shots of the last beats (variety) */
    uint32_t rng;
    unsigned solves,fallbacks;
} SomDirector;

static inline void som_dir_reset(SomDirector *d,uint32_t seed){
    memset(d,0,sizeof(*d));d->rng=seed?seed:0x2545F491u;
    d->now=SOM_DIR_TILE(SOM_DIR_WIDE,0);
}
static inline uint32_t som_dir_rand(SomDirector *d){
    uint32_t x=d->rng?d->rng:0x2545F491u;x^=x<<13;x^=x>>17;x^=x<<5;d->rng=x;return x;
}
static inline unsigned som_dir_pop(SomDirSet s){unsigned n=0;while(s){s&=s-1u;n++;}return n;}
/* Tiles that may stand right after tile t (the grammar, as a set). */
static inline SomDirSet som_dir_next_raw(unsigned t){
    const unsigned s=SOM_DIR_SHOT_OF(t),k=SOM_DIR_RUN_OF(t);
    SomDirSet out=0;
    const unsigned run=k+1u;                       /* beats it has run after this one */
    const int may_hold=!som_dir_max[s]||run<som_dir_max[s];
    if(may_hold)out|=(SomDirSet)1u<<SOM_DIR_TILE(s,k+1u<SOM_DIR_PHASES?k+1u:SOM_DIR_PHASES-1u);
    if(run>=som_dir_min[s])
        for(unsigned n=0;n<SOM_DIR_SHOTS;n++)
            if(n!=s&&(som_dir_follow[s]>>n&1u))out|=(SomDirSet)1u<<SOM_DIR_TILE(n,0);
    return out;
}
/* The grammar as two tables, built once: after[t] and before[t]. */
static SomDirSet som_dir_tab_after[SOM_DIR_TILES],som_dir_tab_before[SOM_DIR_TILES];
static int som_dir_tab_ready;
static inline void som_dir_tables(void){
    if(som_dir_tab_ready)return;
    memset(som_dir_tab_before,0,sizeof(som_dir_tab_before));
    for(unsigned t=0;t<SOM_DIR_TILES;t++){
        som_dir_tab_after[t]=som_dir_next_raw(t);
        for(unsigned u=0;u<SOM_DIR_TILES;u++)
            if(som_dir_tab_after[t]>>u&1u)som_dir_tab_before[u]|=(SomDirSet)1u<<t;
    }
    som_dir_tab_ready=1;
}
static inline SomDirSet som_dir_next(unsigned t){som_dir_tables();return som_dir_tab_after[t];}
static inline unsigned som_dir_low(SomDirSet s){unsigned n=0;while(!(s&1u)){s>>=1;n++;}return n;}
static inline SomDirSet som_dir_after(SomDirSet set){
    som_dir_tables();
    SomDirSet out=0;
    while(set){const unsigned t=som_dir_low(set);set&=set-1u;out|=som_dir_tab_after[t];}
    return out;
}
static inline SomDirSet som_dir_before(SomDirSet set){
    som_dir_tables();
    SomDirSet out=0;
    while(set){const unsigned t=som_dir_low(set);set&=set-1u;out|=som_dir_tab_before[t];}
    return out;
}
static inline SomDirSet som_dir_shots(uint16_t shots){
    SomDirSet out=0;
    for(unsigned s=0;s<SOM_DIR_SHOTS;s++)
        if(shots>>s&1u)for(unsigned k=0;k<SOM_DIR_PHASES;k++)out|=(SomDirSet)1u<<SOM_DIR_TILE(s,k);
    return out;
}
/* Shots the scene allows for free choice. */
static inline uint16_t som_dir_scene_shots(const SomDirScene *sc){
    uint16_t m;
    switch(sc->scene){
    case SOM_DIR_TALK: m=SOM_DIR_BIT(SOM_DIR_TWO)|SOM_DIR_BIT(SOM_DIR_SPEAKER)|SOM_DIR_BIT(SOM_DIR_WIDE);break;
    case SOM_DIR_FIGHT:m=SOM_DIR_BIT(SOM_DIR_MEDIUM)|SOM_DIR_BIT(SOM_DIR_LEAD)|SOM_DIR_BIT(SOM_DIR_FOE)|
                         SOM_DIR_BIT(SOM_DIR_CLOSE)|SOM_DIR_BIT(SOM_DIR_PARTY)|SOM_DIR_BIT(SOM_DIR_WIDE)|
                         SOM_DIR_BIT(SOM_DIR_PULL);break;
    case SOM_DIR_HOLD: m=SOM_DIR_BIT(SOM_DIR_WIDE);break;
    default:           m=SOM_DIR_BIT(SOM_DIR_WIDE)|SOM_DIR_BIT(SOM_DIR_PULL)|SOM_DIR_BIT(SOM_DIR_PARTY)|
                         (sc->still?SOM_DIR_BIT(SOM_DIR_IDLE):0u);break;
    }
    return (uint16_t)(m&(sc->fits|SOM_DIR_BIT(SOM_DIR_WIDE)|SOM_DIR_BIT(SOM_DIR_PULL)));
}
/* How much the scene wants a shot (x16). */
static inline unsigned som_dir_weight(const SomDirScene *sc,unsigned s){
    static const uint8_t peace[SOM_DIR_SHOTS]={40,0,0,0,0,0,10,0,0,0,6,0};
    static const uint8_t still[SOM_DIR_SHOTS]={4,0,0,0,0,0,4,0,0,0,2,60};    /* he rests: close in */
    static const uint8_t talk[SOM_DIR_SHOTS]={4,0,0,0,0,0,0,30,22,0,2,0};
    static const uint8_t fight[4][SOM_DIR_SHOTS]={
        {24,0,20,10,4,4,16,0,0,0,6,0},     /* calm: wide and party */
        {10,0,26,18,6,8,10,0,0,0,6,0},
        {4,0,24,24,8,14,6,0,0,0,4,0},
        {2,0,18,26,10,20,2,0,0,0,4,0},     /* hot: close on the exchange */
    };
    if(sc->scene==SOM_DIR_TALK)return talk[s];
    if(sc->scene==SOM_DIR_FIGHT)return fight[sc->intensity<0?0:sc->intensity>3?3:sc->intensity][s];
    if(sc->scene==SOM_DIR_HOLD)return s==SOM_DIR_WIDE?16u:0u;
    return sc->still?still[s]:peace[s];
}
/* Weighted pick of a tile of `set` for beat j. */
static inline unsigned som_dir_pick(SomDirector *d,const SomDirScene *sc,SomDirSet set,unsigned j,
                                    const uint8_t old[SOM_DIR_HORIZON],int old_valid){
    unsigned w[SOM_DIR_TILES],total=0;
    for(unsigned t=0;t<SOM_DIR_TILES;t++){
        w[t]=0;
        if(!(set>>t&1u))continue;
        const unsigned s=SOM_DIR_SHOT_OF(t),k=SOM_DIR_RUN_OF(t);
        unsigned v=som_dir_weight(sc,s)+1u;
        /* long holds tire, but not the steady shots (the game's view, idling, talking) */
        if(k>0u&&k+1u>som_dir_min[s]&&!(SOM_DIR_STEADY>>s&1u))v=v*2u/(k+1u);
        if(k==0u)for(unsigned r=0;r<6u;r++)if(d->recent[r]==s+1u)v=v*3u/4u;  /* variety */
        if(old_valid&&j+1u<SOM_DIR_HORIZON&&old[j+1u]==t)v*=3u;  /* keep the plan */
        w[t]=v*v;total+=w[t];
    }
    if(!total){for(unsigned t=0;t<SOM_DIR_TILES;t++)if(set>>t&1u)return t;return SOM_DIR_TILE(SOM_DIR_WIDE,0);}
    unsigned r=som_dir_rand(d)%total;
    for(unsigned t=0;t<SOM_DIR_TILES;t++){if(r<w[t])return t;r-=w[t];}
    return SOM_DIR_TILE(SOM_DIR_WIDE,0);
}
/* Arc consistency along the line; 0 on a contradiction. */
static inline int som_dir_propagate(SomDirSet dom[SOM_DIR_HORIZON]){
    for(int changed=1;changed;){
        changed=0;
        for(unsigned j=0;j+1u<SOM_DIR_HORIZON;j++){
            const SomDirSet f=dom[j+1u]&som_dir_after(dom[j]);
            if(f!=dom[j+1u]){dom[j+1u]=f;changed=1;}
            const SomDirSet b=dom[j]&som_dir_before(dom[j+1u]);
            if(b!=dom[j]){dom[j]=b;changed=1;}
        }
        for(unsigned j=0;j<SOM_DIR_HORIZON;j++)if(!dom[j])return 0;
    }
    return 1;
}
/* Solve the plan; beat 0 follows tile `prev`. strict 0 drops the scene. */
static inline int som_dir_solve_with(SomDirector *d,const SomDirScene *sc,unsigned prev,int strict){
    SomDirSet dom[SOM_DIR_HORIZON];
    const SomDirSet free_shots=som_dir_shots((uint16_t)(strict?som_dir_scene_shots(sc):
        (0xFFFFu&~SOM_DIR_PINNED_ONLY)))|((SomDirSet)1u<<SOM_DIR_TILE(SOM_DIR_WIDE,0));
    for(unsigned j=0;j<SOM_DIR_HORIZON;j++){
        dom[j]=d->pin[j]?som_dir_shots((uint16_t)SOM_DIR_BIT(d->pin[j]-1u)):free_shots;
        /* a pinned shot may keep running into the beats after its pin */
        if(!d->pin[j]&&j>0u&&d->pin[j-1u])dom[j]|=som_dir_shots((uint16_t)SOM_DIR_BIT(d->pin[j-1u]-1u));
    }
    dom[0]&=som_dir_next(prev);
    if(!som_dir_propagate(dom))return 0;
    uint8_t old[SOM_DIR_HORIZON];memcpy(old,d->plan,sizeof(old));
    const int old_valid=d->valid;
    for(;;){
        int best=-1;unsigned best_n=0;
        for(unsigned j=0;j<SOM_DIR_HORIZON;j++){
            const unsigned n=som_dir_pop(dom[j]);
            if(n>1u&&(best<0||n<best_n)){best=(int)j;best_n=n;}
        }
        if(best<0)break;
        const unsigned t=som_dir_pick(d,sc,dom[best],(unsigned)best,old,old_valid);
        SomDirSet keep[SOM_DIR_HORIZON];memcpy(keep,dom,sizeof(keep));
        dom[best]=(SomDirSet)1u<<t;
        if(!som_dir_propagate(dom)){                   /* that choice fails: forbid it */
            memcpy(dom,keep,sizeof(keep));
            dom[best]&=~((SomDirSet)1u<<t);
            if(!som_dir_propagate(dom))return 0;
        }
    }
    for(unsigned j=0;j<SOM_DIR_HORIZON;j++){
        unsigned t=0;while(t<SOM_DIR_TILES&&!(dom[j]>>t&1u))t++;
        d->plan[j]=(uint8_t)t;
    }
    d->valid=1;d->solves++;
    return 1;
}
static inline void som_dir_solve(SomDirector *d,const SomDirScene *sc,unsigned prev){
    if(som_dir_solve_with(d,sc,prev,1))return;
    d->fallbacks++;
    if(som_dir_solve_with(d,sc,prev,0))return;
    /* last resort: wide from now on (wide may follow anything that has run) */
    for(unsigned j=0;j<SOM_DIR_HORIZON;j++)d->plan[j]=(uint8_t)SOM_DIR_TILE(SOM_DIR_WIDE,j<4u?j:4u);
    d->valid=1;
}
/* Pin shot s on beats from..from+n-1 (0 = now). */
static inline void som_dir_pin(SomDirector *d,unsigned s,unsigned from,unsigned n){
    for(unsigned j=from;j<from+n&&j<SOM_DIR_HORIZON;j++)d->pin[j]=(uint8_t)(s+1u);
}
/* An event: pins, then the plan is solved again from now on. */
static inline void som_dir_event(SomDirector *d,int ev,const SomDirScene *sc,unsigned long frame){
    memset(d->pin,0,sizeof(d->pin));
    switch(ev){
    case SOM_DIR_EV_WHACK:    som_dir_pin(d,SOM_DIR_PUNCH,0,2);som_dir_pin(d,SOM_DIR_PULL,2,1);break;
    case SOM_DIR_EV_FINISHER: som_dir_pin(d,SOM_DIR_PUNCH,0,2);break;
    case SOM_DIR_EV_HURT:     if(sc->fits&SOM_DIR_BIT(SOM_DIR_CLOSE))som_dir_pin(d,SOM_DIR_CLOSE,0,2);break;
    case SOM_DIR_EV_ARRIVE:   som_dir_pin(d,SOM_DIR_ESTABLISH,0,3);break;
    case SOM_DIR_EV_FIGHT:    if(sc->fits&SOM_DIR_BIT(SOM_DIR_LEAD))som_dir_pin(d,SOM_DIR_LEAD,0,2);break;
    case SOM_DIR_EV_KILL:     if(sc->fits&SOM_DIR_BIT(SOM_DIR_PARTY))som_dir_pin(d,SOM_DIR_PARTY,0,2);break;
    case SOM_DIR_EV_LINE:     break;          /* a new line: the plan is simply solved again */
    /* fighting-game grammar: a combo level pushes in on the exchange, the
     * last foe down is a slow pull-out (the K.O.) */
    case SOM_DIR_EV_COMBO:    som_dir_pin(d,sc->fits&SOM_DIR_BIT(SOM_DIR_FOE)?SOM_DIR_FOE:SOM_DIR_LEAD,0,2);break;
    case SOM_DIR_EV_KO:       som_dir_pin(d,SOM_DIR_PULL,0,2);break;
    default:break;
    }
    /* a punch or an arrival starts a new beat at once; others wait for the beat */
    const unsigned prev=ev==SOM_DIR_EV_WHACK||ev==SOM_DIR_EV_FINISHER||ev==SOM_DIR_EV_ARRIVE||
        ev==SOM_DIR_EV_COMBO||ev==SOM_DIR_EV_KO?
        SOM_DIR_TILE(SOM_DIR_WIDE,SOM_DIR_PHASES-1u):d->now;
    d->valid=0;
    som_dir_solve(d,sc,prev);
    d->now=d->plan[0];d->beat_start=frame;
}
/* Each frame: the tile to play. A new beat solves the plan again. */
static inline unsigned som_dir_frame(SomDirector *d,const SomDirScene *sc,unsigned long frame){
    if(!d->valid){som_dir_solve(d,sc,d->now);d->now=d->plan[0];d->beat_start=frame;}
    else if(frame-d->beat_start>=SOM_DIR_BEAT){
        memmove(d->recent+1,d->recent,sizeof(d->recent)-sizeof(d->recent[0]));
        d->recent[0]=(uint16_t)(SOM_DIR_SHOT_OF(d->now)+1u);
        memmove(d->plan,d->plan+1,SOM_DIR_HORIZON-1u);          /* the old plan, one beat on */
        memmove(d->pin,d->pin+1,SOM_DIR_HORIZON-1u);d->pin[SOM_DIR_HORIZON-1u]=0;
        som_dir_solve(d,sc,d->now);
        d->now=d->plan[0];d->beat_start=frame;
    }
    /* the scene took the shot away (it no longer fits, a menu opened): solve now */
    const unsigned s=SOM_DIR_SHOT_OF(d->now);
    if(!(SOM_DIR_PINNED_ONLY>>s&1u)&&s!=SOM_DIR_WIDE&&s!=SOM_DIR_PULL&&
       !(som_dir_scene_shots(sc)>>s&1u)){
        memset(d->pin,0,sizeof(d->pin));
        som_dir_solve(d,sc,SOM_DIR_TILE(s,SOM_DIR_PHASES-1u));
        d->now=d->plan[0];d->beat_start=frame;
    }
    return SOM_DIR_SHOT_OF(d->now);
}
#endif
