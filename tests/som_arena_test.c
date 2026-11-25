#include "../src/som_arena.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static uint8_t ram[0x20000];
int main(int argc,char **argv){
    uint8_t table[0x1000]={0};
    table[4*5]=0x69;table[4*5+1]=0x01;table[4*9]=0x69;table[4*9+1]=0x01;       /* 5 and 9 -> 361 */
    assert(som_arena_transition_to(table,361u,9u)==9&&som_arena_transition_to(table,361u,7u)==5);
    assert(som_arena_transition_to(table,300u,9u)==-1);
    ram[0xE4]=1;ram[0xE6]=0x0F;
    assert(som_arena_go(ram,0x2C2u,1)&&ram[0xD0]==1&&ram[0xD3]==0x7E);
    const uint8_t want[]={0x1A,0xC2,0};
    assert(ram[0xCF56]==5&&ram[0xCFCE]==1);
    som_arena_restore(ram);assert(ram[0xCF56]==0&&ram[0xCFCE]==0);
    ram[0xCF56]=0x23;ram[0xD0]=0;assert(som_arena_go(ram,0x2C2u,1)&&ram[0xCF56]==0x25);
    som_arena_restore(ram);som_arena_restore(ram);assert(ram[0xCF56]==0x23);ram[0xD0]=1;
    assert(!memcmp(ram+SOM_ARENA_SCRIPT,want,sizeof(want)));
    assert(!som_arena_go(ram,0x2C2u,1));                  /* an event runs */
    ram[0xD0]=0;assert(som_arena_go(ram,0xF7u,0)&&ram[SOM_ARENA_SCRIPT]==0x18);
    /* Another ROM is left alone. */
    uint8_t *rom=calloc(1,0x400000u);
    assert(rom&&!som_arena_patch(rom,0x400000u));
    for(size_t i=0;i<0x400000u;i++)assert(!rom[i]);
    if(argc>1){                                           /* the player's own ROM */
        FILE *f=fopen(argv[1],"rb");
        assert(f&&fread(rom,1,0x400000u,f)==0x400000u);
        fclose(f);
        assert(som_arena_patch(rom,0x400000u)&&!som_arena_patch(rom,0x400000u));
        assert(rom[0x21A5D9]==0x21&&rom[0x2115D9]==7&&rom[0x206000+3*0x378+2]==0xE1);
        puts("PASS: arena (ROM)");
    }
    free(rom);
    puts("PASS: arena");
    return 0;
}
