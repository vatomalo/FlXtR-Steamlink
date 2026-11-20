/* Familiar carried art must never repaint the source map's raw VRAM tiles. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/som_familiar.h"
static uint8_t ram[0x20000],vram[0x10000];
static void put16(unsigned p,unsigned v){ram[p]=(uint8_t)v;ram[p+1u]=(uint8_t)(v>>8);}
int main(void){
    (void)som_familiar_tick;(void)som_familiar_summoner;
    SomFamiliarCarry c;memset(&c,0,sizeof(c));
    c.valid=1;c.has_vram=1;c.tile_count=1;c.home_map=130;
    c.object[0]=1;c.object[0x180]=0;c.object[0x184]=25;
    c.object[0x8A]=0xA0;c.object[0x8B]=0x2E;
    c.source_frame_tile[0]=0x2EA0;
    c.tile_id[0]=0xA0u;
    memset(c.tile_data[0],0xB7,32);
    memset(c.tiles,0x4A,sizeof(c.tiles));memset(c.palette,0x2D,32);
    ram[0xE000u]=1;ram[0xE4u]=1;ram[0xE6u]=0x0F;
    put16(0xE002u,220);put16(0xE004u,180);put16(0xDCu,131);
    /* The target's own sprite upload area and tile layout: palette 5,
     * OBJ tile 0x40, while old source uses 0xA0 and palette 7. */
    put16(0xE68Au,0x2A40u);put16(0xE66Cu,0x1111u);
    memset(vram,0x11,sizeof(vram));
    assert(som_familiar_restore(&c,ram,vram)==0);
    assert(c.painted_slot==0);
    assert(ram[0xE68Au]==0x40u&&ram[0xE68Bu]==0x2Au);
    assert(ram[0xE66Cu]==0x11u&&ram[0xE66Du]==0x11u);
    assert(vram[0xC000u+0x40u*32u]==0xB7u);
    assert(vram[0xC000u+0xA0u*32u]==0x11u);  /* old map untouched */
    assert(ram[0x5000u]==0x4Au&&ram[0xDF00u+5u*32u]==0x2Du);
    assert(ram[0xDF00u+7u*32u]==0u);
    /* Native actor coordinates, HP, reward fields untouched by art repaint. */
    ram[0xE681u]=0x2A;
    ram[0x5000u]=0;
    som_familiar_reapply(&c,ram,vram);
    assert(ram[0x5000u]==0x4Au&&ram[0xE681u]==0x2Au);
    /* Other visible objects sharing palette 5 must not be recoloured. */
    ram[0xEC00u]=1;ram[0xEC8Bu]=0x2Au;
    ram[0xDF00u+5u*32u]=0x91u;
    som_familiar_reapply(&c,ram,vram);
    assert(ram[0xDF00u+5u*32u]==0x91u);
    /* Periodic state sync must retain art, not replace with a foreign buffer. */
    ram[0x5000u]=0;ram[0xE782u]=14; /* actor +0x182: current HP */
    som_familiar_update_object(&c,ram,0);
    assert(c.tiles[0]==0x4Au&&c.object[0x182u]==14);
    puts("som familiar art: ok");
    return 0;
}
