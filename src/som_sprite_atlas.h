#ifndef FLXTR_SOM_SPRITE_ATLAS_H
#define FLXTR_SOM_SPRITE_ATLAS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SOM_ATLAS_RECORD = 68, SOM_ATLAS_TILES = 512, SOM_ATLAS_MAX_RECORDS = 16384 };
typedef struct {
    uint8_t *records;
    unsigned count;
    unsigned pose;
    const uint8_t *tiles[SOM_ATLAS_TILES];
} SomSpriteAtlas;

static inline unsigned som_atlas_u16(const uint8_t *p){
    return (unsigned)p[0]|((unsigned)p[1]<<8);
}

static inline void som_atlas_clear(SomSpriteAtlas *atlas){
    free(atlas->records);
    memset(atlas,0,sizeof(*atlas));
}

static inline bool som_atlas_load(SomSpriteAtlas *atlas,const char *path,unsigned slot){
    FILE *input=fopen(path,"rb");
    if(!input)return false;
    uint8_t header[8];
    uint8_t *data=NULL;
    unsigned count=0;
    bool ok=fread(header,1,sizeof(header),input)==sizeof(header)&&
        !memcmp(header,"SAT1",4)&&header[4]==slot&&header[5]==0u;
    if(ok){
        count=som_atlas_u16(header+6);
        ok=count&&count<=SOM_ATLAS_MAX_RECORDS;
    }
    if(ok){
        data=malloc((size_t)count*SOM_ATLAS_RECORD);
        ok=data&&fread(data,SOM_ATLAS_RECORD,count,input)==count&&fgetc(input)==EOF&&!ferror(input);
    }
    fclose(input);
    unsigned previous=0;
    for(unsigned i=0;ok&&i<count;i++){
        const uint8_t *record=data+(size_t)i*SOM_ATLAS_RECORD;
        const unsigned pose=som_atlas_u16(record),tile=som_atlas_u16(record+2);
        const unsigned key=pose*SOM_ATLAS_TILES+tile;
        ok=tile<SOM_ATLAS_TILES&&(!i||key>previous);
        for(unsigned px=4;ok&&px<SOM_ATLAS_RECORD;px++)ok=record[px]<16u;
        previous=key;
    }
    if(!ok){free(data);return false;}
    som_atlas_clear(atlas);
    atlas->records=data;
    atlas->count=count;
    atlas->pose=UINT32_MAX;
    return true;
}

/* Reindex once after a pose change. The renderer only reads the pointer table. */
static inline void som_atlas_select(SomSpriteAtlas *atlas,unsigned pose){
    if(!atlas->records||atlas->pose==pose)return;
    atlas->pose=pose;
    memset(atlas->tiles,0,sizeof(atlas->tiles));
    unsigned lo=0,hi=atlas->count;
    while(lo<hi){
        const unsigned mid=lo+(hi-lo)/2u;
        const uint8_t *record=atlas->records+(size_t)mid*SOM_ATLAS_RECORD;
        if(som_atlas_u16(record)<pose)lo=mid+1u;
        else hi=mid;
    }
    for(unsigned i=lo;i<atlas->count;i++){
        const uint8_t *record=atlas->records+(size_t)i*SOM_ATLAS_RECORD;
        if(som_atlas_u16(record)!=pose)break;
        atlas->tiles[som_atlas_u16(record+2)]=record+4;
    }
}
#endif
