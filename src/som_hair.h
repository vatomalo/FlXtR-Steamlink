#ifndef FLXTR_SOM_HAIR_H
#define FLXTR_SOM_HAIR_H
/* Hair for the character creator (player, 2026-11-27: "Hair styles").
 *
 * Colour is palette work on the hero's own hair entries 2, 3, 4 (dark, mid,
 * light; the sprite's hair also uses 8), so it follows every pose the game
 * draws. Style is drawn art per facing, placed on the head the game draws
 * this frame like the class headgear (som_head_place in som_armour.h): it
 * adds volume over the native hair, never cuts it away (cutting hair out
 * left holes and smears, see the Monk's bald head). Art colours come from
 * the hero's live hair palette, so a style always matches the hair under it.
 * Pure; the proxy draws. Same for the boy, the girl and the sprite. */
#include <stdint.h>

enum { SOM_HAIR_NATIVE, SOM_HAIR_SPIKY, SOM_HAIR_LONG, SOM_HAIR_PONYTAIL, SOM_HAIR_TOPKNOT, SOM_HAIR_STYLES };
static const char *const som_hair_style_names[SOM_HAIR_STYLES]={"NATIVE","SPIKY","LONG","PONYTAIL","TOPKNOT"};

#define SOM_HAIR_COLOURS 10u
static const char *const som_hair_colour_names[SOM_HAIR_COLOURS]={
    "NATIVE","BLACK","BROWN","AUBURN","BLONDE","SILVER","WHITE","BLUE","GREEN","PINK"};
/* dark, mid, light (8-bit RGB) */
static const uint8_t som_hair_ramps[SOM_HAIR_COLOURS][3][3]={
    {{0,0,0},{0,0,0},{0,0,0}},
    {{24,22,28},{50,48,60},{88,86,104}},
    {{60,34,18},{100,62,34},{146,96,56}},
    {{100,28,18},{156,54,30},{206,94,50}},
    {{150,108,40},{214,170,72},{248,226,144}},
    {{108,110,124},{168,170,184},{228,230,240}},
    {{170,170,180},{218,218,226},{252,252,252}},
    {{28,40,112},{54,82,172},{104,144,232}},
    {{24,80,42},{50,132,66},{112,192,112}},
    {{150,50,100},{212,102,152},{250,172,204}},
};
static inline uint16_t som_hair_bgr555(const uint8_t rgb[3]){
    return (uint16_t)((rgb[0]>>3)|((rgb[1]>>3)<<5)|((rgb[2]>>3)<<10));
}
/* The hero's palette (16 BGR555 entries, little endian) with colour c. */
static inline void som_hair_tint(uint8_t palette[32],unsigned who,unsigned c){
    if(!c||c>=SOM_HAIR_COLOURS||!palette)return;
    for(unsigned i=0;i<3u;i++){
        const uint16_t v=som_hair_bgr555(som_hair_ramps[c][i]);
        palette[2u*(2u+i)]=(uint8_t)v;palette[2u*(2u+i)+1u]=(uint8_t)(v>>8);
    }
    if(who==2u){                                   /* the sprite's hair puffs */
        const uint16_t v=som_hair_bgr555(som_hair_ramps[c][1]);
        palette[16]=(uint8_t)v;palette[17]=(uint8_t)(v>>8);
    }
}

/* Art: 20 x 22, row SOM_HAIR_TOP on the head's top row, centred on it.
 * Every style is a full cap over the hero's own hair (the boy's hair is 15
 * px wide and 11 rows deep, measured 2026-11-27: bandana rows 9-10, eyes
 * row 12), so the game's hair does not show around it; the style's shape
 * (spikes, curtains, tail, bun) is drawn with it.
 * K outline, D dark, A mid, a light (the hero's hair entries 1, 2, 3, 4),
 * G the tie (a red cord); '.' see-through. Side art faces right; left is
 * mirrored. Front is facing down (+0x10 == 1), back facing up (0). */
