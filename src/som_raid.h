/* Raids: a general and its pack attack a town; champions defend it
 * (EXPERIMENTAL, part of the generals opt-in).
 *
 * Monsters exist only on their own maps, so raiders are carried there: on
 * the general's home map the object, its sprite tile buffer
 * (7E:5000 + slot * 0x1000) and its palette (7E:DFA0 + slot * 0x20) are
 * captured (som_familiar_save). In town the three monster slots (objects
 * 3-5) also hold villagers, each with its own VRAM tile area and palette
 * (live 2026-11-17, Potos: object 3 = villager C4, tile base 0x40, OBJ
 * palette 6; objects 4/5 empty with bases 0x48/0x60). A raider is
 * therefore relocated like clone_enemy: it takes a free slot, keeps that
 * slot's own fields (tile base, VRAM addresses), gets its art in that
 * slot's tile buffer, and an OBJ palette 5-7 that no active object uses
 * (attribute bits of +0x8B and the tile fields, palette address +0xF8 =
 * 0x20 * p + 2 as the villager's 0xC2 for palette 6). Buffer and palette
 * are rewritten every frame. A raid starts only after the map has been in
 * field mode for SOM_RAID_SETTLE_FRAMES, since the town's own object setup
 * clears slots on arrival.
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
#define SOM_RAID_SETTLE_FRAMES 120ul
#define SOM_RAID_HERO_REACH 32              /* px along the boy's facing */
#define SOM_RAID_HERO_SPAN 16               /* px across it */
#define SOM_RAID_TILE_BUFFER 0x5000u
#define SOM_RAID_TILE_BUFFER_SIZE 0x1000u

typedef struct {
    SomFamiliarCarry carry[SOM_RAID_RAIDERS];
    int captured[SOM_RAID_RAIDERS];
    unsigned long captured_at;
    int active;
    unsigned map;
    int slot[SOM_RAID_RAIDERS];             /* monster slot of each raider, -1 = none */
    unsigned palette[SOM_RAID_RAIDERS];     /* OBJ palette it was given */
    unsigned long ended_at;
    int have_ended;
    int strike_target;                      /* raider slot a champion swings at, -1 */
    uint8_t hero_attack;                    /* the boy's +0x1C last frame */
    unsigned strike_npc;
    unsigned long strike_at;
} SomRaid;

static inline void som_raid_reset(SomRaid *r){
    memset(r,0,sizeof(*r));
    for(unsigned k=0;k<SOM_RAID_RAIDERS;k++)r->slot[k]=-1;
    r->strike_target=-1;
}

/* An OBJ palette 5-7 that no active object (slots 0-11) uses and that is
 * not in `taken` (bit mask), or -1. */
static inline int som_raid_free_palette(const uint8_t *ram,unsigned taken){
    for(unsigned p=5;p<=7u;p++){
        if(taken&(1u<<p))continue;
        int used=0;
        for(unsigned i=0;i<12u&&!used;i++){
            const uint8_t *o=ram+0xE000u+i*0x200u;
            used=o[0]==1u&&((o[0x8B]>>1)&7u)==p;
        }
        if(!used)return (int)p;
    }
    return -1;
}
/* Gives the object in `o` OBJ palette p and the sprite priority bits
 * `priority` (attribute & 0x30, from its home object): +0x8B, the attribute
 * byte of every tile field, and the palette address +0xF8. */
static inline void som_raid_set_palette(uint8_t *o,unsigned p,unsigned priority){
    const unsigned keep=~(0x30u|0x0Eu);
    o[0x8B]=(uint8_t)((o[0x8B]&keep)|(priority&0x30u)|p<<1);
    for(unsigned f=0;f<sizeof(som_familiar_tile_fields);f++){
        uint8_t *attr=o+som_familiar_tile_fields[f]+1;
        if(o[som_familiar_tile_fields[f]]|*attr)
            *attr=(uint8_t)((*attr&keep)|(priority&0x30u)|p<<1);
    }
    o[0xF8]=(uint8_t)(0x20u*p+2u);
}
/* Writes a raider's art and colours for its slot and palette. */
static inline void som_raid_paint(const SomFamiliarCarry *c,uint8_t *ram,int slot,unsigned p){
    memcpy(ram+SOM_RAID_TILE_BUFFER+(unsigned)slot*SOM_RAID_TILE_BUFFER_SIZE,c->tiles,
           sizeof(c->tiles));
    memcpy(ram+0xDF00u+32u*p,c->palette,sizeof(c->palette));
    memcpy(ram+0x0700u+32u*p,c->palette,sizeof(c->palette));
}

/* An open spot about SOM_RAID_SPAWN_DISTANCE px from the hero (4 sides,
 * then 2/3 and 4/3 of that distance). */
