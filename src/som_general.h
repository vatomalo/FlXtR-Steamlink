/* Generals: persistent named elite monsters (EXPERIMENTAL, opt-in).
 *
 * Phase D of docs/SOM-ULTIMATE-AUDIT.md. A general is one monster species
 * on one home map with a rank and a memory kept in a small versioned text
 * file. Each visit to the home map, the first live monster of the species
 * becomes the general:
 *   - name label over it, palette tinted (gold, crimson from rank 3) in its
 *     own monster palette shadow 7E:DFA0 + slot * 0x20 (as the familiar);
 *   - HP and max HP (+0x182/+0x184) = its own max * (1 + rank);
 *   - bite cooldown +0x1ED (Rabite: set to 99 after each bite and counted
 *     down per frame, som-decomp rabite.md 5.3) held at most
 *     99 - 12 * rank, so it bites more often as it rises.
 * Memory: damage taken per hero weapon type (HP drops while the boy holds
 * that weapon, ram 7E:E1E4 & 7; an approximation, the girl, sprite or magic
 * may have dealt it), encounters, defeats, wins (the boy's HP reached 0
 * while it lived) and escapes (the hero left the map). Killing it or losing
 * to it raises its rank (max SOM_GEN_MAX_RANK): it comes back stronger.
 * Adaptation: the weapon that hurt it most when it last fell is its
 * nemesis; while the boy holds it and its bite is recharging, it backs
 * away from him (1 px per frame, only onto open ground), so it lunges from
 * range instead of trading blows.
 * Pure functions on WRAM and text; no I/O. */
#ifndef FLXTR_SOM_GENERAL_H
#define FLXTR_SOM_GENERAL_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "som_bunshin.h"
#include "som_class_palette.h"

#define SOM_GEN_MAX 8u
#define SOM_GEN_MAX_RANK 5u
#define SOM_GEN_MONSTER_BASE 0xE600u
#define SOM_GEN_OBJECT 0x200u
#define SOM_GEN_PALETTE 0xDFA0u
#define SOM_GEN_KEEP_AWAY 40          /* px (Manhattan) it keeps from a nemesis weapon */
#define SOM_GEN_RETINT_FRAMES 60ul
#define SOM_GEN_FILE_TAG "SOMGENERALS 2"
#define SOM_GEN_FILE_TAG_V1 "SOMGENERALS 1"
#define SOM_GEN_HEROES 3u
#define SOM_GEN_HABITS 8u                  /* = SOM_HABITS of som_enemy_tactics.h */

typedef struct {
    char name[24];
    unsigned species, home_map, rank;
    unsigned encounters, defeats, wins, escapes;
    unsigned damage[8];               /* lifetime damage taken per weapon type */
    int nemesis;                      /* weapon type, -1 = none yet */
    uint8_t habit[SOM_GEN_HEROES][SOM_GEN_HABITS];  /* habits seen per hero (v2) */
} SomGeneral;

typedef struct {
    int slot;                         /* monster slot 0-2, -1 = not on the field */
    unsigned map;
    unsigned prev_hp;
    unsigned damage[8];               /* this encounter */
    int done;                         /* fell or won this visit: no new election */
    int hero_was_down;
    int backing;                      /* currently backing away (for the log) */
    int away;                         /* raiding another map: its palette is carried */
    unsigned max_hp;                  /* its boosted max HP, kept if the game resets it */
    unsigned long elected_at;
    unsigned long last_tint;
    uint8_t tinted[32];
} SomGeneralLive;

enum { SOM_GEN_NONE = 0, SOM_GEN_ELECTED, SOM_GEN_DEFEATED, SOM_GEN_WON, SOM_GEN_ESCAPED,
       SOM_GEN_BACKING, SOM_GEN_STANDING, SOM_GEN_VANISHED };