#define SOM_HAIR_W 20
#define SOM_HAIR_H 22
#define SOM_HAIR_TOP 3
enum { SOM_HAIR_FRONT, SOM_HAIR_BACK, SOM_HAIR_SIDE };
static const char *const som_hair_art[SOM_HAIR_STYLES][3][SOM_HAIR_H]={
    {{0},{0},{0}},
    {   /* spiky: points over the crown, jagged bangs, swept back from the side */
     {"....K....KK....K","...KaK..KaaK..KaK","..KAaAKKAaaAKKAaAK","..KAAAAAAAAAAAAAAK","..KAAAAAAaaAAAAAAK","..KaAAAAAAAAAAAAAK","..KaAAAAAAAAAAAAAK","..KAAAAAAAaAAAAAAK","..KAAAAAAAAAAAAAAK","..KAADAAAAAAAADAAK","..KADKDAAKKAADKDAK","..KDK.KDK..KDK.KDK","..KK............KK","","","","","","","","",""},
     {"....K....KK....K","...KaK..KaaK..KaK","..KAaAKKAaaAKKAaAK","..KAAAAAAAAAAAAAAK","..KAAAAAAaaAAAAAAK","..KaAAAAAAAAAAAAAK","..KaAAAAAAAAAAAAAK","..KAAAAAAAaAAAAAAK","..KAAAAAAAAAAAAAAK","..KAAAAAAAAAAAAAAK","..KAAAAAAAAAAAAAAK","..KDAAAAAAAAAAAADK","...KDAAAAAAAAAADK","....KDDDAAAADDDK",".....KKKKKKKKKK","","","","","","",""},
     {"..K.....K",".KaK...KaK...K",".KAaKKKAaAKKKaK",".KAAAAAAAAAAAAAK",".KAAAAAAaaAAAAAAK",".KaAAAAAAAAAAAAAK",".KaAAAAAAAAAAAAAAK",".KAAAAAAAaAAAAAAAAK",".KAAAAAAAAAAAAAAAAK",".KAAAAAAAAAAAADAADK",".KAAAAAAAAADKDKDK",".KDAAAAAAAK","..KDAAAAAK","...KDDAAK","....KKKK","","","","","","",""}},
    {   /* long: a full cap, curtains to the chest, down the back */
     {"","","","......KKKKKKKK","....KKaaAAAAaaKK","...KaAAAAAAAAAAaK","..KaAAAAAAAAAAAAAK","..KAAAAAAAaAAAAAAK","..KAAAAAAAAAAAAAAK","..KAAAAAAAAAAAAAAK",".KADKDDDDDDDDDDKDAK",".KaDK..........KDaK",".KADK..........KDAK",".KADK..........KDAK",".KaDK..........KDaK",".KADK..........KDAK",".KADK..........KDAK",".KaDK..........KDaK",".KADK..........KDAK",".KADK..........KDAK",".KK..............KK",""},
     {"","","","......KKKKKKKK","....KKaaAAAAaaKK","...KaAAAAAAAAAAaK","..KaAAAAAAAAAAAAAK","..KAAAAAAAaAAAAAAK","..KAAAAAAAAAAAAAAK","..KAAAAAAAAAAAAAAK",".KAAADAAAAAAAAADAAAK",".KAAADAAAAaAAAADAAAK",".KAAADAAAAAAAAADAAAK",".KAAADAAAAaAAAADAAAK",".KAAADAAAAAAAAADAAAK",".KAAADAAAAaAAAADAAAK",".KAAADAAAAAAAAADAAAK",".KAAADAAAAaAAAADAAAK",".KAAADAAAAAAAAADAAAK","..KAADAAAAAAAADAAK","...KKDDAAAAAADDKK",".....KKKKKKKKKK"},
     {"","","",".....KKKKKKKKK","...KKaaAAAAAaaKK","..KaAAAAAAAAAAAaK",".KaAAAAAAAAAAAAAAK",".KAAAAAAAaAAAAAAAAK",".KAAAAAAAAAAAAAAAAK",".KAAAAAAAAAAAADAADK",".KAAAAAAAAADDDDK",".KAADAAKAAK",".KAADaAKAK",".KAADAAKK",".KAADaAK",".KAADAAK",".KAADaAK",".KAADAAK",".KAADaAK",".KADAK","..KDK","...K"}},
    {   /* ponytail: a smooth cap, a red cord and a tail down the back */
     {"","","","......KKKKKKKK","....KKaaAAAAaaKK","...KaAAAAAAAAAAaK","..KaAAAAAAAAAAAAAK","..KAAAAAAAaAAAAAAK","..KAAAAAAAAAAAAAAK","..KAAAAAAAAAAAAAAK","..KADDDDDDDDDDDDAK","..KDK..........KDK","..KK............KK","","","","","","","","",""},
     {"","","","......KKKKKKKK","....KKaaAAAAaaKK","...KaAAAAAAAAAAaK","..KaAAAAAAAAAAAAAK","..KAAAAAAAaAAAAAAK","..KAAAAAAAAAAAAAAK","..KAAAAAAAAAAAAAAK","..KAAAAAAAAAAAAAAK","..KDAAAAAAAAAAAADK","...KDAAAAAAAAAADK","....KDDDAAAADDDK",".....KKKKGGKKKK",".......KAaAAK",".......KAAAAK",".......KAaAAK","........KAAK","........KAaK","........KAAK",".........KK"},
     {"","","",".....KKKKKKKKK","...KKaaAAAAAaaKK","..KaAAAAAAAAAAAaK",".KaAAAAAAAAAAAAAAK",".KAAAAAAAaAAAAAAAAK",".KAAAAAAAAAAAAAAAAK",".KAAAAAAAAAAAADAADK",".KGGKAAAAAADDDDK","KAAKAAAAAAK","KAaKAAAAAK","KAAKDDAAK",".KAAKKKK",".KAaK","..KAK","...KK","","","",""}},
    {   /* topknot: a smooth cap and a bun tied on the crown */
     {"........KKKK",".......KaaAAK",".......KAAAAK","......KGGGGGGK","....KKaaAAAAaaKK","...KaAAAAAAAAAAaK","..KaAAAAAAAAAAAAAK","..KAAAAAAAaAAAAAAK","..KAAAAAAAAAAAAAAK","..KAAAAAAAAAAAAAAK","..KADDDDDDDDDDDDAK","..KDK..........KDK","..KK............KK","","","","","","","","",""},
     {"........KKKK",".......KaaAAK",".......KAAAAK","......KGGGGGGK","....KKaaAAAAaaKK","...KaAAAAAAAAAAaK","..KaAAAAAAAAAAAAAK","..KAAAAAAAaAAAAAAK","..KAAAAAAAAAAAAAAK","..KAAAAAAAAAAAAAAK","..KAAAAAAAAAAAAAAK","..KDAAAAAAAAAAAADK","...KDAAAAAAAAAADK","....KDDDAAAADDDK",".....KKKKKKKKKK","","","","","","",""},
     {"......KKKK",".....KaaAAK",".....KAAAAK","....KGGGGGGKKK","...KKaaAAAAAaaKK","..KaAAAAAAAAAAAaK",".KaAAAAAAAAAAAAAAK",".KAAAAAAAaAAAAAAAAK",".KAAAAAAAAAAAAAAAAK",".KAAAAAAAAAAAADAADK",".KAAAAAAAAADDDDK",".KDAAAAAAAK","..KDAAAAAK","...KDDAAK","....KKKK","","","","","","",""}},

};
/* Rows that stream in the wind (tails), per style and view, first..last;
 * 0,0 none. They sway one pixel, row by row, every 8 frames. */