static inline int som_raid_spot(const uint8_t *ram,unsigned which,int *x,int *y){
    static const int dirs[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    const int hx=(int)som_fam_u16(ram+0xE002),hy=(int)som_fam_u16(ram+0xE004);
    const unsigned place=ram[0xE00B];
    static const int reach[3]={SOM_RAID_SPAWN_DISTANCE,SOM_RAID_SPAWN_DISTANCE*2/3,
                               SOM_RAID_SPAWN_DISTANCE*4/3};
    for(unsigned n=0;n<3u;n++)for(unsigned k=0;k<4u;k++){
        const int *d=dirs[(which+k)&3u];
        const int cx=hx+d[0]*reach[n],cy=hy+d[1]*reach[n];
        if(som_bun_spot_open(ram,cx,cy,place)){*x=cx;*y=cy;return 1;}
    }
    return 0;
}

/* Starts a raid on the current map: returns the number of raiders placed
 * (0 = no free slot or palette; the raid is then not active). */
static inline unsigned som_raid_start(SomRaid *r,uint8_t *ram){
    unsigned placed=0,taken=0;
    for(unsigned k=0;k<SOM_RAID_RAIDERS;k++){
        r->slot[k]=-1;
        const SomFamiliarCarry *c=&r->carry[k];
        if(!r->captured[k]||!c->valid)continue;
        const int p=som_raid_free_palette(ram,taken);
        int x,y;
        if(p<0||!som_raid_spot(ram,k,&x,&y))continue;
        for(int slot=0;slot<3;slot++){
            uint8_t *to=ram+SOM_FAMILIAR_MONSTER_BASE+(unsigned)slot*SOM_FAMILIAR_OBJECT;
            if(to[0]==1u)continue;
            uint8_t keep[sizeof(som_familiar_slot_fields)][2];
            for(unsigned i=0;i<sizeof(som_familiar_slot_fields);i++){
                keep[i][0]=to[som_familiar_slot_fields[i]];
                keep[i][1]=to[som_familiar_slot_fields[i]+1];
            }
            memcpy(to,c->object,SOM_FAMILIAR_OBJECT);
            for(unsigned i=0;i<sizeof(som_familiar_slot_fields);i++){
                to[som_familiar_slot_fields[i]]=keep[i][0];
                to[som_familiar_slot_fields[i]+1]=keep[i][1];
            }
            som_raid_set_palette(to,(unsigned)p,c->object[0x8B]);
            som_raid_paint(c,ram,slot,(unsigned)p);
            som_fam_put16(to+2,(unsigned)x);som_fam_put16(to+4,(unsigned)y);
            to[0x0B]=ram[0xE00B];                  /* the hero's layer and floor */
            to[0x1F1]=0;to[0x1AC]=0xFF;
            som_fam_put16(to+0x182,som_fam_u16(to+0x184));
            r->slot[k]=slot;r->palette[k]=(unsigned)p;taken|=1u<<p;placed++;
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
static inline int som_raid_tick(SomRaid *r,uint8_t *ram,unsigned long frame){
    if(!r->active)return SOM_RAID_GOING;
    if((som_fam_u16(ram+0xDC)&0x1FFu)!=r->map){
        r->active=0;r->ended_at=frame;r->have_ended=1;return SOM_RAID_LEFT;
    }
    unsigned alive=0;
    for(unsigned k=0;k<SOM_RAID_RAIDERS;k++){
        if(som_raid_raider_alive(r,ram,k)){alive++;som_raid_paint(&r->carry[k],ram,r->slot[k],r->palette[k]);}
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

/* The boy's blows on raiders. On a town map the game's own weapon hits do
 * not damage monsters (live 2026-11-17, Potos: raiders kept full HP and
 * +0x1F1 = 0 under the sword), so when the boy starts a swing (+0x1C turns
 * 0x20, or 0x80 for a charged attack; som-decomp graphics.md 5) the raider
 * nearest in front of him (facing +0x10: 0 up, 1 down, 2 side, bit 7 left)
 * within SOM_RAID_HERO_REACH along and SOM_RAID_HERO_SPAN across gets
 * pending damage: his strength +0x188, doubled when charged. Returns the
 * raider slot hit, or -1. Once per swing. */
static inline int som_raid_hero_hit(SomRaid *r,uint8_t *ram){
    const uint8_t attack=ram[0xE01C],before=r->hero_attack;
    r->hero_attack=attack;
    if(!r->active||!attack||before)return -1;
    const unsigned facing=ram[0xE010];
    const int hx=(int)som_fam_u16(ram+0xE002),hy=(int)som_fam_u16(ram+0xE004);
    int best=-1,best_d=0;
    for(unsigned k=0;k<SOM_RAID_RAIDERS;k++){
        if(!som_raid_raider_alive(r,ram,k))continue;
        const uint8_t *m=ram+SOM_FAMILIAR_MONSTER_BASE+(unsigned)r->slot[k]*SOM_FAMILIAR_OBJECT;
        const int dx=(int)som_fam_u16(m+2)-hx,dy=(int)som_fam_u16(m+4)-hy;
        int along,across;
        switch(facing&0x0Fu){
        case 0:along=-dy;across=dx;break;
        case 1:along=dy;across=dx;break;
        default:along=(facing&0x80u)?-dx:dx;across=dy;break;
        }
        if(along<-4||along>SOM_RAID_HERO_REACH||abs(across)>SOM_RAID_HERO_SPAN)continue;
        const int d=abs(along)+abs(across);
        if(best<0||d<best_d){best=r->slot[k];best_d=d;}
    }
    if(best<0)return -1;
    uint8_t *m=ram+SOM_FAMILIAR_MONSTER_BASE+(unsigned)best*SOM_FAMILIAR_OBJECT;
    unsigned damage=ram[0xE188]?ram[0xE188]:1u;
    if(attack&0x80u)damage*=2u;
    const unsigned pending=m[0x1F1]+damage;
    m[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
    return best;
}

#endif
