#ifndef FLXTR_SOM_REAPER_H
#define FLXTR_SOM_REAPER_H
/* The Grim Reaper as the Necromancer's ultimate (player, 2026-11-28: "maybe
 * necromancer can summon the grim reaper as its ultimate attack, as we do
 * have a grim reaper when you die"; "high level attack when necro is really
 * high level"; "Reaps the weak").
 *
 * The game's own Reaper, the one over a fallen hero (measured on the Turbo
 * ROM 2026-11-28 by dumping a death headless): 64 tiles at D4:2988, 24 bytes
 * each in Turbo's effect encoding, expanded with the girl's converter into a
 * 16-tile-wide block (video tiles 0x168-0x1A7, so a piece's four tiles are
 * t, t+1, t+16, t+17 of the block); colours: effect palette 126 (D1:FAEC,
 * five colours, repeated over entries 1-15). It hovers in two layouts of 12
 * pieces, 10 frames each; offsets are from the fallen hero's position
 * (object x, y on screen). Nothing of the art is stored here: the proxy reads
 * it from the player's ROM. Pure. */
#include <stdint.h>
#include <string.h>

#define SOM_REAPER_TILES_AT 0x142988u        /* file offset of D4:2988 */
#define SOM_REAPER_PALETTE_AT 0x11FAECu      /* D1:FAEC */
#define SOM_REAPER_TILE_COUNT 64u
#define SOM_REAPER_PIECES 12u
#define SOM_REAPER_BOB 10u                   /* frames per layout */
typedef struct { int8_t x, y; uint8_t tile; } SomReaperPiece;
/* front first (the game's OAM order) */
static const SomReaperPiece som_reaper_layout[2][SOM_REAPER_PIECES]={
    {{3,-56,0},{-13,-51,2},{-20,-50,4},{-4,-50,6},{-20,-34,24},{-4,-34,26},
     {0,-50,36},{-16,-45,38},{-23,-44,56},{-7,-44,58},{-23,-28,60},{-7,-28,62}},
    {{3,-58,0},{-13,-53,2},{-20,-49,4},{-4,-49,6},{-21,-33,24},{-5,-33,26},
     {0,-52,36},{-16,-47,38},{-23,-43,56},{-7,-43,58},{-24,-27,60},{-8,-27,62}}};

typedef struct {
    int ready;
    uint8_t px[SOM_REAPER_TILE_COUNT][64];   /* colour indices 0-15 */
    uint8_t rgb[16][3];
} SomReaperArt;

/* From the ROM (file bytes, no header). 1 when it looked right. */
static inline int som_reaper_load(SomReaperArt *a,const uint8_t *rom,size_t size){
    memset(a,0,sizeof(*a));
    if(!rom||size<SOM_REAPER_TILES_AT+SOM_REAPER_TILE_COUNT*24u||size<SOM_REAPER_PALETTE_AT+10u)return 0;
    for(unsigned t=0;t<SOM_REAPER_TILE_COUNT;t++){
        const uint8_t *s=rom+SOM_REAPER_TILES_AT+24u*t;
        for(unsigned y=0;y<8u;y++){
            const uint8_t A=s[2u*y],B=s[2u*y+1u],C=s[16u+y],ab=(uint8_t)(A^B);
            const uint8_t p0=(uint8_t)~A,p1=ab,p2=(uint8_t)(~C&ab),p3=(uint8_t)((A&B)^C);  /* the girl's converter */
            for(unsigned x=0;x<8u;x++){
                const unsigned bit=7u-x;
                a->px[t][y*8u+x]=(uint8_t)(((p0>>bit)&1u)|(((p1>>bit)&1u)<<1)|(((p2>>bit)&1u)<<2)|(((p3>>bit)&1u)<<3));
            }
        }
    }
    for(unsigned i=1;i<16u;i++){
        const uint8_t *c=rom+SOM_REAPER_PALETTE_AT+2u*((i-1u)%5u);
        const unsigned v=c[0]|(unsigned)c[1]<<8;
        a->rgb[i][0]=(uint8_t)((v&31u)<<3);a->rgb[i][1]=(uint8_t)(((v>>5)&31u)<<3);a->rgb[i][2]=(uint8_t)(((v>>10)&31u)<<3);
    }
    a->ready=1;
    return 1;
}
/* The colour index at screen offset (dx, dy) from the anchor in layout L
 * (front piece wins). 0: none. */
static inline uint8_t som_reaper_at(const SomReaperArt *a,unsigned L,int dx,int dy){
    if(!a->ready||L>1u)return 0;
    for(unsigned k=0;k<SOM_REAPER_PIECES;k++){
        const SomReaperPiece *p=&som_reaper_layout[L][k];
        const int u=dx-p->x,v=dy-p->y;
        if(u<0||v<0||u>=16||v>=16)continue;
        const unsigned t=p->tile+(unsigned)(u>>3)+16u*(unsigned)(v>>3);
        if(t>=SOM_REAPER_TILE_COUNT)continue;
        const uint8_t c=a->px[t][(v&7)*8+(u&7)];
        if(c)return c;
    }
    return 0;
}
/* Reaps the weak: a monster at or under a quarter of its HP falls (not a
 * boss: kinds 0x57 and up); the others take `damage`. Returns the HP to take
 * (the caller caps it to what one hit can carry). */
static inline unsigned som_reaper_take(unsigned hp,unsigned max_hp,unsigned kind,unsigned damage){
    if(!hp)return 0;
    if(kind<0x57u&&max_hp&&hp*4u<=max_hp)return hp;
    return damage<hp?damage:hp;
}
#endif
