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
#define SOM_HEAD_EYES 35u
#define SOM_HEAD_PIXELS 41u
#define SOM_HEAD_ENTRY (SOM_HEAD_PIXELS+3u*SOM_HEAD_SIZE*SOM_HEAD_SIZE)
#define SOM_HEADS_MAX 96u
typedef struct { uint8_t *data; unsigned count; } SomHeadPack;

static inline void som_heads_free(SomHeadPack *p){free(p->data);p->data=NULL;p->count=0;}
static inline int som_heads_parse(SomHeadPack *p,const uint8_t *bytes,size_t size){
    som_heads_free(p);
    if(!bytes||size<10u||memcmp(bytes,"SOMHEAD2",8))return 0;
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
    return e[SOM_HEAD_PIXELS+view*SOM_HEAD_SIZE*SOM_HEAD_SIZE+(unsigned)y*SOM_HEAD_SIZE+(unsigned)x];
}
/* The head's eye centre in view 0-2 (the pack measured it): 1 if known.
 * A side view without one borrows the front's. */
static inline int som_head_eyes(const uint8_t *e,unsigned view,int *x,int *y){
    if(!e||view>2u)return 0;
    const uint8_t *p=e+SOM_HEAD_EYES+2u*view;
    if(p[0]==0xFFu&&view==2u)p=e+SOM_HEAD_EYES+2u;
    if(p[0]==0xFFu)return 0;
    *x=p[0];*y=p[1];
    return 1;
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
/* Hair only (player, 2026-11-27: "use mask to remove the face and
 * shoulders keeping just the hair"): the head's hair (roles 2-4), and its
 * outline and other colours (ribbons, bows) where they touch the hair. Face,
 * eyes, skin and shoulders are dropped: the hero keeps his own. 0 = none. */
static inline uint8_t som_head_hair_role(const uint8_t *e,unsigned view,int x,int y){
    const uint8_t q=som_head_role(e,view,x,y);
    if(q>=2u&&q<=4u)return q;
    if(q!=1u&&q<16u)return 0;
    static const int d[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for(unsigned k=0;k<4u;k++){
        const uint8_t n=som_head_role(e,view,x+d[k][0],y+d[k][1]);
        if(n>=2u&&n<=4u)return q;
    }
    return 0;
}
/* The tiles to draw instead this frame: tile -> 64 colour indices. */
typedef struct {
    uint8_t has[512];
    uint8_t pixels[512][64];
    /* where each hero's head went this frame (for the class headband) */
    uint8_t on[3],view[3],mirror[3];
    int hx[3],hy[3],eye[3];
    const uint8_t *entry[3];
} SomHeadTiles;

/* One hero: who 0..2, head entry e, OAM shadow oam (544 bytes), VRAM, the
 * hero's live palette (16 BGR555). Returns how many tiles it replaced. */
static inline unsigned som_heads_hero(SomHeadTiles *out,const uint8_t *e,unsigned who,unsigned facing,
                                      const uint8_t *oam,const uint8_t *vram,const uint8_t pal[32]){
    if(!e||!oam||!vram||who>2u)return 0;
    /* Anchored on the head's own piece (player, 2026-11-27: "the heads
     * connect badly to animations"): the hero's 16 x 16 piece with the most
     * hair (indices 2-4) this frame, and any piece beside it on the same row
     * with hair (the girl's and the sprite's mirrored halves). Arms, raised
     * hands and the body never move the head. */
    int best=-1,best_hair=0,hp_y=0,hp_l=0,hp_r=0;
    int hair_of[128];
    for(unsigned j=0;j<128u;j++){
        hair_of[j]=0;
        const uint8_t *o=oam+4u*j;
        if(!som_armour_piece(o,who))continue;
        const unsigned hb=(oam[512u+j/4u]>>(2u*(j%4u)))&3u;
        if(hb&2u)continue;
        const unsigned base=o[2]|((unsigned)(o[3]&1u)<<8);
        int count=0;
        for(unsigned v=0;v<16u;v++)for(unsigned half=0;half<2u;half++){
            const uint8_t *row=som_armour_row(vram,base,half,v);
            for(unsigned b=0;b<8u;b++){
                const unsigned c=((row[0]>>b)&1u)|(((row[1]>>b)&1u)<<1)|(((row[16]>>b)&1u)<<2)|(((row[17]>>b)&1u)<<3);
                count+=c>=2u&&c<=4u;
            }
        }
        hair_of[j]=count;
        if(count>best_hair){best_hair=count;best=(int)j;}
    }
    if(best<0||best_hair<6)return 0;
    {
        const uint8_t *o=oam+4u*(unsigned)best;
        const unsigned hb=(oam[512u+(unsigned)best/4u]>>(2u*((unsigned)best%4u)))&3u;
        int px=o[0]+256*(int)(hb&1u);if(px>=256)px-=512;
        hp_y=o[1];hp_l=px;hp_r=px+16;
        for(unsigned j=0;j<128u;j++){
            const uint8_t *q=oam+4u*j;
            if(!hair_of[j]||hair_of[j]*4<best_hair||q[1]!=o[1])continue;
            const unsigned hq=(oam[512u+j/4u]>>(2u*(j%4u)))&3u;
            int qx=q[0]+256*(int)(hq&1u);if(qx>=256)qx-=512;
            if(qx<hp_l)hp_l=qx;
            if(qx+16>hp_r)hp_r=qx+16;
        }
    }
    const int side=facing==0x02u||facing==0x82u;
    const unsigned view=side?2u:facing==0x00u?0u:1u;
    const int mirror=facing==0x82u;
    /* By the eyes (player, 2026-11-27: "use the eyes to place the
     * hairstyles"): the head's eye centre (the pack's, per view) goes on the
     * hero's own, the mean of his eye whites (index 15) in the head piece and
     * the piece below it this frame. When his eyes do not show (from behind,
     * mid-turn) the last offset seen for this view, relative to the head
     * piece, is used. */
    static int seen[3][3],off_x[3][3]={{0,0,0},{0,0,0},{0,0,0}},off_y[3][3]={{13,13,13},{13,13,13},{13,13,13}};
    {
        int sx_sum=0,sy_sum=0,count=0;
        for(unsigned j=0;view!=0u&&j<128u;j++){
            const uint8_t *q=oam+4u*j;
            if(!som_armour_piece(q,who))continue;
            const unsigned hq=(oam[512u+j/4u]>>(2u*(j%4u)))&3u;
            if(hq&2u||q[1]<hp_y||q[1]>hp_y+16)continue;
            int qx=q[0]+256*(int)(hq&1u);if(qx>=256)qx-=512;
            const unsigned base=q[2]|((unsigned)(q[3]&1u)<<8),hflip=(q[3]>>6)&1u,vflip=(q[3]>>7)&1u;
            for(unsigned v=0;v<16u;v++)for(unsigned half=0;half<2u;half++){
                const uint8_t *row=som_armour_row(vram,base,half,v);
                for(unsigned b=0;b<8u;b++){
                    const unsigned c=((row[0]>>b)&1u)|(((row[1]>>b)&1u)<<1)|(((row[16]>>b)&1u)<<2)|(((row[17]>>b)&1u)<<3);
                    if(c!=15u)continue;
                    const unsigned ux=half*8u+(7u-b);
                    const int ex=qx+(int)(hflip?15u-ux:ux),ey=q[1]+(int)(vflip?15u-v:v);
                    if(ey-hp_y<4||ey-hp_y>22)continue;
                    sx_sum+=ex;sy_sum+=ey;count++;
                }
            }
        }
        const int mid=(hp_l+hp_r)/2;
        if(count){
            off_x[who][view]=sx_sum/count-mid;off_y[who][view]=sy_sum/count-hp_y;seen[who][view]=1;   /* rounded down, like the pack */
        }else if(!seen[who][view]&&seen[who][1]){                 /* never seen this way: the front's */
            off_x[who][view]=view==2u?off_x[who][view]:off_x[who][1];off_y[who][view]=off_y[who][1];
        }
    }
    int ex=8,ey=-1;
    if(!som_head_eyes(e,view,&ex,&ey)&&!som_head_eyes(e,1u,&ex,&ey)){ex=8;ey=12;}
    if(view==0u){ex=SOM_HEAD_SIZE-1-ex;}                         /* the front's eyes, seen from behind */
    if(mirror)ex=SOM_HEAD_SIZE-1-ex;
    int htop=-1;
    for(int y=0;y<SOM_HEAD_SIZE&&htop<0;y++)for(int x=0;x<SOM_HEAD_SIZE;x++)if(som_head_role(e,view,x,y)){htop=y;break;}
    if(htop<0)return 0;
    const int eye=ey;
    const int hx=(hp_l+hp_r)/2+off_x[who][view]-ex,hy=hp_y+off_y[who][view]-ey;
    /* The hero's own hair (and the girl's bow, the sprite's puffs) goes above
     * his eyes where the new hair does not cover it; inside the skull it is
     * skin, so no hole opens over the forehead. His face and headband stay. */
    const int eye_line=hp_y+off_y[who][view],cx=(hp_l+hp_r)/2;
    int hair_top=SOM_HEAD_SIZE;                        /* the new hair's first row */
    for(int y=0;y<SOM_HEAD_SIZE&&hair_top==SOM_HEAD_SIZE;y++)for(int x=0;x<SOM_HEAD_SIZE;x++)
        if(som_head_hair_role(e,view,x,y)){hair_top=y;break;}
    const uint16_t own_hair=who==0u?0x001Cu:who==1u?0x031Cu:0x011Cu;
    out->on[who]=1;out->hx[who]=hx;out->hy[who]=hy;out->eye[who]=eye;out->view[who]=(uint8_t)view;
    out->mirror[who]=(uint8_t)mirror;out->entry[who]=e;
    unsigned n=0;
    for(unsigned j=0;j<128u;j++){
        const uint8_t *o=oam+4u*j;
        if(!som_armour_piece(o,who))continue;
        const unsigned hb=(oam[512u+j/4u]>>(2u*(j%4u)))&3u;
        if(hb&2u)continue;
        int px=o[0]+256*(int)(hb&1u);if(px>=256)px-=512;
        const int py=o[1];
        if(py+16<=hy||py>=hy+SOM_HEAD_SIZE)if(!(py==hp_y&&px+16>hp_l&&px<hp_r))continue;
        const int head_piece=py==hp_y&&px+16>hp_l&&px<hp_r;
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
                const uint8_t role=u>=0&&v>=0&&u<SOM_HEAD_SIZE&&v<SOM_HEAD_SIZE?
                    som_head_hair_role(e,view,mirror?SOM_HEAD_SIZE-1-u:u,v):0u;
                const int old_hair=(own_hair>>c&1u)!=0u,old_line=c==1u&&sy<eye_line-4;
                if(role){c=som_head_index(e,who,role,pal);touched=1;}
                else if(head_piece&&sy<eye_line&&(old_hair||old_line)){
                    const int dx=sx-cx<0?cx-sx:sx-cx,top=eye_line-11;
                    /* skin only under the new hair's top: above a shorter style the
                     * old hair's place is clear, not a block of skin */
                    const int skull=sy>=top&&sy>=hy+hair_top&&sy<=eye_line-2&&dx<=5&&!(sy<top+2&&dx>3);
                    c=skull?(uint8_t)(dx>=4||sy<top+1?6u:7u):0u;touched=1;
                }
                dst[ty*8u+tx]=c;
            }
            if(touched){out->has[tile]=1;n++;}
        }
    }
    return n;
}
#endif
