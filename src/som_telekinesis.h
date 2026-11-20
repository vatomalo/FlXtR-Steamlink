/* Psionics telekinesis: pull native treasure chests without touching event actors.
 * Only the known monster-slot treasure chest (identity 0xEA) is movable.
 * Never write to an NPC, a boss, an ordinary enemy or a scripted map object.
 * The game's own chest identity, inventory/drop state and interaction data stay intact.
 */
#ifndef FLXTR_SOM_TELEKINESIS_H
#define FLXTR_SOM_TELEKINESIS_H

#include <stdint.h>
#include "som_bunshin.h"

#define SOM_TK_CHEST 0xEAu
#define SOM_TK_RANGE 160
#define SOM_TK_STOP 28
#define SOM_TK_SPEED 3
#define SOM_TK_FRAMES 90ul
#define SOM_TK_COOLDOWN 180ul

typedef struct {
    int slot;                      /* monster-object slot 0..2; -1 = idle */
    unsigned map;
    unsigned long started, ready;
} SomTelekinesis;

static inline void som_tk_init(SomTelekinesis *t){
    if(!t)return;
    t->slot=-1;t->map=0;t->started=0;t->ready=0;
}
static inline int som_tk_field(const uint8_t *ram){
    return ram&&ram[0xE000]==1u&&ram[0x00E4]==1u&&ram[0x00E6]==0x0Fu&&
           ram[0x00D0]==0u;
}
static inline int som_tk_is_chest(const uint8_t *object){
    return object&&object[0]==1u&&object[0x180]==SOM_TK_CHEST;
}
static inline int som_tk_distance(int a,int b){
    return a>b?a-b:b-a;
}
/* Returns 1 only when a nearby, same-floor treasure chest was acquired. */
static inline int som_tk_start(SomTelekinesis *t,const uint8_t *ram,
                               unsigned long frame,int familiar_slot){
    if(!t||!som_tk_field(ram)||t->slot>=0||frame<t->ready)return 0;
    const uint8_t *hero=ram+0xE000u;
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    int best=-1,best_dist=SOM_TK_RANGE+1;
    for(int slot=0;slot<3;slot++){
        if(slot==familiar_slot)continue;
        const uint8_t *o=ram+0xE600u+0x200u*(unsigned)slot;
        if(!som_tk_is_chest(o)||((o[0x0B]^hero[0x0B])&0x83u))continue;
        const int d=som_tk_distance((int)som_bun_u16(o+2),hx)+
                    som_tk_distance((int)som_bun_u16(o+4),hy);
        if(d>SOM_TK_STOP+4&&d<=SOM_TK_RANGE&&d<best_dist){
            best=slot;best_dist=d;
        }
    }
    if(best<0)return 0;
    t->slot=best;t->map=som_bun_u16(ram+0xDC);t->started=frame;
    t->ready=frame+SOM_TK_COOLDOWN;
    return 1;
}
/* Returns 1 when the chest moved. Fail closed on map changes, slot reuse,
 * different floors, unwalkable ground, and interrupted field play. */
static inline int som_tk_tick(SomTelekinesis *t,uint8_t *ram,unsigned long frame){
    if(!t||t->slot<0)return 0;
    if(!som_tk_field(ram)||som_bun_u16(ram+0xDC)!=t->map||
       frame-t->started>=SOM_TK_FRAMES){
        t->slot=-1;return 0;
    }
    uint8_t *hero=ram+0xE000u;
    uint8_t *o=ram+0xE600u+0x200u*(unsigned)t->slot;
    if(!som_tk_is_chest(o)||((o[0x0B]^hero[0x0B])&0x83u)){
        t->slot=-1;return 0;
    }
    const int hx=(int)som_bun_u16(hero+2),hy=(int)som_bun_u16(hero+4);
    int x=(int)som_bun_u16(o+2),y=(int)som_bun_u16(o+4);
    int dx=hx-x,dy=hy-y;
    const int distance=som_tk_distance(hx,x)+som_tk_distance(hy,y);
    if(distance<=SOM_TK_STOP){t->slot=-1;return 0;}
    /* Keep the chest outside the hero's collision/interaction footprint. */
    int budget=distance-SOM_TK_STOP;
    if(budget>SOM_TK_SPEED)budget=SOM_TK_SPEED;
    int moved=0;
    for(int step=0;step<budget;step++){
        dx=hx-x;dy=hy-y;
        if(som_tk_distance(dx,0)>=som_tk_distance(dy,0)){
            const int next=x+(dx>0?1:-1);
            if(dx&&som_bun_spot_open(ram,next,y,o[0x0B])){x=next;moved=1;continue;}
        }
        if(dy){
            const int next=y+(dy>0?1:-1);
            if(som_bun_spot_open(ram,x,next,o[0x0B])){y=next;moved=1;continue;}
        }
        if(dx){
            const int next=x+(dx>0?1:-1);
            if(som_bun_spot_open(ram,next,y,o[0x0B])){x=next;moved=1;continue;}
        }
        break;
    }
    if(!moved){t->slot=-1;return 0;}
    o[2]=(uint8_t)x;o[3]=(uint8_t)(x>>8);
    o[4]=(uint8_t)y;o[5]=(uint8_t)(y>>8);
    o[6]=o[7]=0u;
    return 1;
}
#endif