static inline unsigned som_gen_u16(const uint8_t *p){return (unsigned)(p[0]|p[1]<<8);}
static inline void som_gen_put16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static inline uint8_t *som_gen_object(uint8_t *ram,int slot){
    return ram+SOM_GEN_MONSTER_BASE+(unsigned)slot*SOM_GEN_OBJECT;
}
static inline int som_gen_alive(const uint8_t *o){
    return o[0]==1u&&o[0x180]<0x80u&&!(som_gen_u16(o+0x190)&0x8000u);
}
/* A monster that was killed: its slot shows the dying marker +0x180 = 0xF9
 * or the chest 0xEA (live 2026-11-14), or status bit 0x8000. Anything else
 * (slot cleared, another object written over it) is not a defeat. */
static inline int som_gen_killed(const uint8_t *o,unsigned last_hp){
    if(last_hp==0u)return 1;
    return o[0]==1u&&(o[0x180]==0xF9u||o[0x180]==0xEAu||(som_gen_u16(o+0x190)&0x8000u));
}
static inline unsigned som_gen_map(const uint8_t *ram){return som_gen_u16(ram+0xDC)&0x1FFu;}
static inline int som_gen_field(const uint8_t *ram){
    return ram[0xE000]==1u&&ram[0x00E4]==1u&&ram[0x00E6]==0x0Fu;
}
static inline unsigned som_gen_cooldown_cap(unsigned rank){
    const int cap=99-12*(int)rank;
    return (unsigned)(cap<40?40:cap);
}
static inline unsigned som_gen_tint_profile(unsigned rank){
    return rank>=3u?SOM_PAL_DARK_CRIMSON:SOM_PAL_DIVINE_GOLD;
}

/* The built-in roster when no file exists yet. */
static inline unsigned som_gen_defaults(SomGeneral *g){
    memset(g,0,sizeof(*g));
    snprintf(g->name,sizeof(g->name),"RABBITE ELITE");
    g->species=0x00;g->home_map=130;g->rank=1;g->nemesis=-1;   /* Rabites west of Potos */
    return 1;
}

/* Text: the tag line, then one general per line:
 * species map rank encounters defeats wins escapes d0..d7 nemesis
 *   h<24 hex bytes: 3 heroes x 8 habits> name                     (v2)
 * Version 1 files (no habit field) still load, with no habits. */
static inline size_t som_gen_format(const SomGeneral *g,unsigned count,char *out,size_t size){
    size_t n=(size_t)snprintf(out,size,"%s\n",SOM_GEN_FILE_TAG);
    for(unsigned i=0;i<count&&n<size;i++){
        const SomGeneral *e=&g[i];
        char habits[2u*SOM_GEN_HEROES*SOM_GEN_HABITS+1u];
        for(unsigned k=0;k<SOM_GEN_HEROES*SOM_GEN_HABITS;k++)
            snprintf(habits+2u*k,3,"%02X",e->habit[k/SOM_GEN_HABITS][k%SOM_GEN_HABITS]);
        n+=(size_t)snprintf(out+n,size-n,"%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %d h%s %s\n",
            e->species,e->home_map,e->rank,e->encounters,e->defeats,e->wins,e->escapes,
            e->damage[0],e->damage[1],e->damage[2],e->damage[3],e->damage[4],e->damage[5],
            e->damage[6],e->damage[7],e->nemesis,habits,e->name);
    }
    return n<size?n:size;
}
/* Number of generals read (0 on a bad tag or no valid line). */
static inline unsigned som_gen_parse(const char *text,SomGeneral *g,unsigned max){
    if(!text)return 0;
    const int v2=strncmp(text,SOM_GEN_FILE_TAG "\n",sizeof(SOM_GEN_FILE_TAG))==0;
    if(!v2&&strncmp(text,SOM_GEN_FILE_TAG_V1 "\n",sizeof(SOM_GEN_FILE_TAG_V1))!=0)return 0;
    const char *line=strchr(text,'\n');
    unsigned count=0;
    while(line&&*++line&&count<max){
        SomGeneral e;memset(&e,0,sizeof(e));
        int used=0;
        if(sscanf(line,"%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u %d %n",&e.species,&e.home_map,
                  &e.rank,&e.encounters,&e.defeats,&e.wins,&e.escapes,&e.damage[0],&e.damage[1],
                  &e.damage[2],&e.damage[3],&e.damage[4],&e.damage[5],&e.damage[6],&e.damage[7],
                  &e.nemesis,&used)==16&&used>0){
            const char *p=line+used;
            int hex=v2&&*p=='h';
            for(unsigned d=1;hex&&d<=2u*SOM_GEN_HEROES*SOM_GEN_HABITS;d++){
                const char c=p[d];
                hex=(c>='0'&&c<='9')||(c>='A'&&c<='F')||(c>='a'&&c<='f');
            }
            if(hex&&p[1u+2u*SOM_GEN_HEROES*SOM_GEN_HABITS]==' '){
                p++;
                for(unsigned k=0;k<SOM_GEN_HEROES*SOM_GEN_HABITS;k++,p+=2){
                    unsigned v=0;
                    for(unsigned d=0;d<2u;d++){
                        const char c=p[d];
                        v=v*16u+(unsigned)(c<='9'?c-'0':(c|0x20)-'a'+10);
                    }
                    e.habit[k/SOM_GEN_HABITS][k%SOM_GEN_HABITS]=(uint8_t)v;
                }
                while(*p==' ')p++;
            }
            size_t k=0;
            for(;*p&&*p!='\n'&&k+1<sizeof(e.name);p++)e.name[k++]=*p;
            e.name[k]=0;
            if(e.species<0x80u&&e.home_map<0x200u&&e.rank>=1u&&e.rank<=SOM_GEN_MAX_RANK&&
               e.nemesis>=-1&&e.nemesis<8&&k)g[count++]=e;
        }
        line=strchr(line,'\n');
    }
    return count;
}

