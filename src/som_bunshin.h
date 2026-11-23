/* Kage Bunshin: shadow clones of the hero (proxy-side, no game objects).
 *
 * Clones are positions the proxy keeps beside the hero. They copy every
 * step he takes, one axis at a time, and stop at walls (the map's own tile
 * attributes, cidwel/som-decomp docs/hero-movement.md 7 and
 * tools/som_memory.py classify_tile; anything uncertain counts as a wall).
 * They are drawn over the video frame from the hero's own sprite pieces.
 *
 *  - A monster touching a clone makes it explode: every live monster in the
 *    blast takes damage through the game's pending-damage byte +0x1F1, so
 *    targets flinch, are knocked back and die normally (as the familiar).
 *  - A monster touching the hero swaps him into the clone farthest from it
 *    (substitution); the spot he left explodes.
 *  - The player can swap into any clone (A, aimed with the D-pad).
 * Pure functions on WRAM; no I/O. */
#ifndef FLXTR_SOM_BUNSHIN_H
#define FLXTR_SOM_BUNSHIN_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SOM_BUN_COUNT 2u
#define SOM_BUN_FRAMES 600ul        /* clones last 10 s */
#define SOM_BUN_SPACING 24
#define SOM_BUN_SPACING_Y 32         /* clones above / below the hero */
#define SOM_BUN_TOUCH_X 14          /* monster touching a clone, px */
#define SOM_BUN_TOUCH_Y 12
#define SOM_BUN_HERO_TOUCH_X 18     /* a little wider: swap before the bite */
#define SOM_BUN_HERO_TOUCH_Y 14
#define SOM_BUN_BLAST_X 40
#define SOM_BUN_BLAST_Y 32
#define SOM_BUN_SWAP_COOLDOWN 30ul
#define SOM_BUN_SWAP_MIN 12            /* px: closer clones are not swap targets */
#define SOM_BUN_MONSTER_BASE 0xE600u
#define SOM_BUN_OBJECT 0x200u
#define SOM_BUN_BOOMS 6u
#define SOM_BUN_BOOM_FRAMES 28ul

typedef struct {
    int x[SOM_BUN_COUNT],y[SOM_BUN_COUNT];
    int ox[SOM_BUN_COUNT],oy[SOM_BUN_COUNT];   /* formation spot, from the hero */
    uint8_t alive[SOM_BUN_COUNT];
    int hero_x,hero_y;              /* hero position the clones last followed */
    unsigned long until,swap_ready;
    unsigned map;
} SomBunshin;

typedef struct {                    /* explosion or smoke puff, world px */
    int x,y,smoke;
    unsigned long start;
} SomBoom;

static inline unsigned som_bun_u16(const uint8_t *p){return (unsigned)(p[0]|p[1]<<8);}

/* One map tile the hero can surely stand on. `place` is the hero's +0x0B
 * byte: bit 7 = second tile layer, bits 0-1 = floor height. */
