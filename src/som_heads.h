#ifndef FLXTR_SOM_HEADS_H
#define FLXTR_SOM_HEADS_H
/* The game's own heads as hair styles (player, 2026-11-27: drawn hair caps
 * "were the worst! can't we use ingame hairstyles?" ... "all humans").
 *
 * tools/som_head_pack.py reads every person's head from the player's ROM
 * (16 x 16 per facing, colours by role: 1 outline, 2-4 hair, 5-7 skin,
 * 15 white, 16+ other colours). Each frame, before the core draws, this
 * finds the hero's sprite pieces in the OAM shadow and works out, for each
 * of their 8 x 8 tiles that the head covers, the tile the core should draw
 * instead (through the sprite atlas hook, retro_flxtr_set_obj_lookup): the
 * borrowed head inside its 16 x 16 square, the game's own pixels outside it,
 * and the hero's old hair cleared where the new head has none. The core then
 * draws it like any sprite: its priority, clipping and the real background
 * through every transparent pixel. Colours are the hero's own palette:
 * hair on the hair entries (so the creator's hair colour applies), skin on
 * the hero's skin, other colours on the nearest hero colour.
 *
 * Limits: a tile the game draws twice (the girl's and the sprite's mirrored
 * halves facing up and down) carries one picture, so the head is mirrored
 * there; the game's blank tiles are left alone, so a head wider than the
 * hero's own pieces is cut at their edge. Same code for all three heroes. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "som_armour.h"

#define SOM_HEAD_SIZE 16
#define SOM_HEAD_ENTRY (1u+9u+1u+24u+3u*SOM_HEAD_SIZE*SOM_HEAD_SIZE)
#define SOM_HEADS_MAX 96u
typedef struct { uint8_t *data; unsigned count; } SomHeadPack;

static inline void som_heads_free(SomHeadPack *p){free(p->data);p->data=NULL;p->count=0;}
static inline int som_heads_parse(SomHeadPack *p,const uint8_t *bytes,size_t size){
    som_heads_free(p);
    if(!bytes||size<10u||memcmp(bytes,"SOMHEAD1",8))return 0;
    const unsigned n=bytes[8]|(unsigned)bytes[9]<<8;
    if(!n||n>SOM_HEADS_MAX||size<10u+(size_t)n*SOM_HEAD_ENTRY)return 0;
    p->data=(uint8_t*)malloc((size_t)n*SOM_HEAD_ENTRY);
    if(!p->data)return 0;
    memcpy(p->data,bytes+10,(size_t)n*SOM_HEAD_ENTRY);
    p->count=n;
    return 1;
}
static inline const uint8_t *som_head_entry(const SomHeadPack *p,unsigned i){
    return p->data&&i<p->count?p->data+(size_t)i*SOM_HEAD_ENTRY:NULL;
}
/* RGB of the source's own hair (dark, mid, light), k 0..2. */
static inline const uint8_t *som_head_hair(const uint8_t *e,unsigned k){return e+1u+3u*(k<3u?k:2u);}
/* The role at (x, y) of view 0 up, 1 down, 2 side (facing right). */
static inline uint8_t som_head_role(const uint8_t *e,unsigned view,int x,int y){
    if(x<0||y<0||x>=SOM_HEAD_SIZE||y>=SOM_HEAD_SIZE||view>2u)return 0;
    return e[35u+view*SOM_HEAD_SIZE*SOM_HEAD_SIZE+(unsigned)y*SOM_HEAD_SIZE+(unsigned)x];
}
/* The hero's palette entry for a role: skin is 5-7 for the boy, 6-7 for the
 * girl and the sprite (their 5 is clothes); extras the nearest colour. */
static inline uint8_t som_head_index(const uint8_t *e,unsigned who,uint8_t role,const uint8_t pal[32]){
    if(!role)return 0;
    if(role<=4u||role==15u)return role;
    if(role<=7u)return who==0u?role:(uint8_t)(role==5u?6u:role);
    const unsigned k=role-16u;
    if(k>=8u||k>=e[10])return 1;
    const uint8_t *rgb=e+11u+3u*k;
    unsigned best=1,dist=~0u;
    for(unsigned i=1;i<16u;i++){
        const unsigned c=pal[2u*i]|(unsigned)pal[2u*i+1u]<<8;
        const int dr=(int)((c&31u)<<3)-rgb[0],dg=(int)(((c>>5)&31u)<<3)-rgb[1],db=(int)(((c>>10)&31u)<<3)-rgb[2];
        const unsigned d=(unsigned)(dr*dr+dg*dg+db*db);
        if(d<dist){dist=d;best=i;}
    }
    return (uint8_t)best;
}
/* The tiles to draw instead this frame: tile -> 64 colour indices. */
typedef struct {
    uint8_t has[512];
    uint8_t pixels[512][64];
} SomHeadTiles;

