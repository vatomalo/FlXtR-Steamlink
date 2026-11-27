/* Monk's X: Intercept (player, 2026-11-27: "monks X is no longer flying
 * kick, its Shadow of Mordor & Batman + Sifu's intercept"; "use different
 * attacks for intercept").
 *
 * A monster starting its attack (+0x1C becomes 0x80; a Rabite's bite lands
 * about 14 frames later, som-decomp rabite.md 5.3) near the Monk opens a
 * window, shown as "!" over it (Batman's warning). X in the window steps
 * the Monk in front of it and strikes before the blow: its attack is
 * cancelled and the hit staggers it (the game's hurt state, 1.75 s: Sifu's
 * intercept). Late in the window (close to the blow) is a perfect
 * intercept. Every intercept uses the next strike of a rotation, and
 * quick intercepts chain (Mordor's counter streak). X with no window open
 * is a whiff and locks X briefly, as the games do.
 * Pure functions on WRAM; no I/O. */
#ifndef FLXTR_SOM_INTERCEPT_H
#define FLXTR_SOM_INTERCEPT_H

#include <stdint.h>
#include <stdlib.h>

#define SOM_ICPT_SEE 112           /* px (Manhattan): attacks the Monk can reach */
#define SOM_ICPT_WINDOW 16ul       /* frames after the attack starts */
#define SOM_ICPT_PERFECT 8ul       /* from this frame of the window on: perfect */
#define SOM_ICPT_CHAIN 180ul       /* frames: the next intercept keeps the streak */
#define SOM_ICPT_WHIFF 30ul        /* frames X is locked after a whiff */
#define SOM_ICPT_STAND 20          /* px from the monster the Monk lands */
#define SOM_ICPT_MONSTERS 0xE600u
#define SOM_ICPT_OBJECT 0x200u

typedef struct {
    uint8_t prev[3];               /* each monster's +0x1C last frame */
    uint8_t id[3];
    unsigned long opened[3];       /* frame its window opened, 0 = none */
    unsigned long last_at;         /* last intercept */
    unsigned long locked_until;    /* whiff lockout */
    unsigned chain;                /* intercepts in the streak */
    unsigned next;                 /* next strike of the rotation */
} SomIntercept;

/* The strikes: glove power attack level (its own animation), name. */
typedef struct { uint8_t level; const char *name; } SomIcptMove;
static const SomIcptMove som_icpt_moves[]={
    {2,"Palm Intercept"},{4,"Knee Intercept"},{1,"Elbow Intercept"},
    {5,"Sweep Intercept"},{3,"Shoulder Intercept"},{6,"Dragon Intercept"},
};
#define SOM_ICPT_MOVES (sizeof(som_icpt_moves)/sizeof(som_icpt_moves[0]))

static inline unsigned som_icpt_u16(const uint8_t *p){return (unsigned)(p[0]|p[1]<<8);}
static inline int som_icpt_alive(const uint8_t *m){
    return m[0]==1&&m[0x180]<0x80&&!(som_icpt_u16(m+0x190)&0x8000u)&&som_icpt_u16(m+0x182);
}

/* Once a frame: open a window for each monster that just began an attack
 * within reach of `hero`, close the ones that ended or ran out. */
static inline void som_icpt_scan(SomIntercept *s,const uint8_t *ram,const uint8_t *hero,
                                 unsigned long frame,int familiar_slot){
    const int hx=(int)som_icpt_u16(hero+2),hy=(int)som_icpt_u16(hero+4);
    for(unsigned k=0;k<3u;k++){
        const uint8_t *m=ram+SOM_ICPT_MONSTERS+SOM_ICPT_OBJECT*k;
        const uint8_t state=m[0x1C];
        const int ok=(int)k!=familiar_slot&&som_icpt_alive(m)&&!((m[0x0B]^hero[0x0B])&0x80u);
        if(!ok||state!=0x80u||m[0x180]!=s->id[k]||frame-s->opened[k]>SOM_ICPT_WINDOW)s->opened[k]=0;
        if(ok&&state==0x80u&&s->prev[k]!=0x80u&&
           abs((int)som_icpt_u16(m+2)-hx)+abs((int)som_icpt_u16(m+4)-hy)<=SOM_ICPT_SEE){
            s->opened[k]=frame?frame:1ul;s->id[k]=m[0x180];
        }
        s->prev[k]=state;
    }
}
/* The open window closest to its blow (the oldest), or -1. */
static inline int som_icpt_target(const SomIntercept *s){
    int best=-1;
    for(int k=0;k<3;k++)
        if(s->opened[k]&&(best<0||s->opened[k]<s->opened[best]))best=k;
    return best;
}
/* Where the Monk lands: SOM_ICPT_STAND px from the monster on his own side
 * (the larger axis), facing it. */
static inline void som_icpt_stand(const uint8_t *hero,const uint8_t *m,int *x,int *y,uint8_t *face){
    const int hx=(int)som_icpt_u16(hero+2),hy=(int)som_icpt_u16(hero+4);
    const int mx=(int)som_icpt_u16(m+2),my=(int)som_icpt_u16(m+4);
    const int dx=hx-mx,dy=hy-my;
    if(abs(dx)>=abs(dy)){
        *x=mx+(dx>=0?SOM_ICPT_STAND:-SOM_ICPT_STAND);*y=my;*face=dx>=0?0x82u:0x02u;
    }else{
        *x=mx;*y=my+(dy>=0?SOM_ICPT_STAND:-SOM_ICPT_STAND);*face=dy>=0?0x00u:0x01u;
    }
}
/* Damage of an intercept: base by class level, perfect x1.5, +2 per link
 * of the streak (up to +10). */
static inline unsigned som_icpt_damage(unsigned level,int perfect,unsigned chain){
    unsigned d=14u+2u*level;
    if(perfect)d=d*3u/2u;
    d+=2u*(chain<5u?chain:5u);
    return d;
}

#endif
