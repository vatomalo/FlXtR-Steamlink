/* SoM visual identity palettes. Pure BGR555 transforms, not WRAM hooks.
 * Index 0 is transparent and must remain unchanged. Palette previews and
 * familiar sprites can use these ramps without re-writing character data.
 */
#ifndef FLXTR_SOM_CLASS_PALETTE_H
#define FLXTR_SOM_CLASS_PALETTE_H
#include <stdint.h>

enum SomPaletteProfile {
    SOM_PAL_ORIGINAL = 0,
    SOM_PAL_SWORDMASTER_FROST,
    SOM_PAL_MONK_CRIMSON_GOLD,
    SOM_PAL_NINJA_MIDNIGHT,
    SOM_PAL_SUMMONER_WHITE,
    SOM_PAL_CELESTIAL_FAMILIAR,
    SOM_PAL_ROGUE_SHADOW,
    SOM_PAL_SAMURAI_INDIGO,
    SOM_PAL_ARCHER_FOREST,
    SOM_PAL_MAGE_ARCANE,
    SOM_PAL_DIVINE_GOLD,
    SOM_PAL_DARK_CRIMSON,
    SOM_PAL_DANCER_ROSE,
    SOM_PAL_FLOW_TEAL,
    SOM_PAL_HUNTER_LEATHER,
    SOM_PAL_NECRO_BONE,
    SOM_PAL_ARTIFICER_BRASS,
    SOM_PAL_PSION_VIOLET,
    SOM_PAL_PROFILE_COUNT
};