static const uint8_t som_hair_flutter[SOM_HAIR_STYLES][3][2]={
    {{0,0},{0,0},{0,0}},
    {{0,0},{0,0},{0,0}},
    {{0,0},{17,21},{14,21}},
    {{0,0},{16,21},{12,17}},
    {{0,0},{0,0},{0,0}},
};
/* The art's letter at (u, v) for a hero facing `facing` at `frame`. */
static inline char som_hair_at(unsigned style,unsigned facing,int u,int v,unsigned long frame){
    if(style==SOM_HAIR_NATIVE||style>=SOM_HAIR_STYLES||u<0||v<0||u>=SOM_HAIR_W||v>=SOM_HAIR_H)return '.';
    const int side=facing==0x02u||facing==0x82u;
    const unsigned art=side?SOM_HAIR_SIDE:facing==0x00u?SOM_HAIR_BACK:SOM_HAIR_FRONT;
    if(facing==0x82u)u=SOM_HAIR_W-1-u;
    const uint8_t *f=som_hair_flutter[style][art];
    if(f[1]&&v>=f[0]&&v<=f[1]&&(((frame>>3)+(unsigned long)v)&1ul)){
        u+=side?1:(v&1)?1:-1;
        if(u<0||u>=SOM_HAIR_W)return '.';
    }
    const char *row=som_hair_art[style][art][v];
    for(int i=0;i<u;i++)if(!row[i])return '.';
    return row[u]?row[u]:'.';
}
/* The palette entry a letter draws with (0: a fixed colour, see G). */
static inline unsigned som_hair_entry(char k){
    return k=='K'?1u:k=='D'?2u:k=='A'?3u:k=='a'?4u:0u;
}
/* With strands: the mid shade A gets dark strands and light glints in a
 * fixed diagonal pattern (the game's own hair is all strands), so a cap does
 * not read as one flat block (dots read as gravel: lines it is). u, v: art
 * coordinates. */
static inline unsigned som_hair_entry_at(char k,int u,int v){
    if(k!='A')return som_hair_entry(k);
    if(((u+(v>>2))&3)==0)return 2u;                  /* strands: slanted lines */
    if(v>=4&&v<=7&&((u+v)%3)==0)return 4u;           /* the glint band on the crown */
    return 3u;
}
#endif