static inline int som_bun_tile_open(const uint8_t *ram,int x,int y,unsigned place){
    if(x<0||y<0)return 0;
    const int layer2=(place&0x80u)!=0;
    const unsigned floor=place&3u;
    const unsigned index=(((unsigned)y>>4)&0x7Fu)*128u+(((unsigned)x>>4)&0x7Fu);
    const unsigned id=ram[(layer2?0x14000u:0x10000u)+index];
    const uint8_t *a=ram+(layer2?0x1BC00u:0x1B800u)+id*4u;
    const unsigned terrain=a[0]&7u,ground=a[2];
    if(a[0]&8u)return 0;                         /* layer switch */
    if(terrain==3u)return 0;                     /* wall */
    if((terrain==1u||terrain==2u)&&(floor==1u||floor==2u)&&terrain!=floor)
        return 0;                                /* another floor height */
    if((ground&0xF0u)==0x10u&&(ground&7u)>=4u)return 0;   /* half tile */
    if((ground>=0x30u&&ground<=0x3Fu&&ground!=0x31u&&ground!=0x32u)||
       (ground>=0xD0u&&ground<=0xD2u))return 0;  /* moving / special ground */
    return 1;
}
/* The hero's 13x9 box at (x, y) is on open tiles inside the map. */
static inline int som_bun_spot_open(const uint8_t *ram,int x,int y,unsigned place){
    const int w=(int)som_bun_u16(ram+0xC0),h=(int)som_bun_u16(ram+0xC2);
    if(x<8||y<8||x>w-8||y>h-8)return 0;
    return som_bun_tile_open(ram,x-6,y-4,place)&&som_bun_tile_open(ram,x+6,y-4,place)&&
           som_bun_tile_open(ram,x-6,y+4,place)&&som_bun_tile_open(ram,x+6,y+4,place);
}
static inline unsigned som_bun_alive(const SomBunshin *b){
    unsigned n=0;
    for(unsigned i=0;i<SOM_BUN_COUNT;i++)n+=b->alive[i]?1u:0u;
    return n;
}
static inline void som_bun_clear(SomBunshin *b){memset(b,0,sizeof(*b));}
static inline int som_bun_near(int ax,int ay,int bx,int by,int rx,int ry){
    return abs(ax-bx)<=rx&&abs(ay-by)<=ry;
}
/* Each clone's formation spot is its offset from the hero. */
static inline void som_bun_reform(SomBunshin *b,int hx,int hy){
    for(unsigned i=0;i<SOM_BUN_COUNT;i++){b->ox[i]=b->x[i]-hx;b->oy[i]=b->y[i]-hy;}
    b->hero_x=hx;b->hero_y=hy;
}
/* Two clones either side of the hero, across his facing (a hero facing
 * right gets them above and below). Where that side is blocked a clone
 * stands diagonally behind him, then in front, and only then close beside him; the two
 * never share a spot (live 2026-11-23: a tree below the hero put both at
 * 16 px and they tangled with him). Returns how many found open ground. */
static inline unsigned som_bun_spawn(SomBunshin *b,const uint8_t *ram,int hx,int hy,
                                     unsigned facing,unsigned place,unsigned long frame){
    som_bun_clear(b);
    const int across=(facing&0x7Fu)==0x02u;     /* facing left/right */
    const int fx=(facing&0x7Fu)==0x02u?((facing&0x80u)?-1:1):0;
    const int fy=fx?0:((facing&0x7Fu)==0x01u?1:-1);
    for(unsigned i=0;i<SOM_BUN_COUNT;i++){
        const int sign=i?1:-1;
        const int side=across?SOM_BUN_SPACING_Y:SOM_BUN_SPACING;
        /* across the facing (far, nearer), diagonally behind, diagonally
         * in front, then close beside him */
        const int spot[7][2]={
            {across?0:sign*side,across?sign*side:0},
            {across?0:sign*(side-8),across?sign*(side-8):0},
            {-fx*24+(across?0:sign*16),-fy*24+(across?sign*16:0)},
            {-fx*24+(across?0:sign*24),-fy*24+(across?sign*24:0)},
            {fx*24+(across?0:sign*16),fy*24+(across?sign*16:0)},
            {fx*24+(across?0:sign*24),fy*24+(across?sign*24:0)},
            {across?0:sign*16,across?sign*16:0}};
        for(unsigned k=0;k<7u;k++){
            const int x=hx+spot[k][0],y=hy+spot[k][1];
            if(i&&b->alive[0]&&som_bun_near(x,y,b->x[0],b->y[0],15,15))continue;
            if(som_bun_spot_open(ram,x,y,place)){
                b->x[i]=x;b->y[i]=y;b->alive[i]=1;break;
            }
        }
    }
    som_bun_reform(b,hx,hy);
    b->until=frame+SOM_BUN_FRAMES;b->map=som_bun_u16(ram+0xDC);
    return som_bun_alive(b);
}
/* The clones keep their formation: each walks back toward its spot beside
 * the hero, faster than he walks, so one held up by a wall catches up
 * instead of piling onto the hero (live 2026-11-23: two blocked clones
 * stacked on one spot). A jump of more than 16 px (a warp) is not copied:
 * they walk back to him. A swap re-forms them (som_bun_reform). */
