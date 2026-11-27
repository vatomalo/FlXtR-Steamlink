#ifndef FLXTR_SOM_STARTER_H
#define FLXTR_SOM_STARTER_H
#include <stdint.h>
#include <string.h>
#include "som_starter_pixels.h"

enum { SOM_SKIN_COUNT=4 };
static const char *const som_skin_names[SOM_SKIN_COUNT]={"IVORY","GOLDEN","COPPER","UMBER"};
#define SOM_RGB5(r,g,b) ((r)|((g)<<5)|((b)<<10))
static inline void som_starter_palette(unsigned slot,unsigned skin,uint16_t rgb[16]){
    static const uint16_t colours[]={0,SOM_RGB5(4,5,8),SOM_RGB5(9,23,24),SOM_RGB5(5,14,17),
        SOM_RGB5(11,7,5),SOM_RGB5(13,8,5),SOM_RGB5(22,13,7),
        0,0,SOM_RGB5(7,6,10),SOM_RGB5(28,12,9)};
    static const uint16_t skins[SOM_SKIN_COUNT][2]={
        {SOM_RGB5(31,25,19),SOM_RGB5(27,17,13)},
        {SOM_RGB5(30,22,13),SOM_RGB5(23,15,9)},
        {SOM_RGB5(25,15,9),SOM_RGB5(18,10,6)},
        {SOM_RGB5(18,11,8),SOM_RGB5(12,7,5)}};
    memset(rgb,0,16u*sizeof(*rgb));
    memcpy(rgb,colours,sizeof(colours));
    rgb[7]=skins[skin][0];rgb[8]=skins[skin][1];
    if(slot==1u){rgb[2]=SOM_RGB5(25,13,19);rgb[3]=SOM_RGB5(16,8,13);}
    if(slot==2u){rgb[2]=SOM_RGB5(19,23,8);rgb[3]=SOM_RGB5(12,14,5);}
}
#undef SOM_RGB5
#endif
