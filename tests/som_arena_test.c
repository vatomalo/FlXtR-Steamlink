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
    /* Fights */
    uint8_t records[29*0x54]={0};
    for(unsigned id=0;id<0x54u;id++)records[29u*id]=(uint8_t)id;
    for(unsigned roll=0;roll<50u;roll++){
        const unsigned id=som_arena_species(records,20u,roll);
        assert(id>=17u&&id<=23u);
    }
    assert(som_arena_wave_level(21u,1u)==14u&&som_arena_wave_level(21u,10u)==27u&&som_arena_wave_level(99u,10u)==0x40u);
    memset(ram+0xCF00,0,0x100);
    assert(som_arena_boss_pick(ram,7u)==0);                 /* the Mantis Ant only */
    ram[0xCF2C]=3;assert(som_arena_boss_pick(ram,1u)==3);
    som_arena_list(ram,1u,1,0x57u,16u,13u);
    assert(ram[0xC81D]==0x57&&ram[0xC81E]==0&&ram[0xC819]==0x0F&&ram[0xC81A]==0x90);
    uint8_t *a=ram+0xE000,*b=ram+0xE200;
    memset(a,0,0x400);a[2]=100;a[4]=100;b[2]=110;b[4]=100;
    a[0xC2]=20;a[0xC3]=20;a[0xC1]=0xF2;
    assert(!som_arena_hit(ram,0u,1u));a[0x1C]=0x20;assert(som_arena_hit(ram,0u,1u)==2);
    b[2]=180;assert(!som_arena_hit(ram,0u,1u));
    ram[0xD000]=1;ram[0xD002]=180;ram[0xD004]=86;assert(som_arena_hit(ram,0u,1u)==1&&ram[0xD007]==8);
    assert(!som_arena_hit(ram,0u,1u));                      /* spent */
    a[0x198]=62;b[0x1A5]=22;assert(som_arena_damage(a,b)==25u);
    b[0x1A5]=255;assert(som_arena_damage(a,b)==6u);
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
