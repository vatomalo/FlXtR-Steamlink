/* Mentalist telekinesis: pull native treasure chest actors toward the hero.
 * No loot, story flags, NPC state or chest contents are written by the proxy.
 * Only actor positions (+0x02/+0x04) change, one collision-checked step at
 * a time. Chests are distinguished by status2 bit 7 (actor +0x1B1).
 * See the SoM WRAM character layout. Do not generalise this to event props:
 * many share the actor array but cannot safely be relocated.
 */
#ifndef FLXTR_SOM_TELEKINESIS_H
#define FLXTR_SOM_TELEKINESIS_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "som_bunshin.h"

#define SOM_TK_FIRST_SLOT 3u
#define SOM_TK_LAST_SLOT 11u
#define SOM_TK_RANGE 160
#define SOM_TK_STOP 28
#define SOM_TK_SPEED 3
#define SOM_TK_MAX_FRAMES 90ul
#define SOM_TK_COOLDOWN 240ul

typedef struct {
    int slot;                     /* object slot 3..11; -1 while idle */
    unsigned map;
    uint8_t kind;
    int last_x,last_y;            /* reject an object changed by the game */
    unsigned long start;
} SomTelekinesis;

static inline void som_tk_reset(SomTelekinesis *t) {
    memset(t,0,sizeof(*t));
    t->slot=-1;
}
static inline int som_tk_field(const uint8_t *ram) {
    return ram&&ram[0xE000u]==1u&&ram[0xE4u]==1u&&
           ram[0xE6u]==0x0Fu&&ram[0xD0u]==0u&&
           ram[0x7Du]!=1u&&!(ram[0xE8u]&0x04u);
}
static inline int som_tk_is_chest(const uint8_t *o) {
    return o[0]==1u&&(o[0x1B1u]&0x80u)!=0u&&
           o[0x181u]!=0xFFu&&(o[0x1FBu]&0x04u)==0u;
}
static inline int som_tk_nearest(const uint8_t *ram,int hx,int hy,unsigned place) {
    int found=-1,nearest=SOM_TK_RANGE+1;
    for(unsigned slot=SOM_TK_FIRST_SLOT;slot<=SOM_TK_LAST_SLOT;slot++){
        const uint8_t *o=ram+0xE000u+0x200u*slot;
        if(!som_tk_is_chest(o)||(o[0x0Bu]&0x83u)!=(place&0x83u))continue;
        if(som_bun_u16(ram+0x3Au)==0xE000u+0x200u*slot)continue;
        const int x=(int)som_bun_u16(o+2u),y=(int)som_bun_u16(o+4u);
        const int dist=abs(hx-x)+abs(hy-y);
        if(dist<=SOM_TK_RANGE&&dist<nearest){nearest=dist;found=(int)slot;}
    }
    return found;
}
/* Return 1 only when a chest is acquired. Does not move anything yet. */
static inline int som_tk_begin(SomTelekinesis *t,uint8_t *ram,unsigned long frame){
    if(t->slot>=0||!som_tk_field(ram))return 0;
    const uint8_t *hero=ram+0xE000u;
    const int hx=(int)som_bun_u16(hero+2u),hy=(int)som_bun_u16(hero+4u);
    const int slot=som_tk_nearest(ram,hx,hy,hero[0x0Bu]);
    if(slot<0)return 0;
    const uint8_t *o=ram+0xE000u+0x200u*(unsigned)slot;
    t->slot=slot;t->map=som_bun_u16(ram+0xDCu);t->kind=o[0x180u];
    t->last_x=(int)som_bun_u16(o+2u);t->last_y=(int)som_bun_u16(o+4u);
    t->start=frame;
    return 1;
}
/* 1: moved, 2: in pickup range, -1: cancelled/blocked, 0: inactive.
 * Movement uses the same conservative terrain check as the proxy's dodge.
 * Always leave the game to run the native chest-opening/trap/loot logic. */
static inline int som_tk_step(SomTelekinesis *t,uint8_t *ram,unsigned long frame){
    if(t->slot<0)return 0;
    if(!som_tk_field(ram)||som_bun_u16(ram+0xDCu)!=t->map||
       frame-t->start>SOM_TK_MAX_FRAMES){som_tk_reset(t);return -1;}
    uint8_t *o=ram+0xE000u+0x200u*(unsigned)t->slot;
    const uint8_t *hero=ram+0xE000u;
    int x=(int)som_bun_u16(o+2u),y=(int)som_bun_u16(o+4u);
    if(!som_tk_is_chest(o)||o[0x180u]!=t->kind||
       x!=t->last_x||y!=t->last_y||
       (o[0x0Bu]&0x83u)!=(hero[0x0Bu]&0x83u)){
        som_tk_reset(t);return -1;
    }
    const int hx=(int)som_bun_u16(hero+2u),hy=(int)som_bun_u16(hero+4u);
    const int dx=hx-x,dy=hy-y;
    if(abs(dx)+abs(dy)<=SOM_TK_STOP){som_tk_reset(t);return 2;}
    const unsigned place=o[0x0Bu];
    /* Try the longer axis first. A tile collision never becomes a teleport. */
    int moved=0;
    for(unsigned axis=0;axis<2u;axis++){
        const int use_x=(abs(dx)>=abs(dy))==(axis==0u);
        const int delta=use_x?dx:dy;
        const int step=delta>0?(delta<SOM_TK_SPEED?delta:SOM_TK_SPEED):
                       delta<0?(delta>-SOM_TK_SPEED?delta:-SOM_TK_SPEED):0;
        if(!step)continue;
        const int nx=x+(use_x?step:0),ny=y+(use_x?0:step);
        if(!som_bun_spot_open(ram,nx,ny,place))continue;
        x=nx;y=ny;moved=1;break;
    }
    if(!moved){som_tk_reset(t);return -1;}
    o[2]=(uint8_t)x;o[3]=(uint8_t)(x>>8);
    o[4]=(uint8_t)y;o[5]=(uint8_t)(y>>8);
    t->last_x=x;t->last_y=y;
    return 1;
}
#endif
