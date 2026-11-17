/* Raids: a general and its pack attack a town; champions defend it
 * (EXPERIMENTAL, part of the generals opt-in).
 *
 * Monsters exist only on their own maps, so raiders are carried there the
 * way the familiar is (src/som_familiar.h): on the general's home map the
 * object, its sprite tiles in VRAM and its palette are captured; in town a
 * free monster slot gets the object and the tiles/palette are written back
 * every frame. In towns the three monster slots (objects 3-5) also hold
 * villagers, so a raider takes only a free slot and is left out when its
 * palette or any of its tiles is in use by another object on the map
 * (villager sprites would be garbled otherwise).
 *
 * Champions: villagers with work frames (src/som_npc_work.h). During a raid
 * the proxy picks a static villager within SOM_RAID_CHAMPION_REACH of a
 * raider, starts one npc_work swing toward it and, halfway through the
 * swing, queues SOM_RAID_CHAMPION_DAMAGE as pending damage +0x1F1 when the
 * raider is still in reach: the game applies it, so raiders flinch, are
 * knocked back and die normally. Pure functions on WRAM; no I/O. */
#ifndef FLXTR_SOM_RAID_H
#define FLXTR_SOM_RAID_H

#include <stdlib.h>
#include <string.h>
#include "som_familiar.h"
#include "som_bunshin.h"
#include "som_npc_walk.h"

#define SOM_RAID_TOWN_MAP 131u              /* Potos, beside the Rabite field 130 */
#define SOM_RAID_MIN_RANK 3u
#define SOM_RAID_GAP_FRAMES (60ul*60ul*10ul)  /* at most one raid per 10 minutes */
#define SOM_RAID_CAPTURE_DELAY 120ul         /* frames after election: sprites are up */
#define SOM_RAID_CAPTURE_EVERY 600ul
#define SOM_RAID_SPAWN_DISTANCE 48
#define SOM_RAID_CHAMPION_REACH 40          /* px, Manhattan */
#define SOM_RAID_CHAMPION_DAMAGE 8u
#define SOM_RAID_RAIDERS 2u                 /* the general, then one of its pack */

typedef struct {
    SomFamiliarCarry carry[SOM_RAID_RAIDERS];
    int captured[SOM_RAID_RAIDERS];
    unsigned long captured_at;
    int active;
    unsigned map;
    int slot[SOM_RAID_RAIDERS];             /* monster slot of each raider, -1 = none */
    unsigned long ended_at;
    int have_ended;
    int strike_target;                      /* raider slot a champion swings at, -1 */
    unsigned strike_npc;
    unsigned long strike_at;
} SomRaid;

static inline void som_raid_reset(SomRaid *r){
    memset(r,0,sizeof(*r));
    for(unsigned k=0;k<SOM_RAID_RAIDERS;k++)r->slot[k]=-1;
    r->strike_target=-1;
}

/* 1 when a carried raider can be drawn on this map: its palette and tiles
 * are not used by any active object in slots 0-11 (heroes, NPCs, monsters). */
static inline int som_raid_fits(const SomFamiliarCarry *c,const uint8_t *ram){
    if(!c->valid||!c->has_vram)return 0;
    const unsigned palette=(c->object[0x8B]>>1)&7u;
    for(unsigned i=0;i<12u;i++){
        const uint8_t *o=ram+0xE000u+i*0x200u;
        if(o[0]!=1u)continue;
        if(i>=3u&&((o[0x8B]>>1)&7u)==palette)return 0;
        for(unsigned f=0;f<sizeof(som_familiar_tile_fields);f++){
            const unsigned v=som_fam_u16(o+som_familiar_tile_fields[f]);
            if(!v)continue;
            const unsigned base=(v&0xFFu)|((v>>8)&1u)<<8;
            const unsigned corner[4]={base,base+1u,base+16u,base+17u};
            for(unsigned k=0;k<4u;k++)
                for(unsigned t=0;t<c->tile_count;t++)
                    if(c->tile_id[t]==corner[k])return 0;
        }
    }
    return 1;
}