static inline uint16_t som_pal_rgb(unsigned r,unsigned g,unsigned b) {
    return (uint16_t)((r&31u)|((g&31u)<<5)|((b&31u)<<10));
}
static inline uint16_t som_pal_color(uint16_t source, unsigned profile) {
    if(profile==SOM_PAL_ORIGINAL || profile>=SOM_PAL_PROFILE_COUNT)return source;
    const unsigned r=source&31u,g=(source>>5)&31u,b=(source>>10)&31u;
    /* Preserve native shading by mapping source luminance to an intentional
     * discipline-specific color ramp. Avoid using the source hue: white
     * Summoners stay white regardless of their original red/blue costume. */
    const unsigned lum=(r*30u+g*59u+b*11u)/100u;
    unsigned nr=0,ng=0,nb=0;
    switch(profile){
    case SOM_PAL_SWORDMASTER_FROST:
        nr=lum*16u/31u;ng=lum*27u/31u;nb=lum;
        break;
    case SOM_PAL_MONK_CRIMSON_GOLD:
        if(lum>18u){nr=lum;ng=(lum-18u)*28u/13u;nb=lum/10u;}
        else {nr=lum;ng=lum/6u;nb=lum/9u;}
        break;
    case SOM_PAL_NINJA_MIDNIGHT:
        nr=lum*5u/31u;ng=lum*9u/31u;nb=5u+lum*18u/31u;
        break;
    case SOM_PAL_SUMMONER_WHITE:
        nr=lum;ng=lum;nb=lum;
        break;
    case SOM_PAL_CELESTIAL_FAMILIAR:
        nr=lum*22u/31u;ng=lum*28u/31u;nb=lum;
        break;
    case SOM_PAL_ROGUE_SHADOW:          /* dark plum and charcoal */
        nr=lum*20u/31u;ng=lum*13u/31u;nb=lum*22u/31u;
        break;
    case SOM_PAL_SAMURAI_INDIGO:        /* indigo with steel highlights */
        if(lum>20u){nr=lum*24u/31u;ng=lum*26u/31u;nb=lum;}
        else {nr=lum*6u/31u;ng=lum*8u/31u;nb=4u+lum*20u/31u;}
        break;
    case SOM_PAL_ARCHER_FOREST:         /* forest green and leaf */
        nr=lum*9u/31u;ng=lum;nb=lum*10u/31u;
        break;
    case SOM_PAL_MAGE_ARCANE:           /* arcane violet */
        nr=lum*21u/31u;ng=lum*10u/31u;nb=lum;
        break;
    case SOM_PAL_DIVINE_GOLD:           /* white robes, gold trim */
        if(lum>20u){nr=lum;ng=lum;nb=lum*27u/31u;}
        else {nr=lum;ng=lum*24u/31u;nb=lum*8u/31u;}
        break;
    case SOM_PAL_DARK_CRIMSON:          /* black with blood red */
        if(lum>18u){nr=lum*26u/31u;ng=lum*4u/31u;nb=lum*6u/31u;}
        else {nr=lum*8u/31u;ng=lum*5u/31u;nb=lum*7u/31u;}
        break;
    case SOM_PAL_DANCER_ROSE:           /* rose and blush */
        nr=lum;ng=lum*14u/31u;nb=lum*21u/31u;
        break;
    case SOM_PAL_HUNTER_LEATHER:        /* dark leather and crimson */
        if(lum>20u){nr=lum*27u/31u;ng=lum*12u/31u;nb=lum*10u/31u;}
        else {nr=lum*17u/31u;ng=lum*11u/31u;nb=lum*7u/31u;}
        break;
    case SOM_PAL_NECRO_BONE:            /* grave grey and black (player, 2026-11-27) */
        if(lum>20u){nr=lum*19u/31u;ng=lum*19u/31u;nb=lum*22u/31u;}
        else {nr=lum*6u/31u;ng=lum*6u/31u;nb=lum*8u/31u;}
        break;
    case SOM_PAL_ARTIFICER_BRASS:       /* brass and copper */
        if(lum>20u){nr=lum;ng=lum*24u/31u;nb=lum*9u/31u;}
        else {nr=lum*25u/31u;ng=lum*14u/31u;nb=lum*7u/31u;}
        break;
    case SOM_PAL_PSION_VIOLET:          /* pale violet and cyan */
        if(lum>20u){nr=lum*20u/31u;ng=lum*27u/31u;nb=lum;}
        else {nr=lum*19u/31u;ng=lum*9u/31u;nb=lum*27u/31u;}
        break;
    case SOM_PAL_FLOW_TEAL:             /* teal with silver */
        if(lum>22u){nr=lum*25u/31u;ng=lum*29u/31u;nb=lum*29u/31u;}
        else {nr=lum*4u/31u;ng=lum*22u/31u;nb=lum*21u/31u;}
        break;
    default: return source;
    }
    return som_pal_rgb(nr,ng,nb);
}
static inline void som_pal_apply(uint8_t *palette,unsigned profile){
    if(!palette||profile==SOM_PAL_ORIGINAL||profile>=SOM_PAL_PROFILE_COUNT)return;
    for(unsigned i=1;i<16u;i++){
        uint16_t c=(uint16_t)(palette[2u*i]|(unsigned)palette[2u*i+1u]<<8);
        uint16_t out=som_pal_color(c,profile);
        palette[2u*i]=(uint8_t)out;
        palette[2u*i+1u]=(uint8_t)(out>>8);
    }
}
/* The Necromancer's body (player, 2026-11-27: "the palest skin, blackhair
 * lots of palette hacks towards grey and black"): hair 2-4 (darkest to
 * lightest) near black with a cold sheen, skin (boy 5-7, girl and sprite
 * 6-7, 7 the lightest) bone pale; the sprite's hair 8 too. who: 0 boy,
 * 1 girl, 2 sprite. The clothes take the grey ramp above. */
static inline void som_pal_necro_body(uint8_t *palette,unsigned who){
    static const uint16_t hair[3]={(3u<<10)|(2u<<5)|2u,(7u<<10)|(5u<<5)|5u,(12u<<10)|(9u<<5)|9u};
    static const uint16_t skin[3]={(23u<<10)|(19u<<5)|19u,(27u<<10)|(25u<<5)|25u,(31u<<10)|(30u<<5)|30u};
    if(!palette||who>2u)return;
    uint16_t set[16];unsigned have=0;
    for(unsigned i=0;i<16u;i++)set[i]=0xFFFFu;
    for(unsigned i=0;i<3u;i++)set[2u+i]=hair[i];
    if(who==0u)set[5]=skin[0];
    set[6]=skin[1];set[7]=skin[2];
    if(who==2u)set[8]=hair[1];
    for(unsigned i=1;i<16u;i++){
        if(set[i]==0xFFFFu)continue;
        palette[2u*i]=(uint8_t)set[i];palette[2u*i+1u]=(uint8_t)(set[i]>>8);have++;
    }
    (void)have;
}
#endif