static inline void som_gen_reset_live(SomGeneralLive *l){
    memset(l,0,sizeof(*l));l->slot=-1;
}
static inline int som_gen_heaviest(const unsigned damage[8]){
    int best=-1;unsigned most=0;
    for(int w=0;w<8;w++)if(damage[w]>most){most=damage[w];best=w;}
    return best;
}

/* Elects and boosts the general if it is not on the field yet; `skip` is a
 * monster slot that must not be chosen (the familiar), -1 for none. */
static inline int som_gen_elect(const SomGeneral *g,SomGeneralLive *l,uint8_t *ram,unsigned long frame,
                                int skip){
    const unsigned map=som_gen_map(ram);
    if(l->map!=map){som_gen_reset_live(l);l->map=map;}
    if(l->slot>=0||l->done||map!=g->home_map||!som_gen_field(ram))return SOM_GEN_NONE;
    for(int slot=0;slot<3;slot++){
        uint8_t *o=som_gen_object(ram,slot);
        if(slot==skip||!som_gen_alive(o)||o[0x180]!=g->species)continue;
        unsigned hp=som_gen_u16(o+0x184)*(1u+g->rank);
        if(hp>999u)hp=999u;
        som_gen_put16(o+0x184,hp);som_gen_put16(o+0x182,hp);
        uint8_t *pal=ram+SOM_GEN_PALETTE+(unsigned)slot*0x20u;
        som_pal_apply(pal,som_gen_tint_profile(g->rank));
        memcpy(l->tinted,pal,32);
        l->slot=slot;l->prev_hp=hp;l->last_tint=frame;l->elected_at=frame;l->max_hp=hp;
        memset(l->damage,0,sizeof(l->damage));
        l->hero_was_down=som_gen_u16(ram+0xE182)==0u;
        return SOM_GEN_ELECTED;
    }
    return SOM_GEN_NONE;
}

/* Records the end of an encounter in the general's memory. */
static inline void som_gen_conclude(SomGeneral *g,SomGeneralLive *l,int outcome){
    g->encounters++;
    for(int w=0;w<8;w++)g->damage[w]+=l->damage[w];
    if(outcome==SOM_GEN_DEFEATED){
        g->defeats++;
        const int heaviest=som_gen_heaviest(l->damage);
        if(heaviest>=0)g->nemesis=heaviest;
    }else if(outcome==SOM_GEN_WON)g->wins++;
    else if(outcome==SOM_GEN_ESCAPED)g->escapes++;
    if((outcome==SOM_GEN_DEFEATED||outcome==SOM_GEN_WON)&&g->rank<SOM_GEN_MAX_RANK)g->rank++;
    l->slot=-1;l->done=1;
}

