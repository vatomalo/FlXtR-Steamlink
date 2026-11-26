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
/* What the SNES draws over hero `who`: every sprite piece earlier in OAM than
 * the hero's own front-most piece (sprites overlap by OAM order whatever their
 * BG priority, so the core's depth cannot tell one hero from another; player,
 * 2026-11-27: "if player is on top of player depth is not respected"). Marks
 * their opaque pixels in a SOM_ARMOUR_COVER square whose top-left is screen
 * (cx, cy). Returns the count marked. */
#define SOM_ARMOUR_COVER 64
static inline unsigned som_armour_cover(const uint8_t *oam,const uint8_t *vram,unsigned who,int cx,int cy,
                                        uint8_t cover[SOM_ARMOUR_COVER][SOM_ARMOUR_COVER]){
    memset(cover,0,SOM_ARMOUR_COVER*SOM_ARMOUR_COVER);
    unsigned front=128u,n=0;
    for(unsigned j=0;j<128u&&front==128u;j++)if(som_armour_piece(oam+4u*j,who))front=j;
    for(unsigned j=0;j<front;j++){
        const uint8_t *e=oam+4u*j;
        const unsigned hb=(oam[512u+j/4u]>>(2u*(j%4u)))&3u;
        if(hb&2u||e[1]>=0xE0u)continue;            /* large pieces, or parked off screen */
        int x=e[0]+256*(int)(hb&1u);if(x>=256)x-=512;
        const int y=e[1];
        if(x+16<=cx||x>=cx+SOM_ARMOUR_COVER||y+16<=cy||y>=cy+SOM_ARMOUR_COVER)continue;
        const unsigned tile=e[2]|((unsigned)(e[3]&1u)<<8);
        for(unsigned py=0;py<16u;py++)for(unsigned px=0;px<16u;px++){
            const int X=x+(int)px-cx,Y=y+(int)py-cy;
            if(X<0||Y<0||X>=SOM_ARMOUR_COVER||Y>=SOM_ARMOUR_COVER)continue;
            const unsigned u=(e[3]&64u)?15u-px:px,v=(e[3]&128u)?15u-py:py;
            const unsigned tt=(tile&256u)|((tile+(u>>3)+16u*(v>>3))&255u);
            if(som_armour_tile_pixel(vram,tt,u&7u,v&7u)){cover[Y][X]=1;n++;}
        }
    }
    return n;
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
/* Shoulder plates (sode): the boy's arms (indices 5, 6, 7) in the first
 * SOM_ARMOUR_SODE_ROWS rows of the torso band take the armour colour too, so
 * the cuirass reads as one piece and not as patches between the arms
 * (player, 2026-11-26: "red shirt patches"). */
#define SOM_ARMOUR_SODE_ROWS 4
static const uint16_t som_armour_sode_mask[3]={(1u<<5)|(1u<<6)|(1u<<7),0,0};
static inline unsigned som_armour_sode(const uint8_t img[SOM_ARMOUR_BOX][SOM_ARMOUR_BOX],unsigned who,int from,
                                       uint8_t mask[SOM_ARMOUR_BOX][SOM_ARMOUR_BOX]){
    if(who>2u||!som_armour_sode_mask[who])return 0;
    unsigned n=0;
    for(int y=from;y<from+SOM_ARMOUR_SODE_ROWS&&y<SOM_ARMOUR_BOX;y++)
        for(int x=0;x<SOM_ARMOUR_BOX;x++)
            if(img[y][x]&&(som_armour_sode_mask[who]>>img[y][x]&1u)&&!mask[y][x]){mask[y][x]=2;n++;}
    return n;
}
/* Headgear (player, 2026-11-26: "might look better with a kabuto", then
 * "do more armors"): drawn art per class, front / back / side (left = the
 * side mirrored), 20x14 (player, 2026-11-27: "too small"), placed on the head the game draws this frame: row
 * SOM_HEAD_TOP sits on the top row of the head, centred on its top rows.
 * Letters: K outline, A main, a highlight, D shade, G accent, W trim,
 * R / r second colour (the kabuto's crimson); '.' see-through. */
#define SOM_HEAD_W 20
#define SOM_HEAD_H 14
#define SOM_HEAD_TOP 3      /* 3 rows lower than drawn: the brim sits on the bandana (player, 2026-11-26) */
enum { SOM_HEAD_FRONT, SOM_HEAD_BACK, SOM_HEAD_SIDE };
enum { SOM_HEAD_KABUTO, SOM_HEAD_NINJA, SOM_HEAD_HUNTER, SOM_HEAD_MAGE, SOM_HEAD_ARCHER, SOM_HEAD_MONK, SOM_HEAD_NECRO, SOM_HEADS };
static const char *const som_head_art[SOM_HEADS][3][SOM_HEAD_H]={
    {
     {"....................","....................","....................","G..................G","GG................GG",".GG...KKKKKKKK...GG.","..GGKKaaAAaaAAKKGG..","...KaaAAAAAAAAAAK...","..KaAAAAAAAAAAAAAK..",".KAAAAGGGGGGGGAAAAK.","KRRKKKKKKKKKKKKKKRRK","KRr..............rRK","KRr..............rRK",".Kr..............rK."},
     {"....................","....................","....................","....................","....................","......KKKKKKKK......","....KKaaAAaaAAKK....","...KaaAAAAAAAAAAK...","..KaAAAAAAAAAAAAAK..",".KAAAAAAAAAAAAAAAAK.","KRRRRRRRRRRRRRRRRRRK","KrRGRRGRRGGRRGRRGRrK","KrrRRRRRRRRRRRRRRrrK",".KKrrrrrrrrrrrrrrKK."},
     {"....................","....................","....................","...................G","..................GG","......KKKKKKKK...GG.","....KKaaAAaaAAKKGG..","...KaaAAAAAAAAAAK...","..KaAAAAAAAAAAAAAK..",".KAAAAAAAAAAAAGGGAK.","KRRRRRKKKKKKKKKKKKK.","KrRRRRK...K.........","KrrRRK..............",".KKrK..............."}},  /* kabuto */
    {
     {"....................","....................","....................","....................","....................","....................","....................","....KKKKKKKKKKKK....","KAAAKGaaaaaaaaGKAAAK","KDDDKGGWWGGWWGGKDDDK","....KKKKKKKKKKKK....","....................","....................","...................."},
     {"....................","....................","....................","....................","....................","....................","....................","....................","KAAAAAAAAAAAAAAAAAAK","KDDDDDDKKDDKKDDDDDDK",".......KAA.KAAK.....","......KAKKKAKKAK....",".....KAK..AK..KAK...",".....KK...K....KK..."},
     {"....................","....................","....................","....................","....................","....................","....................","...............KKKK.","KK.KAAAAAAAAAAAKGaaK","KAKKDDDDDDDDDDDKGGGK",".KAAK..........KKKK.","..KAAK..............","...KK...............","...................."}},  /* ninja */
    {
     {"....................","....................","....................","....KKKKKKKKKKKK....","...KaaAAAAAAAAAAK...","...KaAAAAAAAAAAAK...","...KGGGGGGGGGGGGK...",".KKKKKKKKKKKKKKKKKK.","KaaAAAAAAAAAAAAAAADK",".KKKKKKKKKKKKKKKKKK.","....................","....................","....................","...................."},
     {"....................","....................","....................","....KKKKKKKKKKKK....","...KaaAAAAAAAAAAK...","...KaAAAAAAAAAAAK...","...KGGGGGGGGGGGGK...",".KKKKKKKKKKKKKKKKKK.","KaaAAAAAAAAAAAAAAADK",".KKKKKKKKKKKKKKKKKK.","....................","....................","....................","...................."},
     {"....................","....................","....................",".....KKKKKKKKKKK....","....KaaAAAaAAAAAK...","....KaAAAAAAAAAAK...","....KGGGGGGGGGGGK...","KKKKKKKKKKKKKKKKKKKK","KaaAAAAAAAAAAAAAAADK",".KKKKKKKKKKKKKKKKKK.","....................","....................","....................","...................."}},  /* hunter */
    {
     {"........KK..KK......",".......KaA.KaAK.....","......KaAAKaAAK.....","......KaAAKaAADK....",".....KaAAAaAAAADK...",".....KaAAAaAAAADK...","....KaAAAAAAAAAADK..","....KGGGGGGGGGGGGK..",".KKKKKKKKKKKKKKKKKK.","KaaAAAAAAAAAAAAAAADK",".KKKKKKKKKKKKKKKKKK.","....................","....................","...................."},
     {"........KK..KK......",".......KaA.KaAK.....","......KaAAKaAAK.....","......KaAAKaAADK....",".....KaAAAaAAAADK...",".....KaAAAaAAAADK...","....KaAAAAAAAAAADK..","....KGGGGGGGGGGGGK..",".KKKKKKKKKKKKKKKKKK.","KaaAAAAAAAAAAAAAAADK",".KKKKKKKKKKKKKKKKKK.","....................","....................","...................."},
     {"..KK................","..KaKK..............","...KaAKK..KK........","....KaAADKAADK......",".....KaAAAaAAADK....",".....KaAAAaAAAADK...","....KaAAAAAAAAAADK..","....KGGGGGGGGGGGGK..","KKKKKKKKKKKKKKKKKKKK","KaaAAAAAAAAAAAAAAADK",".KKKKKKKKKKKKKKKKKK.","....................","....................","...................."}},  /* mage */
    {
     {"....................","....................","....................","................G...","...............GG...","....KKKKKKKKKKGGK...","...KaaAAAAAAAAGAAK..","..KaAAAAAAAAAAAAADK.",".KKKKKKKKKKKKKKKKKK.","....................","....................","....................","....................","...................."},
     {"....................","....................","....................","................G...","...............GG...","....KKKKKKKKKKGGK...","...KaaAAAAAAAAGAAK..","..KaAAAAAAAAAAAAADK.",".KKKKKKKKKKKKKKKKKK.","....................","....................","....................","....................","...................."},
     {"....................","....................","....................",".G..................",".GG.................","..GGKKKKKKKKKK......","...KaaAAAAAAAAAK....","..KaAAAAAAAAAAAAKK..",".KKKKKKKKKKKKKKKAAKK","...............KKK..","....................","....................","....................","...................."}},  /* archer */
    {
     {"....................","....................","....................","....................","....................","....................","....................","....................","....................","..KAAAAAAAAAAAAAAK..","..KDDDDDDDDDDDDDDK..","....................","....................","...................."},
     {"....................","....................","....................","....................","....................","....................","....................","....................","....................","..KAAAAAAAAAAAAAAK..","..KDDDDKKDDKKDDDDK..","......KAAKKAAK......",".....KAKKAAKKAK.....",".....KK..KK..KK....."},
     {"....................","....................","....................","....................","....................","....................","....................","....................","....................","KK.KAAAAAAAAAAAAAK..","KAKKDDDDDDDDDDDDDK..",".KAAK...............","..KK................","...................."}},  /* monk */
    {
     {"....................","....................","....................","....................",".....KKKKKKKKKK.....","...KKaaAAAaAAAAKK...","..KaaAAAAAAAAAAAAK..",".KaAWWWWWWWWWWWAADK.",".KAAK..........KADK.","KAAK............KADK","KAD..............DDK","KAD..............DDK",".KD..............DK.","...................."},
     {"....................","....................","....................","....................","....................","...KKaaAAAaAAAAKK...","..KaaAAWAAAWAAAAAK..",".KaAAAAWAAAWAAAAADK.",".KAAAAAWAAAWAAAAADK.","KAAAAAAWAAAWAAAAAADK","KAAAAAAAAAAAAAAAADDK","KADDAAAAAAAAAAAADDDK",".KKDDDDDDDDDDDDDDKK.","...................."},
     {"....................","....................","....................","....................",".....KKKKKKKKKK.....","...KKaaAAAaAAAAKK...","..KaaAAAAAAAAAAAAK..",".KaAAAAAAAAAAAAWWWWK","KAAAAAAAAKAAAK......","KAAAAAAAK.AAK.......","KAAAAAAK..AK........","KDDAAAK...K.........",".KKDDK..............","...................."}},  /* necro */
};
/* Personality: rows that flutter (tails, feathers) per headgear and view
 * (front, back, side), first..last row, 0,0 none. They sway one pixel,
 * row by row, every 8 frames. */
static const uint8_t som_head_flutter[SOM_HEADS][3][2]={
    {{0,0},{0,0},{0,0}},          /* kabuto */
    {{0,0},{10,13},{10,12}},      /* hagane: knot tails */
    {{0,0},{0,0},{0,0}},          /* hunter */
    {{0,2},{0,2},{0,2}},          /* mage: the hat's tip */
    {{3,4},{3,4},{3,4}},          /* archer: the feather */
    {{0,0},{11,13},{11,12}},      /* monk: hachimaki tails */
    {{0,0},{0,0},{0,0}},          /* necro */
};
/* Colours per headgear, in the letter order "KAaDGWRr". */
static const uint8_t som_head_rgb[SOM_HEADS][8][3]={
    {{24,20,28},{44,40,58},{96,92,120},{30,28,40},{232,184,72},{232,232,220},{180,36,44},{110,20,32}},  /* kabuto */
    {{16,16,22},{58,60,78},{98,102,124},{36,36,50},{196,204,214},{0,0,0},{0,0,0},{0,0,0}},            /* ninja */
    {{24,16,10},{92,58,32},{132,92,56},{62,38,20},{170,30,36},{0,0,0},{0,0,0},{0,0,0}},               /* hunter */
    {{20,12,36},{92,52,168},{140,104,214},{60,30,112},{240,200,80},{0,0,0},{0,0,0},{0,0,0}},          /* mage */
    {{14,30,14},{52,128,52},{100,176,84},{32,84,32},{214,40,40},{0,0,0},{0,0,0},{0,0,0}},             /* archer */
    {{48,48,56},{236,236,232},{255,255,255},{168,168,176},{0,0,0},{240,240,240},{0,0,0},{0,0,0}},     /* monk: white hachimaki */
    {{8,8,10},{40,38,44},{70,68,80},{24,22,28},{0,0,0},{226,218,190},{0,0,0},{0,0,0}},                /* necro */
};
static inline const uint8_t *som_head_colour(unsigned head,char k){
    static const char letters[]="KAaDGWRr";
    if(head>=SOM_HEADS)return NULL;
    for(unsigned i=0;i<8u;i++)if(letters[i]==k)return som_head_rgb[head][i];
    return NULL;
}
/* Where the headgear goes in the composed image: its top-left (*kx, *ky). */
static inline int som_head_place(const uint8_t img[SOM_ARMOUR_BOX][SOM_ARMOUR_BOX],int *kx,int *ky){
    int top=-1;
    for(int y=0;y<SOM_ARMOUR_BOX&&top<0;y++)
        for(int x=0;x<SOM_ARMOUR_BOX;x++)if(img[y][x]){top=y;break;}
    if(top<0)return 0;
    int l=SOM_ARMOUR_BOX,r=-1;
    for(int y=top;y<top+6&&y<SOM_ARMOUR_BOX;y++)
        for(int x=0;x<SOM_ARMOUR_BOX;x++)if(img[y][x]){if(x<l)l=x;if(x>r)r=x;}
    if(r<l)return 0;
    *kx=(l+r+1)/2-SOM_HEAD_W/2;*ky=top-SOM_HEAD_TOP;
    return 1;
}
/* The headgear's letter at (u, v) for a hero facing `facing` (+0x10) at
 * frame `frame` (fluttering rows sway). */
static inline char som_head_at(unsigned head,unsigned facing,int u,int v,unsigned long frame){
    if(head>=SOM_HEADS||u<0||v<0||u>=SOM_HEAD_W||v>=SOM_HEAD_H)return '.';
    const int side=facing==0x02u||facing==0x82u;
    const unsigned art=side?SOM_HEAD_SIDE:facing==0x00u?SOM_HEAD_BACK:SOM_HEAD_FRONT;
    if(facing==0x82u)u=SOM_HEAD_W-1-u;
    const uint8_t *f=som_head_flutter[head][art];
    if(f[1]&&v>=f[0]&&v<=f[1]&&(((frame>>3)+(unsigned long)v)&1ul)){
        u+=side?1:(v&1)?1:-1;                 /* the tail streams back, or sways */
        if(u<0||u>=SOM_HEAD_W)return '.';
    }
    return som_head_art[head][art][v][u];
}
/* A glint runs across the gold and steel every 2 s. */
static inline int som_head_glint(int u,unsigned long frame){
    const unsigned long t=frame%120ul;
    return t<(unsigned long)SOM_HEAD_W*2ul&&(unsigned long)u==t/2ul;
}
/* The class's look: headgear (or -1) and whether the Samurai torso and sode
 * are painted. Classes 0..15 (som_disciplines.h). */
typedef struct { int head; uint8_t torso; } SomClassLook;
static inline SomClassLook som_class_look(unsigned cls){
    SomClassLook l={-1,0};
    switch(cls){
    case 10: l.head=SOM_HEAD_KABUTO;l.torso=1;break;   /* Bushido */
    case 2:  l.head=SOM_HEAD_NINJA;break;              /* Ninjutsu */
    case 12: l.head=SOM_HEAD_HUNTER;break;             /* Vampire Hunting */
    case 3:  l.head=SOM_HEAD_MAGE;break;               /* Magic */
    case 11: l.head=SOM_HEAD_ARCHER;break;             /* Archery */
    case 1:  l.head=SOM_HEAD_MONK;break;               /* Martial Arts */
    case 13: l.head=SOM_HEAD_NECRO;break;              /* Necromancy */
    default: break;
    }
    return l;
}
#endif