/* One hero: who 0..2, head entry e, OAM shadow oam (544 bytes), VRAM, the
 * hero's live palette (16 BGR555). Returns how many tiles it replaced. */
static inline unsigned som_heads_hero(SomHeadTiles *out,const uint8_t *e,unsigned who,unsigned facing,
                                      const uint8_t *oam,const uint8_t *vram,const uint8_t pal[32]){
    if(!e||!oam||!vram||who>2u)return 0;
    static uint8_t img[SOM_ARMOUR_BOX][SOM_ARMOUR_BOX];
    int x0,y0;
    if(!som_armour_compose(oam,vram,who,img,&x0,&y0))return 0;
    int top=-1;
    for(int y=0;y<SOM_ARMOUR_BOX&&top<0;y++)for(int x=0;x<SOM_ARMOUR_BOX;x++)if(img[y][x]){top=y;break;}
    if(top<0)return 0;
    int l=SOM_ARMOUR_BOX,r=-1;
    for(int y=top;y<top+6&&y<SOM_ARMOUR_BOX;y++)for(int x=0;x<SOM_ARMOUR_BOX;x++)
        if(img[y][x]){if(x<l)l=x;if(x>r)r=x;}
    const int side=facing==0x02u||facing==0x82u;
    const unsigned view=side?2u:facing==0x00u?0u:1u;
    const int mirror=facing==0x82u;
    /* the borrowed head's own top row and chin (its last skin row) */
    int htop=-1,chin=-1;
    for(int y=0;y<SOM_HEAD_SIZE;y++)for(int x=0;x<SOM_HEAD_SIZE;x++){
        const uint8_t q=som_head_role(e,view,x,y);
        if(q&&htop<0)htop=y;
        if(q>=5u&&q<=7u)chin=y;
    }
    if(htop<0)return 0;
    if(chin<0)chin=SOM_HEAD_SIZE-4;                    /* from behind: no face */
    /* screen square of the head: its top on the hero's top row, centred */
    const int hx=x0+(l+r+1)/2-SOM_HEAD_SIZE/2,hy=y0+top-htop;
    unsigned n=0;
    for(unsigned j=0;j<128u;j++){
        const uint8_t *o=oam+4u*j;
        if(!som_armour_piece(o,who))continue;
        const unsigned hb=(oam[512u+j/4u]>>(2u*(j%4u)))&3u;
        if(hb&2u)continue;
        int px=o[0]+256*(int)(hb&1u);if(px>=256)px-=512;
        const int py=o[1];
        if(px+16<=hx||px>=hx+SOM_HEAD_SIZE||py+16<=hy||py>=hy+SOM_HEAD_SIZE)continue;
        const unsigned base=o[2]|((unsigned)(o[3]&1u)<<8),hflip=(o[3]>>6)&1u,vflip=(o[3]>>7)&1u;
        for(unsigned q=0;q<4u;q++){
            const unsigned tile=(base&256u)|((base+(q&1u)+16u*(q>>1))&255u);   /* as the PPU wraps */
            if(out->has[tile])continue;                  /* drawn twice: the first picture */
            /* the native tile; blank tiles are left alone */
            uint8_t native[64];int any=0;
            for(unsigned ty=0;ty<8u;ty++){
                const uint8_t *row=som_armour_row(vram,base,q&1u,(q>>1)*8u+ty);
                for(unsigned tx=0;tx<8u;tx++){
                    const unsigned bit=7u-tx;
                    const uint8_t c=(uint8_t)(((row[0]>>bit)&1u)|(((row[1]>>bit)&1u)<<1)|
                                              (((row[16]>>bit)&1u)<<2)|(((row[17]>>bit)&1u)<<3));
                    native[ty*8u+tx]=c;any|=c;
                }
            }
            if(!any)continue;
            uint8_t *dst=out->pixels[tile];
            int touched=0;
            for(unsigned ty=0;ty<8u;ty++)for(unsigned tx=0;tx<8u;tx++){
                /* where this tile pixel lands on screen */
                const unsigned ux=(q&1u)*8u+tx,uy=(q>>1)*8u+ty;
                const int sx=px+(int)(hflip?15u-ux:ux),sy=py+(int)(vflip?15u-uy:uy);
                uint8_t c=native[ty*8u+tx];
                const int u=sx-hx,v=sy-hy;
                if(u>=0&&v>=0&&u<SOM_HEAD_SIZE&&v<SOM_HEAD_SIZE){
                    const uint8_t role=som_head_role(e,view,mirror?SOM_HEAD_SIZE-1-u:u,v);
                    if(role){c=som_head_index(e,who,role,pal);touched=1;}
                    else if(v<=chin&&(c==1u||(c>=2u&&c<=4u)||(who==0u&&c>=8u&&c<=10u))){c=0;touched=1;}
                }
                dst[ty*8u+tx]=c;
            }
            if(touched){out->has[tile]=1;n++;}
        }
    }
    return n;
}
#endif