/* One frame. Returns an event (DEFEATED/WON/ESCAPED end the encounter and
 * update `g`; BACKING/STANDING report a change of the adaptation). */
static inline int som_gen_tick(SomGeneral *g,SomGeneralLive *l,uint8_t *ram,unsigned long frame){
    if(l->slot<0)return SOM_GEN_NONE;
    if(som_gen_map(ram)!=l->map){som_gen_conclude(g,l,SOM_GEN_ESCAPED);return SOM_GEN_ESCAPED;}
    if(!som_gen_field(ram))return SOM_GEN_NONE;      /* menu, event, transition: wait */
    uint8_t *o=som_gen_object(ram,l->slot);
    const unsigned weapon=ram[0xE1E4]&7u;
    if(!som_gen_alive(o)||o[0x180]!=g->species){
        if(!som_gen_killed(o,l->prev_hp)){           /* gone without a fight: no record */
            l->slot=-1;l->done=1;
            return SOM_GEN_VANISHED;
        }
        if(l->prev_hp)l->damage[weapon]+=l->prev_hp;   /* the killing blow */
        som_gen_conclude(g,l,SOM_GEN_DEFEATED);
        return SOM_GEN_DEFEATED;
    }
    /* Off its home map the game rebuilds the stats (live: 160 -> 40 in
     * Potos); keep the boost, and full HP stays full. */
    if(l->max_hp&&som_gen_u16(o+0x184)!=l->max_hp){
        if(som_gen_u16(o+0x182)>=som_gen_u16(o+0x184))som_gen_put16(o+0x182,l->max_hp);
        som_gen_put16(o+0x184,l->max_hp);
    }
    const unsigned hp=som_gen_u16(o+0x182);
    if(hp<l->prev_hp)l->damage[weapon]+=l->prev_hp-hp;
    l->prev_hp=hp;
    const int hero_down=som_gen_u16(ram+0xE182)==0u;
    if(hero_down&&!l->hero_was_down){
        som_gen_conclude(g,l,SOM_GEN_WON);
        return SOM_GEN_WON;
    }
    l->hero_was_down=hero_down;
    const unsigned cap=som_gen_cooldown_cap(g->rank);
    if(o[0x1ED]>cap)o[0x1ED]=(uint8_t)cap;
    if(!l->away&&frame-l->last_tint>=SOM_GEN_RETINT_FRAMES){
        uint8_t *pal=ram+SOM_GEN_PALETTE+(unsigned)l->slot*0x20u;
        if(memcmp(pal,l->tinted,32)!=0){             /* the game reloaded it */
            som_pal_apply(pal,som_gen_tint_profile(g->rank));
            memcpy(l->tinted,pal,32);
        }
        l->last_tint=frame;
    }
    /* Adaptation: back away from its nemesis weapon while the bite recharges. */
    int back=0;
    if(g->nemesis>=0&&weapon==(unsigned)g->nemesis&&o[0x1ED]>20u&&o[0x1C]!=0x40u){
        const int mx=(int)som_gen_u16(o+2),my=(int)som_gen_u16(o+4);
        const int hx=(int)som_gen_u16(ram+0xE002),hy=(int)som_gen_u16(ram+0xE004);
        const int dx=mx-hx,dy=my-hy;
        if(abs(dx)+abs(dy)<SOM_GEN_KEEP_AWAY){
            const int sx=dx>0?1:dx<0?-1:0,sy=dy>0?1:dy<0?-1:0;
            const unsigned place=o[0x0B];
            if(sx&&som_bun_spot_open(ram,mx+sx,my,place)){som_gen_put16(o+2,(unsigned)(mx+sx));back=1;}
            if(sy&&som_bun_spot_open(ram,(int)som_gen_u16(o+2),my+sy,place)){
                som_gen_put16(o+4,(unsigned)(my+sy));back=1;
            }
        }
    }
    if(back!=l->backing){l->backing=back;return back?SOM_GEN_BACKING:SOM_GEN_STANDING;}
    return SOM_GEN_NONE;
}

/* "RABBITE ELITE R2" */
static inline void som_gen_label(const SomGeneral *g,char *out,size_t size){
    snprintf(out,size,"%s R%u",g->name,g->rank);
}

#endif
