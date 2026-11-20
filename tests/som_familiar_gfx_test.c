#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../src/som_familiar.h"

int main(void){
    uint8_t *ram=calloc(0x20000u,1u),*vram=calloc(0x10000u,1u);
    assert(ram&&vram);
    SomFamiliarCarry *c=calloc(1,sizeof(*c));
    assert(c);
    c->valid=1;c->has_vram=1;
    c->object[0]=1u;c->object[0x180]=0x01u;
    c->object[0x8A]=0xA0u;c->object[0x8B]=0x2Au; /* source tile A0, palette 5 */
    c->captured_base=0xA0u;c->tile_count=1u;c->tile_id[0]=0xA0u;
    memset(c->tile_data[0],0xCA,32u);
    memset(c->sprite_palette,0x34,32u);
    memset(c->palette,0x34,32u);
    /* Slot zero is empty; the game reserved tile C0 and OBJ palette 7. */
    uint8_t *dst=ram+SOM_FAMILIAR_MONSTER_BASE;
    dst[0x8A]=0xC0u;dst[0x8B]=0x2Eu;
    ram[0xE002]=64u;ram[0xE004]=64u;
    vram[SOM_FAMILIAR_OBJ_VRAM+0xA0u*32u]=0xBEu;
    assert(som_familiar_restore(c,ram,vram)==0);
    assert(c->active_palette==7u);
    assert(c->active_shift==0x20);
    assert(dst[0x8A]==0xC0u);
    assert(((dst[0x8B]>>1)&7u)==7u);
    assert(vram[SOM_FAMILIAR_OBJ_VRAM+0xA0u*32u]==0xBEu);
    assert(vram[SOM_FAMILIAR_OBJ_VRAM+0xC0u*32u]==0xCAu);
    assert(ram[0xDF00u+7u*32u]==0x34u);
    /* Reapplying never touches the captured source's old tile ID. */
    vram[SOM_FAMILIAR_OBJ_VRAM+0xA0u*32u]=0xFAu;
    som_familiar_reapply(c,ram,vram,0);
    assert(vram[SOM_FAMILIAR_OBJ_VRAM+0xA0u*32u]==0xFAu);
    assert(vram[SOM_FAMILIAR_OBJ_VRAM+0xC0u*32u]==0xCAu);
    /* A fresh destination slot must be declined if its palette is in use. */
    dst[0]=0;
    dst[0x8A]=0xC0u;dst[0x8B]=0x2Eu;
    uint8_t *other=ram+0xE000u+0x200u*6u;
    other[0]=1;other[0x8B]=0x2Eu;
    assert(som_familiar_restore(c,ram,vram)==-1);
    assert(dst[0]==0);
    free(c);free(vram);free(ram);
    return 0;
}
