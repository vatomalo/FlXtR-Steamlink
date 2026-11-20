#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../src/som_telekinesis.h"

int main(void){
    uint8_t *ram=calloc(0x20000u,1u);
    assert(ram);
    SomTelekinesis tk;
    som_tk_init(&tk);
    assert(tk.slot==-1);
    ram[0xE000]=1;ram[0xE4]=1;ram[0xE6]=0x0F;
    ram[0xC0]=0;ram[0xC1]=2;ram[0xC2]=0;ram[0xC3]=2;
    ram[0xE002]=64;ram[0xE004]=64;
    uint8_t *chest=ram+0xE600;
    chest[0]=1;chest[0x180]=0xEA;chest[2]=128;chest[4]=64;
    assert(som_tk_is_chest(chest));
    assert(som_tk_start(&tk,ram,1,-1));
    assert(tk.slot==0);
    const unsigned x0=som_bun_u16(chest+2);
    assert(som_tk_tick(&tk,ram,2));
    assert(som_bun_u16(chest+2)<x0);
    /* Normal monsters, NPCs and bosses cannot be pulled. */
    som_tk_init(&tk);
    chest[0x180]=0x01;
    assert(!som_tk_start(&tk,ram,3,-1));
    chest[0x180]=0x57;
    assert(!som_tk_start(&tk,ram,3,-1));
    chest[0x180]=0xEA;
    assert(!som_tk_start(&tk,ram,3,0));
    chest[0x0B]=0x80;
    assert(!som_tk_start(&tk,ram,3,-1));
    chest[0x0B]=0;
    assert(som_tk_start(&tk,ram,3,-1));
    /* Slot reuse and map transitions abort instead of moving another actor. */
    chest[0x180]=0x05;
    assert(!som_tk_tick(&tk,ram,4));
    assert(tk.slot==-1);
    chest[0x180]=0xEA;
    assert(!som_tk_start(&tk,ram,4,-1)); /* cooldown */
    som_tk_init(&tk);
    assert(som_tk_start(&tk,ram,7,-1));
    ram[0xDC]=1;
    assert(!som_tk_tick(&tk,ram,8));
    assert(tk.slot==-1);
    free(ram);
    return 0;
}
