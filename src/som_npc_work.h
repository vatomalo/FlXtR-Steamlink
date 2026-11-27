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
#include "som_class_palette.h"

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
    int own_palette;                 /* colours 1-15 straight from the object's live palette */
    int lift;                        /* px drawn above the object (a jump the game does not know) */
    int fixed,wx,wy;                 /* fixed: drawn at world (wx, wy), not at the object */
    int scale_pct;                   /* 0 or 100: as is; else shrunk about the foot point */
    unsigned profile;                /* som_class_palette profile for colours 1-15, 0 = none */
    unsigned long pose_duration;    /* one-shot source timeline in video frames, scaled to length */
    int no_rest;                     /* loop the frames without the resting pose (a walk) */
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
static inline const char *som_npc_work_refusal_ex(const uint8_t *wram,size_t size,const SomNpcWork *w,
                                        const uint8_t *pack,size_t pack_size,SomWorkEntry *entry,
                                        int allow_combat){
    if(!w)return "no command";
    if(w->dir>3u)return "bad direction";
    if(w->cycles<1u||w->cycles>SOM_WORK_MAX_CYCLES)return "bad cycle count";
    const SomNpcWalk as_walk={w->map,w->slot,w->kind,w->x,w->y,w->dir,1u};
    const char *refusal=som_npc_walk_refusal_ex(wram,size,&as_walk,allow_combat);
    if(refusal)return refusal;
    if(wram[SOM_NPC_OBJECT(w->slot)+0x0Eu]&SOM_WORK_HERO_HIDDEN)return "NPC already hidden";
    if(!pack)return "no frame pack";
    if(!som_work_find(pack,pack_size,w->kind,w->dir>2u?2u:w->dir,entry))
        return "no work frames for this kind";
    return NULL;
}

static inline const char *som_npc_work_refusal(const uint8_t *wram,size_t size,const SomNpcWork *w,
                                        const uint8_t *pack,size_t pack_size,SomWorkEntry *entry){
    return som_npc_work_refusal_ex(wram,size,w,pack,pack_size,entry,0);
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
    const unsigned tick_frames=s->pose_duration?1u:SOM_WORK_TICK_FRAMES;
    const unsigned long swing=s->pose_duration?s->pose_duration:som_work_swing_frames(&s->entry);
    unsigned long t=s->pose_duration&&s->length?
        (elapsed<s->length?elapsed*swing/s->length:swing-1u):
        elapsed%(swing+(s->no_rest?0u:SOM_WORK_REST_FRAMES));
    const uint8_t *p=s->entry.first;
    if(t>=swing)return p;            /* resting: the first pose */
    for(unsigned f=0;f<s->entry.frames;f++){
        const unsigned long len=(unsigned long)p[0]*tick_frames;
        if(t<len)return p;
        t-=len;
        p+=5+(size_t)p[3]*p[4];
    }
    return s->entry.first;
}

/* The worker's colours 1-20 (BGR555) as the game shows them now. Turbo
 * passes every sprite colour through a filter that changes with 7E:CF06
 * (measured 2026-11-17 at CF06 = 4: red and green | 1, blue / 2), so the
 * villager's own palette is taken from the colour buffer 7E:0700 (OBJ
 * palette = sprite attribute obj+0x8B bits 1-3, as the boy's 0x23 =
 * palette 1), and the weapon's five ROM colours are mapped per channel the
 * way the villager's fifteen were (piecewise linear between measured
 * values). If the buffer does not look like a filtered copy of the pack
 * colours (one ROM value -> two live values far apart, order not kept, or
 * a value moved by more than 16), the pack colours are used unchanged. Returns 1 when live colours were used. */
