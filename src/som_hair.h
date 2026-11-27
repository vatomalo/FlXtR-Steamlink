#ifndef FLXTR_SOM_HAIR_H
#define FLXTR_SOM_HAIR_H
/* Hair colour for the character creator (player, 2026-11-27): palette work
 * on the hero's own hair entries 2, 3, 4 (dark, mid, light; the sprite's
 * hair also uses 8), so it follows every pose the game draws. The style is
 * a head from the game itself (som_heads.h); drawn caps were dropped (player:
 * "they were the worst"). Same for the boy, the girl and the sprite. */
#include <stdint.h>

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
/* The hero's palette with hair ramp rgb[3] (dark, mid, light). */
static inline void som_hair_tint_rgb(uint8_t palette[32],unsigned who,const uint8_t *const rgb[3]){
    if(!palette)return;
    for(unsigned i=0;i<3u;i++){
        const uint16_t v=som_hair_bgr555(rgb[i]);
        palette[2u*(2u+i)]=(uint8_t)v;palette[2u*(2u+i)+1u]=(uint8_t)(v>>8);
    }
    if(who==2u){
        const uint16_t v=som_hair_bgr555(rgb[1]);
        palette[16]=(uint8_t)v;palette[17]=(uint8_t)(v>>8);
    }
}
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

#endif