/* An open spot SOM_RAID_SPAWN_DISTANCE px from the hero (tries 4 sides). */
static inline int som_raid_spot(const uint8_t *ram,unsigned which,int *x,int *y){
    static const int dirs[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    const int hx=(int)som_fam_u16(ram+0xE002),hy=(int)som_fam_u16(ram+0xE004);
    const unsigned place=ram[0xE00B];
    for(unsigned k=0;k<4u;k++){
        const int *d=dirs[(which+k)&3u];
        const int cx=hx+d[0]*SOM_RAID_SPAWN_DISTANCE,cy=hy+d[1]*SOM_RAID_SPAWN_DISTANCE;
        if(som_bun_spot_open(ram,cx,cy,place)){*x=cx;*y=cy;return 1;}
    }
    return 0;
}

/* Starts a raid on the current map: returns the number of raiders placed
 * (0 = none fit; the raid is then not active). */
static inline unsigned som_raid_start(SomRaid *r,uint8_t *ram,uint8_t *vram){
    if(!vram)return 0;
    unsigned placed=0;
    for(unsigned k=0;k<SOM_RAID_RAIDERS;k++){
        r->slot[k]=-1;
        const SomFamiliarCarry *c=&r->carry[k];
        if(!r->captured[k]||!som_raid_fits(c,ram))continue;
        int x,y;
        if(!som_raid_spot(ram,k,&x,&y))continue;
        for(int slot=0;slot<3;slot++){
            uint8_t *to=ram+SOM_FAMILIAR_MONSTER_BASE+(unsigned)slot*SOM_FAMILIAR_OBJECT;
            if(to[0]==1u)continue;
            memcpy(to,c->object,SOM_FAMILIAR_OBJECT);
            som_familiar_reapply(c,ram,vram);
            som_fam_put16(to+2,(unsigned)x);som_fam_put16(to+4,(unsigned)y);
            to[0x0B]=ram[0xE00B];                  /* the hero's layer and floor */
            to[0x1F1]=0;to[0x1AC]=0xFF;
            som_fam_put16(to+0x182,som_fam_u16(to+0x184));
            r->slot[k]=slot;placed++;
            break;
        }
    }
    if(placed){r->active=1;r->map=som_fam_u16(ram+0xDC)&0x1FFu;r->strike_target=-1;}
    return placed;
}

static inline int som_raid_raider_alive(const SomRaid *r,const uint8_t *ram,unsigned k){
    if(r->slot[k]<0)return 0;
    const uint8_t *o=ram+SOM_FAMILIAR_MONSTER_BASE+(unsigned)r->slot[k]*SOM_FAMILIAR_OBJECT;
    return som_fam_alive(o)&&o[0x180]==r->carry[k].object[0x180];
}

enum { SOM_RAID_GOING = 0, SOM_RAID_REPELLED, SOM_RAID_LEFT };

/* Once per frame while active: keeps the raiders drawn and reports the end. */
static inline int som_raid_tick(SomRaid *r,uint8_t *ram,uint8_t *vram,unsigned long frame){
    if(!r->active)return SOM_RAID_GOING;
    if((som_fam_u16(ram+0xDC)&0x1FFu)!=r->map){
        r->active=0;r->ended_at=frame;r->have_ended=1;return SOM_RAID_LEFT;
    }
    unsigned alive=0;
    for(unsigned k=0;k<SOM_RAID_RAIDERS;k++){
        if(som_raid_raider_alive(r,ram,k)){alive++;som_familiar_reapply(&r->carry[k],ram,vram);}
        else r->slot[k]=-1;
    }
    if(!alive){r->active=0;r->ended_at=frame;r->have_ended=1;return SOM_RAID_REPELLED;}
    return SOM_RAID_GOING;
}

/* A champion for the raid: the NPC slot (3-11) of a static villager whose
 * kind `has_frames` accepts, nearest to a live raider within reach; its
 * facing toward the raider (0 up, 1 down, 2 right, 3 left) and the
 * raider's monster slot. Returns 0 when none. */
static inline int som_raid_champion(const SomRaid *r,const uint8_t *ram,
                                    int (*has_frames)(unsigned kind,void *ctx),void *ctx,
                                    unsigned *npc,unsigned *dir,int *target){
    int best=-1;
    for(unsigned s=SOM_NPC_FIRST_SLOT;s<=SOM_NPC_LAST_SLOT;s++){
        const uint8_t *v=ram+SOM_NPC_OBJECT(s);
        if(v[0]!=1u||v[0x3D]<0x80u||!(v[0x44]&0x80u))continue;
        if(has_frames&&!has_frames(v[0x3D],ctx))continue;
        const int vx=(int)som_fam_u16(v+2),vy=(int)som_fam_u16(v+4);
        for(unsigned k=0;k<SOM_RAID_RAIDERS;k++){
            if(!som_raid_raider_alive(r,ram,k))continue;
            const uint8_t *m=ram+SOM_FAMILIAR_MONSTER_BASE+(unsigned)r->slot[k]*SOM_FAMILIAR_OBJECT;
            const int dx=(int)som_fam_u16(m+2)-vx,dy=(int)som_fam_u16(m+4)-vy;
            const int d=abs(dx)+abs(dy);
            if(d>SOM_RAID_CHAMPION_REACH||(best>=0&&d>=best))continue;
            best=d;*npc=s;*target=r->slot[k];
            *dir=abs(dx)>=abs(dy)?(dx>=0?2u:3u):(dy>=0?1u:0u);
        }
    }
    return best>=0;
}

/* The champion's blow lands: pending damage if the raider is still in
 * reach of the villager. Returns 1 when it hit. */
static inline int som_raid_strike(SomRaid *r,uint8_t *ram){
    const int target=r->strike_target;
    r->strike_target=-1;
    if(target<0)return 0;
    uint8_t *m=ram+SOM_FAMILIAR_MONSTER_BASE+(unsigned)target*SOM_FAMILIAR_OBJECT;
    if(!som_fam_alive(m))return 0;
    const uint8_t *v=ram+SOM_NPC_OBJECT(r->strike_npc);
    const int d=abs((int)som_fam_u16(m+2)-(int)som_fam_u16(v+2))+
                abs((int)som_fam_u16(m+4)-(int)som_fam_u16(v+4));
    if(d>SOM_RAID_CHAMPION_REACH+8)return 0;
    const unsigned pending=m[0x1F1]+SOM_RAID_CHAMPION_DAMAGE;
    m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
    return 1;
}

#endif