static inline int som_npc_work_colours(const SomNpcWorkState *s,const uint8_t *wram,uint16_t out[21]){
    for(unsigned i=1;i<=20u;i++)out[i]=s->entry.colours[i-1u];
    out[0]=0;
    if(s->profile){                 /* a class palette over the pack's own colours */
        for(unsigned i=1;i<=15u;i++)out[i]=som_pal_color(out[i],s->profile);
        return 1;
    }
    if(!wram)return 0;
    const unsigned pal=(wram[SOM_NPC_OBJECT(s->slot)+0x8Bu]>>1)&7u;
    const uint8_t *live=wram+0x0700u+32u*pal;
    if(s->own_palette){             /* a hero in his own (class) colours */
        for(unsigned i=1;i<=15u;i++)out[i]=(uint16_t)((live[2u*i]|live[2u*i+1u]<<8)&0x7FFFu);
        return 1;
    }
    int lut[3][32];
    for(unsigned c=0;c<3u;c++)for(unsigned v=0;v<32u;v++)lut[c][v]=-1;
    for(unsigned i=1;i<=15u;i++){
        const unsigned rom=s->entry.colours[i-1u],now=live[2u*i]|(unsigned)live[2u*i+1u]<<8;
        if(now&0x8000u)return 0;
        for(unsigned c=0;c<3u;c++){
            const int r=(int)((rom>>(5u*c))&31u),l=(int)((now>>(5u*c))&31u);
            if(lut[c][r]<0)lut[c][r]=l;
            else if(lut[c][r]-l>2||l-lut[c][r]>2)return 0;
        }
    }
    for(unsigned c=0;c<3u;c++){      /* a filter keeps order and moves values by <= 16 */
        int last=-1;
        for(int v=0;v<32;v++){
            if(lut[c][v]<0)continue;
            if(lut[c][v]<last-2||lut[c][v]-v>16||v-lut[c][v]>16)return 0;
            last=lut[c][v];
        }
    }
    for(unsigned i=1;i<=15u;i++)out[i]=(uint16_t)(live[2u*i]|live[2u*i+1u]<<8);
    for(unsigned i=16;i<=20u;i++){
        unsigned value=0;
        for(unsigned c=0;c<3u;c++){
            const int v=(int)((s->entry.colours[i-1u]>>(5u*c))&31u);
            int lo=-1,hi=-1;
            for(int k=v;k>=0;k--)if(lut[c][k]>=0){lo=k;break;}
            for(int k=v;k<32;k++)if(lut[c][k]>=0){hi=k;break;}
            int mapped;
            if(lo<0&&hi<0)mapped=v;
            else if(lo<0)mapped=hi?lut[c][hi]*v/hi:lut[c][hi];
            else if(hi<0)mapped=lo?lut[c][lo]*v/lo:lut[c][lo];
            else if(hi==lo)mapped=lut[c][lo];
            else mapped=lut[c][lo]+(lut[c][hi]-lut[c][lo])*(v-lo)/(hi-lo);
            if(mapped<0)mapped=0;
            if(mapped>31)mapped=31;
            value|=(unsigned)mapped<<(5u*c);
        }
        out[i]=(uint16_t)value;
    }
    return 1;
}

/* Draw a packed pose at a screen position. With no WRAM the pack
 * supplies its own colours, so a menu preview cannot affect gameplay. */
