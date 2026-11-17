/* Living world: villagers at work (EXPERIMENTAL, same opt-in as npc_walk).
 *
 * Villager sprites only stand and walk, so a working villager is drawn by
 * the proxy: frames pre-rendered on the host from the ROM by
 * tools/som_villager_work.py (the villager's head on the boy's swing, the
 * body in the villager's colours, plus the weapon) are copied onto the
 * proxy's private video frame at the villager's foot point.
 *
 * The one game-RAM write: obj+0x0E bit 7, the flag the game's own
 * TOGGLE_HERO_VIS event sets to hide an actor (som-decomp cutscene-engine.md;
 * the sprite update $C0:E40E stops at a negative +0x0E, graphics.md), so the
 * native villager is not drawn underneath. It is set when the work starts
 * and cleared when it ends, and only while the slot still holds the same
 * villager on the same map: after a map change the slot belongs to another
 * object and is left alone. Collision and talking are unaffected; talking
 * to a worker ends the work.
 *
 * Command: "npc_work MAP SLOT KIND X Y DIR CYCLES" (decimal; DIR 0 up,
 * 1 down, 2 right, 3 left; X/Y as observed, a stale command is refused),
 * "npc_work_stop". Accepted only when som_npc_walk_refusal() accepts the
 * same NPC, so all of its live-state checks apply. */
#ifndef SOM_NPC_WORK_H
#define SOM_NPC_WORK_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "som_npc_walk.h"
#include "som_overlay.h"

#define SOM_WORK_PACK_FILE "/home/apps/retroarch/cores/experimental/som_work_frames.bin"
#define SOM_WORK_MAX_CYCLES 60u
#define SOM_WORK_TICK_FRAMES 5u      /* actors animate on one NMI phase of five */
#define SOM_WORK_REST_FRAMES 25u     /* the first pose is held between swings */
#define SOM_WORK_HERO_HIDDEN 0x80u

typedef struct {
    unsigned kind, weapon, facing, frames;
    uint16_t colours[20];            /* indices 1-20 */
    const uint8_t *first;            /* the first frame record */
} SomWorkEntry;

typedef struct {
    unsigned map, slot, kind, x, y, dir, cycles;
} SomNpcWork;

typedef struct {
    int active;
    unsigned map, slot, kind, dir;
    unsigned long start, length;     /* frames */
    SomWorkEntry entry;
} SomNpcWorkState;

/* Walks one frame record; NULL when it would leave the pack. */
static inline const uint8_t *som_work_next_frame(const uint8_t *p,const uint8_t *end){
    if(!p||end-p<5)return NULL;
    const size_t pixels=(size_t)p[3]*p[4];
    if((size_t)(end-p)-5u<pixels)return NULL;
    return p+5+pixels;
}

/* Finds (kind, facing) in a pack; 0 when absent or the pack is malformed.
 * Facing: 0 up, 1 down, 2 side. */
static inline int som_work_find(const uint8_t *pack,size_t size,unsigned kind,unsigned facing,
                         SomWorkEntry *out){
    if(!pack||size<10||memcmp(pack,"SOMWORK1",8)!=0)return 0;
    const uint8_t *p=pack+10,*end=pack+size;
    const unsigned entries=pack[8]|(unsigned)pack[9]<<8;
    for(unsigned e=0;e<entries;e++){
        if(end-p<44)return 0;
        SomWorkEntry entry;
        entry.kind=p[0];entry.weapon=p[1];entry.facing=p[2];entry.frames=p[3];
        for(unsigned i=0;i<20u;i++)entry.colours[i]=(uint16_t)(p[4+2*i]|p[5+2*i]<<8);
        entry.first=p+44;
        const uint8_t *q=entry.first;
        for(unsigned f=0;f<entry.frames;f++)
            if(!(q=som_work_next_frame(q,end)))return 0;
        if(entry.kind==kind&&entry.facing==facing&&entry.frames){*out=entry;return 1;}
        p=q;
    }
    return 0;
}

static inline int som_npc_parse_work(const char *line,SomNpcWork *out){
    SomNpcWork w;
    char tail;
    if(!line||!out)return 0;
    if(sscanf(line,"npc_work %u %u %u %u %u %u %u %c",&w.map,&w.slot,&w.kind,&w.x,&w.y,
              &w.dir,&w.cycles,&tail)!=7)return 0;
    *out=w;
    return 1;
}

static inline unsigned long som_work_swing_frames(const SomWorkEntry *e){
    unsigned long ticks=0;
    const uint8_t *p=e->first;
    for(unsigned f=0;f<e->frames;f++){ticks+=p[0];p+=5+(size_t)p[3]*p[4];}
    return ticks*SOM_WORK_TICK_FRAMES;
}

/* NULL when the work may start, else why not. */
static inline const char *som_npc_work_refusal(const uint8_t *wram,size_t size,const SomNpcWork *w,
                                        const uint8_t *pack,size_t pack_size,SomWorkEntry *entry){
    if(!w)return "no command";
    if(w->dir>3u)return "bad direction";
    if(w->cycles<1u||w->cycles>SOM_WORK_MAX_CYCLES)return "bad cycle count";
    const SomNpcWalk as_walk={w->map,w->slot,w->kind,w->x,w->y,w->dir,1u};
    const char *refusal=som_npc_walk_refusal(wram,size,&as_walk);
    if(refusal)return refusal;
    if(wram[SOM_NPC_OBJECT(w->slot)+0x0Eu]&SOM_WORK_HERO_HIDDEN)return "NPC already hidden";
    if(!pack)return "no frame pack";
    if(!som_work_find(pack,pack_size,w->kind,w->dir>2u?2u:w->dir,entry))
        return "no work frames for this kind";
    return NULL;
}

