#ifndef FLXTR_SOM_ARMOUR_H
#define FLXTR_SOM_ARMOUR_H
/* Visible armour on the heroes' native sprites (Phase C.2).
 *
 * A hero is drawn from 2-4 OAM pieces (16x16) whose tiles the game uploads
 * per animation frame: boy and girl rows 0x1C0-0x1CF, sprite 0x1E0-0x1E7
 * (0x1E8+ are the HUD portraits). Measured on the Turbo ROM (scratchpad probe
 * arm.c, 2026-11-26) over standing, walking in four directions and swings:
 *   boy    hair 2,3,4; shirt 8,9 with dark stripes 10 - 8/9 are also the
 *          face, so the torso starts below the last hair row of the top piece;
 *          arms 5,6,7; trousers 11,12; gloves/boots 13,14.
 *   girl   hair 2,3,4 (and bow 8,9); face 5,6,7,15; dress 10,11,12 (top and
 *          skirt); the torso is the 6 rows under her hair.
 *   sprite hair 2,3,4,8 hanging beside the face; tunic 10,11,12 (also in the
 *          cap), so the torso starts at the first tunic row 9+ rows down.
 * The mask is therefore palette index AND body band, per hero and per frame:
 * it follows every pose the game draws and never takes the face, hands, hair,
 * legs or the weapon (separate tiles). Pixels covered by other sprites or
 * scenery are left alone by the painter (it only replaces a pixel that still
 * shows the native colour).
 *
 * The outfit is data (SomOutfit): layers per body slot with their own colours,
 * separate from discipline and stats. Only the torso layer is drawn so far. */
#include <stdint.h>
#include <string.h>

#define SOM_ARMOUR_BOX 48
enum { SOM_SLOT_TORSO, SOM_SLOT_COUNT };      /* head, arms, legs, ... later */

typedef struct {
    uint8_t torso;          /* bit per palette index */
    uint8_t hair;
} SomArmourIdx;
typedef struct {
    uint16_t torso_mask, hair_mask;
    uint8_t neck_from_hair; /* 1: below the last hair row; 0: first torso row */
    uint8_t head_rows;      /* rows from the top that are never torso */
    uint8_t head_scan;      /* rows from the top searched for hair */
    uint8_t height;         /* torso rows */
} SomArmourBody;
static const SomArmourBody som_armour_body[3]={
    {(1u<<8)|(1u<<9)|(1u<<10),(1u<<2)|(1u<<3)|(1u<<4),1,14,16,8},  /* boy: head 14 rows */
    {(1u<<10)|(1u<<11)|(1u<<12),(1u<<2)|(1u<<3)|(1u<<4),1,14,18,6},/* girl */
    {(1u<<10)|(1u<<11)|(1u<<12),0,0,9,0,8},                        /* sprite */
};

/* A hero's own body piece (not the weapon, not the HUD portrait). */
static inline int som_armour_piece(const uint8_t *e,unsigned who){
    const unsigned tile=e[2]|((unsigned)(e[3]&1u)<<8),row=tile&~0x10u;
    const int body=(row>=0x1C0u&&row<0x1D0u&&(who||row<0x1C8u))||(row>=0x1E0u&&row<0x1E8u);
    return body&&((e[3]>>1)&7u)==who+1u&&e[1]<0xC0u;
}
static inline unsigned som_armour_tile_pixel(const uint8_t *vram,unsigned tile,unsigned px,unsigned py){
    const unsigned word=(tile&0x100u?0x7000u:0x6000u)+(tile&0xFFu)*16u;
    const uint8_t *t=vram+((word*2u)&0xFFFFu);
    const unsigned bit=7u-px;
    return ((t[2u*py]>>bit)&1u)|(((t[2u*py+1u]>>bit)&1u)<<1)|
           (((t[16u+2u*py]>>bit)&1u)<<2)|(((t[16u+2u*py+1u]>>bit)&1u)<<3);
}
/* Composes hero `who` from OAM (0x220 bytes) and VRAM into a 48x48 index
 * image at screen (*x0,*y0), lower OAM entries on top. 0 if not drawn. */
