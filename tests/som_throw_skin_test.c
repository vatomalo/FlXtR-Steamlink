#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../src/som_throw_skin.h"

static void projectile(uint8_t *ram,unsigned k,unsigned facing,unsigned tile,
                       int sprite_x,int sprite_y){
    uint8_t *entry=ram+0xD000u+0x10u*k;
    entry[0]=3u;entry[1]=(uint8_t)facing;
    entry[2]=100u;entry[4]=80u;
    entry[14]=(uint8_t)tile;entry[15]=(uint8_t)(tile>>8);
    uint8_t *o=ram+0x0800u+4u*k;
    o[0]=(uint8_t)sprite_x;o[1]=(uint8_t)sprite_y;
    o[2]=(uint8_t)tile;o[3]=(uint8_t)(tile>>8);
}

int main(void){
    uint8_t ram[0x20000u]={0};
    const uint8_t look[3]={SOM_SKIN_STAR,0,0};
    SomThrowSkin out[9];
    /* Side throw's old exact offset, plus up/down offsets from the native
     * OAM piece. Each replacement must stay on that piece. */
    projectile(ram,0u,2u,0x120u,92,76);
    projectile(ram,1u,0u,0x122u,92,64);
    projectile(ram,2u,1u,0x124u,92,92);
    assert(som_throw_skins(ram,look,out)==3u);
    for(unsigned k=0;k<3u;k++){
        assert(ram[0x0800u+4u*k+1u]==0xF0u);
        assert(out[k].kind==SOM_SKIN_STAR&&out[k].x==100);
        assert(out[k].small);
    }
    assert(out[0].y==84&&out[1].y==72&&out[2].y==100);
    memset(ram,0,sizeof(ram));
    projectile(ram,0u,2u,0x120u,92,76);
    /* the shuriken is small even alone (player, 2026-11-27); the Fuma is full size */
    assert(som_throw_skins(ram,look,out)==1u&&out[0].small);
    memset(ram,0,sizeof(ram));
    projectile(ram,0u,2u,0x120u,92,76);
    {const uint8_t fuma[3]={SOM_SKIN_FUMA,SOM_SKIN_FUMA,SOM_SKIN_FUMA};
     assert(som_throw_skins(ram,fuma,out)==1u&&!out[0].small&&out[0].kind==SOM_SKIN_FUMA);}
    return 0;
}