static inline int som_npc_work_draw_at(const SomNpcWorkState *s,const uint8_t *wram,
                                     unsigned long frame,int fx,int fy,uint8_t *pixels,
                                     unsigned width,unsigned height,size_t pitch,int format){
    if(!s->active||!pixels)return 0;
    const unsigned sx=width>=512?2u:1u,sy=height>=400?2u:1u;
    const int base_w=(int)(width/sx),base_h=(int)(height/sy);
    const uint8_t *p=som_npc_work_pose(s,frame-s->start);
    const int dx=(int8_t)p[1],dy=(int8_t)p[2],w=p[3],h=p[4];
    const uint8_t *px=p+5;
    const int mirror=s->dir==3u;
    uint16_t bgr[21];
    som_npc_work_colours(s,wram,bgr);
    uint32_t colours[21];
    for(unsigned i=1;i<=20u;i++){
        const unsigned c=bgr[i];
        colours[i]=som_overlay_color(format,(c&31u)*255u/31u,((c>>5)&31u)*255u/31u,
                                     ((c>>10)&31u)*255u/31u);
    }
    int drawn=0;
    const int scale=s->scale_pct>0&&s->scale_pct<100?s->scale_pct:100;
    if(scale<100){
        /* Each output pixel samples the source (nearest): the frame shrinks
         * about the foot point (0, 0), so it stays on the ground. */
        const int x0=(dx*scale)/100-1,x1=((dx+w)*scale+99)/100+1;
        const int y0=(dy*scale)/100-1,y1=((dy+h)*scale+99)/100+1;
        for(int qy=y0;qy<y1;qy++)for(int qx=x0;qx<x1;qx++){
            const int ax=qx*100+50,ay=qy*100+50;
            const int sxp=(ax>=0?ax/scale:-((-ax+scale-1)/scale))-dx;
            const int syp=(ay>=0?ay/scale:-((-ay+scale-1)/scale))-dy;
            if(sxp<0||syp<0||sxp>=w||syp>=h)continue;
            const unsigned c=px[syp*w+sxp];
            if(!c||c>20u)continue;
            const int ox=fx+(mirror?-qx-1:qx),oy=fy+qy;
            if(ox<0||oy<0||ox>=base_w||oy>=base_h)continue;
            som_overlay_fill(pixels,pitch,format,(unsigned)ox*sx,(unsigned)oy*sy,sx,sy,colours[c]);
            drawn=1;
        }
        return drawn;
    }
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

/* The pose as palette indices 1-15 (weapon colours 16-20 left out) in a
 * box x box image whose top-left is screen (*x0, *y0), placed and mirrored
 * exactly as som_npc_work_draw_at puts it at (fx, fy). For the armour pass:
 * a hero the proxy draws itself (Ukemi) is dressed like his game sprite.
 * 0 when nothing is drawn or it does not fit. */
static inline int som_npc_work_compose_at(const SomNpcWorkState *s,unsigned long frame,int fx,int fy,
                                          uint8_t *img,int box,int *x0,int *y0){
    if(!s->active||!img||box<=0)return 0;
    const uint8_t *p=som_npc_work_pose(s,frame-s->start);
    const int dx=(int8_t)p[1],dy=(int8_t)p[2],w=p[3],h=p[4];
    const uint8_t *px=p+5;
    const int mirror=s->dir==3u;
    const int scale=s->scale_pct>0&&s->scale_pct<100?s->scale_pct:100;
    int l=1<<20,t=1<<20,r=-(1<<20),b=-(1<<20);
    for(int pass=0;pass<2;pass++){
        if(pass){
            if(r<l||b<t||r-l+1>box||b-t+1>box)return 0;
            memset(img,0,(size_t)box*(size_t)box);
        }
        const int qx0=scale<100?(dx*scale)/100-1:dx,qx1=scale<100?((dx+w)*scale+99)/100+1:dx+w;
        const int qy0=scale<100?(dy*scale)/100-1:dy,qy1=scale<100?((dy+h)*scale+99)/100+1:dy+h;
        for(int qy=qy0;qy<qy1;qy++)for(int qx=qx0;qx<qx1;qx++){
            int sxp=qx-dx,syp=qy-dy;
            if(scale<100){
                const int ax=qx*100+50,ay=qy*100+50;
                sxp=(ax>=0?ax/scale:-((-ax+scale-1)/scale))-dx;
                syp=(ay>=0?ay/scale:-((-ay+scale-1)/scale))-dy;
            }
            if(sxp<0||syp<0||sxp>=w||syp>=h)continue;
            const unsigned c=px[syp*w+sxp];
            if(!c||c>15u)continue;
            const int ox=fx+(mirror?-qx-1:qx),oy=fy+qy;
            if(!pass){if(ox<l)l=ox;if(ox>r)r=ox;if(oy<t)t=oy;if(oy>b)b=oy;}
            else img[(oy-t)*box+(ox-l)]=(uint8_t)c;
        }
    }
    *x0=l;*y0=t;
    return 1;
}
static inline int som_npc_work_compose(const SomNpcWorkState *s,const uint8_t *wram,unsigned long frame,
                                       uint8_t *img,int box,int *x0,int *y0){
    if(!s->active||!wram)return 0;
    const unsigned o=SOM_NPC_OBJECT(s->slot);
    const unsigned cam=(wram[o+0x0Bu]&0x80u)?0xACu:0xA8u;
    const int fx=s->fixed?(int16_t)((unsigned)s->wx-som_npc_u16(wram,cam)):
                          (int16_t)(som_npc_u16(wram,o+0x02u)-som_npc_u16(wram,cam));
    const int fy=s->fixed?(int16_t)((unsigned)s->wy-som_npc_u16(wram,cam+2u))-s->lift:
                          (int16_t)(som_npc_u16(wram,o+0x04u)-som_npc_u16(wram,cam+2u))-
                          wram[o+0x45u]-s->lift;
    return som_npc_work_compose_at(s,frame,fx,fy,img,box,x0,y0);
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
    const int fx=s->fixed?(int16_t)((unsigned)s->wx-som_npc_u16(wram,cam)):
                          (int16_t)(som_npc_u16(wram,o+0x02u)-som_npc_u16(wram,cam));
    const int fy=s->fixed?(int16_t)((unsigned)s->wy-som_npc_u16(wram,cam+2u))-s->lift:
                          (int16_t)(som_npc_u16(wram,o+0x04u)-som_npc_u16(wram,cam+2u))-
                          wram[o+0x45u]-s->lift;
    return som_npc_work_draw_at(s,wram,frame,fx,fy,pixels,width,height,pitch,format);
}

#endif