static inline int som_armour_compose(const uint8_t *oam,const uint8_t *vram,unsigned who,
                                     uint8_t img[SOM_ARMOUR_BOX][SOM_ARMOUR_BOX],int *x0,int *y0){
    int l=512,t=512,r=-512,b=-512;
    for(unsigned j=0;j<128u;j++){
        const uint8_t *e=oam+4u*j;
        if(!som_armour_piece(e,who))continue;
        const unsigned hb=(oam[512u+j/4u]>>(2u*(j%4u)))&3u;
        if(hb&2u)continue;
        int x=e[0]+256*(int)(hb&1u);if(x>=256)x-=512;
        const int y=e[1];   /* measured: OAM y is the screen row */
        if(x<l)l=x;
        if(x+16>r)r=x+16;
        if(y<t)t=y;
        if(y+16>b)b=y+16;
    }
    if(r<=l||b<=t||r-l>SOM_ARMOUR_BOX||b-t>SOM_ARMOUR_BOX)return 0;
    memset(img,0,SOM_ARMOUR_BOX*SOM_ARMOUR_BOX);
    for(int j=127;j>=0;j--){
        const uint8_t *e=oam+4u*(unsigned)j;
        if(!som_armour_piece(e,who))continue;
        const unsigned hb=(oam[512u+(unsigned)j/4u]>>(2u*((unsigned)j%4u)))&3u;
        if(hb&2u)continue;
        int x=e[0]+256*(int)(hb&1u);if(x>=256)x-=512;
        const int y=e[1];   /* measured: OAM y is the screen row */
        const unsigned tile=e[2]|((unsigned)(e[3]&1u)<<8);
        for(unsigned py=0;py<16u;py++)for(unsigned px=0;px<16u;px++){
            const unsigned u=(e[3]&64u)?15u-px:px,v=(e[3]&128u)?15u-py:py;
            const unsigned tt=(tile&256u)|((tile+(u>>3)+16u*(v>>3))&255u);
            const unsigned c=som_armour_tile_pixel(vram,tt,u&7u,v&7u);
            if(c)img[y+(int)py-t][x+(int)px-l]=(uint8_t)c;
        }
    }
    *x0=l;*y0=t;
    return 1;
}
/* Torso band of a composed hero: rows [*from, *to). 0 if none. */
static inline int som_armour_torso_rows(const uint8_t img[SOM_ARMOUR_BOX][SOM_ARMOUR_BOX],unsigned who,
                                        int *from,int *to){
    if(who>2u)return 0;
    const SomArmourBody *b=&som_armour_body[who];
    int top=-1;
    for(int y=0;y<SOM_ARMOUR_BOX&&top<0;y++)
        for(int x=0;x<SOM_ARMOUR_BOX;x++)if(img[y][x]){top=y;break;}
    if(top<0)return 0;
    int neck=-1;
    if(b->neck_from_hair){
        for(int y=top;y<top+b->head_scan&&y<SOM_ARMOUR_BOX;y++)
            for(int x=0;x<SOM_ARMOUR_BOX;x++)
                if(img[y][x]&&(b->hair_mask>>img[y][x]&1u)){neck=y+1;break;}
        if(neck<top+b->head_rows)neck=top+b->head_rows;
    }else{
        for(int y=top+b->head_rows;y<SOM_ARMOUR_BOX&&neck<0;y++)
            for(int x=0;x<SOM_ARMOUR_BOX;x++)
                if(img[y][x]&&(b->torso_mask>>img[y][x]&1u)){neck=y;break;}
    }
    if(neck<0||neck>=SOM_ARMOUR_BOX)return 0;
    *from=neck;*to=neck+b->height>SOM_ARMOUR_BOX?SOM_ARMOUR_BOX:neck+b->height;
    return 1;
}
/* 1 where the torso layer goes. Returns the pixel count. */
static inline unsigned som_armour_torso_mask(const uint8_t img[SOM_ARMOUR_BOX][SOM_ARMOUR_BOX],unsigned who,
                                             uint8_t mask[SOM_ARMOUR_BOX][SOM_ARMOUR_BOX],int *from){
    memset(mask,0,SOM_ARMOUR_BOX*SOM_ARMOUR_BOX);
    int a,z;
    if(!som_armour_torso_rows(img,who,&a,&z))return 0;
    unsigned n=0;
    const uint16_t want=som_armour_body[who].torso_mask;
    for(int y=a;y<z;y++)for(int x=0;x<SOM_ARMOUR_BOX;x++)
        if(img[y][x]&&(want>>img[y][x]&1u)){mask[y][x]=1;n++;}
    *from=a;
    return n;
}

/* Outfits: colours per layer, independent of class and stats. Shade is
 * chosen from the native index's rank in the torso set (light, mid, dark);
 * lacing every third row from the neck. */
typedef struct { uint8_t rgb[3][3]; uint8_t lacing[3]; uint8_t lace_every; } SomArmourLayer;
typedef struct { const char *name; SomArmourLayer layer[SOM_SLOT_COUNT]; } SomOutfit;
enum { SOM_OUTFIT_NONE, SOM_OUTFIT_SAMURAI, SOM_OUTFITS };
static const SomOutfit som_outfits[SOM_OUTFITS]={
    {"none",{{{{0}},{0},0}}},
    /* lacquered lamellar: crimson plates, golden lacing */
    {"Samurai",{{{{206,44,52},{150,26,38},{92,16,28}},{232,184,72},3}}},
};
/* Colour for a masked pixel: index c (native), row from the neck. */
static inline const uint8_t *som_armour_colour(const SomOutfit *o,unsigned slot,unsigned who,unsigned c,int row){
    const SomArmourLayer *L=&o->layer[slot];
    if(L->lace_every&&row%L->lace_every==L->lace_every-1)return L->lacing;
    unsigned rank=0;
    const uint16_t set=som_armour_body[who>2u?0u:who].torso_mask;
    for(unsigned k=1;k<c;k++)if(set>>k&1u)rank++;
    return L->rgb[rank>2u?2u:rank];
}
#endif