static inline void som_npc_work_begin(SomNpcWorkState *s,uint8_t *wram,const SomNpcWork *w,
                               const SomWorkEntry *entry,unsigned long frame){
    s->active=1;s->map=w->map;s->slot=w->slot;s->kind=w->kind;s->dir=w->dir;
    s->entry=*entry;s->start=frame;
    s->length=(som_work_swing_frames(entry)+SOM_WORK_REST_FRAMES)*w->cycles;
    wram[SOM_NPC_OBJECT(w->slot)+0x0Eu]|=SOM_WORK_HERO_HIDDEN;
}

/* 1 when the slot still holds the villager the work started on. */
static inline int som_npc_work_same(const SomNpcWorkState *s,const uint8_t *wram){
    const unsigned o=SOM_NPC_OBJECT(s->slot);
    return wram[0xE000]==1u&&wram[0x00E4]==1u&&wram[0x00E6]==0x0Fu&&
           (som_npc_u16(wram,0x00DC)&0x1FFu)==s->map&&wram[o]==1u&&wram[o+0x3Du]==s->kind;
}

/* Ends the work: shows the native villager again if it is still there. */
static inline void som_npc_work_end(SomNpcWorkState *s,uint8_t *wram){
    if(!s->active)return;
    if(wram&&som_npc_work_same(s,wram))
        wram[SOM_NPC_OBJECT(s->slot)+0x0Eu]&=(uint8_t)~SOM_WORK_HERO_HIDDEN;
    s->active=0;
}

/* Once per frame: NULL while the work goes on, else why it ended (and it
 * has been ended). */
static inline const char *som_npc_work_tick(SomNpcWorkState *s,uint8_t *wram,unsigned long frame){
    if(!s->active)return NULL;
    const char *why=NULL;
    if(!som_npc_work_same(s,wram))why="villager or map gone";
    else if(wram[0x00D0])why="event (talk) started";
    else if(som_npc_u16(wram,0x003A)==SOM_NPC_OBJECT(s->slot))why="talk target";
    else if(frame-s->start>=s->length)why="done";
    if(why)som_npc_work_end(s,wram);
    return why;
}

/* The frame record shown `elapsed` frames into the work. */
static inline const uint8_t *som_npc_work_pose(const SomNpcWorkState *s,unsigned long elapsed){
    const unsigned long swing=som_work_swing_frames(&s->entry);
    unsigned long t=elapsed%(swing+SOM_WORK_REST_FRAMES);
    const uint8_t *p=s->entry.first;
    if(t>=swing)return p;            /* resting: the first pose */
    for(unsigned f=0;f<s->entry.frames;f++){
        const unsigned long len=(unsigned long)p[0]*SOM_WORK_TICK_FRAMES;
        if(t<len)return p;
        t-=len;
        p+=5+(size_t)p[3]*p[4];
    }
    return s->entry.first;
}

/* Draws the worker on the frame copy (base screen 256x224, doubled when the
 * frame is). The foot point is world minus the camera, as $C0:E443 computes
 * +0x20/+0x22 (+0x0B bit 7 selects the second camera), lifted by the jump
 * height +0x45. Returns 1 when something was drawn. */
static inline int som_npc_work_draw(const SomNpcWorkState *s,const uint8_t *wram,unsigned long frame,
                             uint8_t *pixels,unsigned width,unsigned height,size_t pitch,
                             int format){
    if(!s->active||!pixels||!wram)return 0;
    const unsigned o=SOM_NPC_OBJECT(s->slot);
    const unsigned cam=(wram[o+0x0Bu]&0x80u)?0xACu:0xA8u;
    const int fx=(int16_t)(som_npc_u16(wram,o+0x02u)-som_npc_u16(wram,cam));
    const int fy=(int16_t)(som_npc_u16(wram,o+0x04u)-som_npc_u16(wram,cam+2u))-wram[o+0x45u];
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    const int base_w=(int)(width/sx),base_h=(int)(height/sy);
    const uint8_t *p=som_npc_work_pose(s,frame-s->start);
    const int dx=(int8_t)p[1],dy=(int8_t)p[2],w=p[3],h=p[4];
    const uint8_t *px=p+5;
    const int mirror=s->dir==3u;
    uint32_t colours[21];
    for(unsigned i=1;i<=20u;i++){
        const unsigned c=s->entry.colours[i-1u];
        colours[i]=som_overlay_color(format,(c&31u)*255u/31u,((c>>5)&31u)*255u/31u,
                                     ((c>>10)&31u)*255u/31u);
    }
    int drawn=0;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        const unsigned c=px[y*w+x];
        if(!c||c>20u)continue;
        const int ox=fx+(mirror?-(dx+x)-1:dx+x),oy=fy+dy+y;
        if(ox<0||oy<0||ox>=base_w||oy>=base_h)continue;
        som_overlay_fill(pixels,pitch,format,(unsigned)ox*sx,(unsigned)oy*sy,sx,sy,colours[c]);
        drawn=1;
    }
    return drawn;
}

#endif
