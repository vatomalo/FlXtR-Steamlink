/* Safe Mentalist chest pulling: target selection, collisions and event guards. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/som_telekinesis.h"

static uint8_t ram[0x20000];
static void put16(unsigned offset,unsigned value){
    ram[offset]=(uint8_t)value;ram[offset+1u]=(uint8_t)(value>>8);
}
static uint8_t *actor(unsigned slot){return ram+0xE000u+0x200u*slot;}
static void setup(void){
    memset(ram,0,sizeof(ram));
    ram[0xE000u]=1u;ram[0xE4u]=1u;ram[0xE6u]=0x0Fu;
    put16(0xDCu,130u);put16(0xC0u,512u);put16(0xC2u,512u);
    put16(0xE002u,200u);put16(0xE004u,100u);
    /* Flat, walkable tile attributes are all zero. */
}
static void chest(unsigned slot,unsigned x,unsigned y){
    uint8_t *o=actor(slot);o[0]=1;o[0x180u]=0xA0u;
    o[0x181u]=3u;o[0x1B1u]=0x80u;o[0x1FBu]=0x10u;
    put16(0xE000u+0x200u*slot+2u,x);
    put16(0xE000u+0x200u*slot+4u,y);
}
int main(void){
    SomTelekinesis t;som_tk_reset(&t);
    setup();
    /* NPCs and ordinary monsters cannot be grabbed even when nearer. */
    actor(3)[0]=1;put16(0xE602u,198u);put16(0xE604u,100u);
    chest(4,120u,100u);
    assert(som_tk_nearest(ram,200,100,0u)==4);
    assert(som_tk_begin(&t,ram,100u)&&t.slot==4);
    int completed=0;
    for(unsigned long f=101u;f<200u;f++){
        const int s=som_tk_step(&t,ram,f);
        if(s==2){completed=1;break;}
        assert(s==1);
    }
    assert(completed&&t.slot==-1);
    assert(som_bun_u16(actor(4)+2u)>=172u);
    assert(actor(4)[0x181u]==3u&&actor(4)[0x1B1u]==0x80u);
    assert(actor(4)[0x1FBu]==0x10u);
    assert(som_bun_u16(actor(3)+2u)==198u);

    /* Native opened chests cannot be selected, and actor identity is checked. */
    setup();chest(4,120u,100u);actor(4)[0x181u]=0xFFu;
    assert(!som_tk_begin(&t,ram,300u));
    actor(4)[0x181u]=0;
    assert(som_tk_begin(&t,ram,300u));
    actor(4)[0x180u]=0xB0u;
    assert(som_tk_step(&t,ram,301u)==-1&&t.slot==-1);

    /* A map change or native script interrupts the grab without teleporting. */
    setup();chest(4,120u,100u);assert(som_tk_begin(&t,ram,500u));
    put16(0xDCu,131u);
    assert(som_tk_step(&t,ram,501u)==-1&&som_bun_u16(actor(4)+2u)==120u);
    setup();chest(4,120u,100u);assert(som_tk_begin(&t,ram,500u));
    ram[0xD0u]=0x83u;
    assert(som_tk_step(&t,ram,501u)==-1&&som_bun_u16(actor(4)+2u)==120u);

    /* Collision holds the chest on the near side of a wall. */
    setup();chest(4,120u,100u);assert(som_tk_begin(&t,ram,800u));
    ram[0x1B800u]=3u; /* flat tile 0 is now an impassable wall */
    assert(som_tk_step(&t,ram,801u)==-1&&som_bun_u16(actor(4)+2u)==120u);

    /* The game can reclaim an object slot; our stale grab must be refused. */
    setup();chest(4,120u,100u);assert(som_tk_begin(&t,ram,900u));
    put16(0xE802u,121u);
    assert(som_tk_step(&t,ram,901u)==-1&&t.slot==-1);

    /* Same tile/floor only, no across-wall or across-layer teleport. */
    setup();chest(4,120u,100u);actor(4)[0x0Bu]=0x80u;
    assert(!som_tk_begin(&t,ram,1200u));
    puts("som telekinesis: ok");
    return 0;
}