static inline void som_bun_follow(SomBunshin *b,const uint8_t *ram,int hx,int hy,unsigned place){
    const int jx=hx-b->hero_x,jy=hy-b->hero_y;
    b->hero_x=hx;b->hero_y=hy;
    if(abs(jx)>16||abs(jy)>16)return;          /* not copied; they walk back */
    const int speed=(abs(jx)>abs(jy)?abs(jx):abs(jy))+2>4?(abs(jx)>abs(jy)?abs(jx):abs(jy))+2:4;
    for(unsigned i=0;i<SOM_BUN_COUNT;i++){
        if(!b->alive[i])continue;
        int dx=hx+b->ox[i]-b->x[i],dy=hy+b->oy[i]-b->y[i];
        if(dx>speed)dx=speed;else if(dx<-speed)dx=-speed;
        if(dy>speed)dy=speed;else if(dy<-speed)dy=-speed;
        if(dx&&som_bun_spot_open(ram,b->x[i]+dx,b->y[i],place))b->x[i]+=dx;
        if(dy&&som_bun_spot_open(ram,b->x[i],b->y[i]+dy,place))b->y[i]+=dy;
    }
}
static inline int som_bun_monster_alive(const uint8_t *o){
    return o[0]==1&&o[0x180]<0x80&&!(som_bun_u16(o+0x190)&0x8000u);
}
/* The first live monster (not `skip`, the familiar) touching (x, y), or -1. */
static inline int som_bun_toucher(const uint8_t *ram,int x,int y,int rx,int ry,int skip){
    for(int slot=0;slot<3;slot++){
        if(slot==skip)continue;
        const uint8_t *o=ram+SOM_BUN_MONSTER_BASE+(unsigned)slot*SOM_BUN_OBJECT;
        if(som_bun_monster_alive(o)&&
           som_bun_near((int)som_bun_u16(o+2),(int)som_bun_u16(o+4),x,y,rx,ry))return slot;
    }
    return -1;
}
/* Queue `damage` on every live monster in the blast. Returns how many. */
static inline unsigned som_bun_blast(uint8_t *ram,int x,int y,unsigned damage,int skip){
    unsigned hit=0;
    for(int slot=0;slot<3;slot++){
        if(slot==skip)continue;
        uint8_t *o=ram+SOM_BUN_MONSTER_BASE+(unsigned)slot*SOM_BUN_OBJECT;
        if(!som_bun_monster_alive(o)||
           !som_bun_near((int)som_bun_u16(o+2),(int)som_bun_u16(o+4),x,y,
                         SOM_BUN_BLAST_X,SOM_BUN_BLAST_Y))continue;
        const unsigned pending=o[0x1F1]+damage;
        o[0x1F1]=(uint8_t)(pending>0xFFu?0xFFu:pending);
        hit++;
    }
    return hit;
}
/* The clone to swap into (one at least SOM_BUN_SWAP_MIN px from the hero):
 * the one farthest from (fx, fy) when fleeing
 * (flee = 1), else the one lying most in direction (dir_x, dir_y) from the
 * hero, else the first alive. -1 with none. */
static inline int som_bun_pick(const SomBunshin *b,int hx,int hy,int flee,int fx,int fy,
                               int dir_x,int dir_y){
    int best=-1;long best_score=0;
    for(unsigned i=0;i<SOM_BUN_COUNT;i++){
        if(!b->alive[i])continue;
        if(som_bun_near(b->x[i],b->y[i],hx,hy,SOM_BUN_SWAP_MIN-1,SOM_BUN_SWAP_MIN-1))
            continue;                        /* standing on him: no swap */
        long score;
        if(flee)score=labs((long)b->x[i]-fx)+labs((long)b->y[i]-fy);
        else if(dir_x||dir_y)score=(long)(b->x[i]-hx)*dir_x+(long)(b->y[i]-hy)*dir_y;
        else score=-(long)i;
        if(best<0||score>best_score){best=(int)i;best_score=score;}
    }
    return best;
}
static inline void som_bun_boom(SomBoom booms[SOM_BUN_BOOMS],int x,int y,int smoke,
                                unsigned long frame){
    unsigned slot=0;unsigned long oldest=~0ul;
    for(unsigned i=0;i<SOM_BUN_BOOMS;i++){
        if(!booms[i].start){slot=i;break;}
        if(booms[i].start<oldest){oldest=booms[i].start;slot=i;}
    }
    booms[slot].x=x;booms[slot].y=y;booms[slot].smoke=smoke;booms[slot].start=frame?frame:1u;
}
#endif
