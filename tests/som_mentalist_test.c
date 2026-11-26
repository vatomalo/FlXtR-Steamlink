/* The Mentalist's string on synthetic WRAM (src/som_mentalist.h). */
#include <assert.h>
#include <stdio.h>
#include "../src/som_mentalist.h"

static uint8_t ram[0x20000];
static uint8_t *mon(unsigned k){return ram+SOM_MIND_BASE+SOM_MIND_OBJECT*k;}
static void put(unsigned k,uint8_t id,int x,int y){
    uint8_t *m=mon(k);memset(m,0,SOM_MIND_OBJECT);
    m[0]=1;m[0x180]=id;som_mind_put16(m+2,(unsigned)x);som_mind_put16(m+4,(unsigned)y);
    som_mind_put16(m+0x182,50);som_mind_put16(m+0x184,50);
}
static int walls_at_x(const uint8_t *r,int x,int y,unsigned place){(void)r;(void)y;(void)place;return x<300;}

int main(void){
    SomMind s;som_mind_reset(&s);
    /* The string: 3 whispers, Misdirection, finisher, then it starts again;
     * a pause starts it again too. */
    for(unsigned i=1;i<=5;i++)assert(som_mind_next_step(&s,0,100+i,90)==i);
    assert(som_mind_next_step(&s,0,106,90)==1);
    assert(som_mind_next_step(&s,0,107,90)==2);
    assert(som_mind_next_step(&s,0,300,90)==1);
    assert(som_mind_next_step(&s,1,300,90)==1);          /* each hero their own */

    /* Marks: up to three; a new monster in the slot starts clean. */
    put(0,0x01,100,100);put(1,0x02,140,100);put(2,0x03,400,400);
    for(unsigned i=0;i<5;i++)som_mind_mark(&s,ram,0);
    assert(s.marks[0]==3);
    put(0,0x04,100,100);som_mind_sync(&s,ram,0);assert(s.marks[0]==0);

    /* Misdirection: monster 0 goes for its nearest fellow (1, not far 2),
     * does not go for heroes, and strikes once through pending damage. */
    assert(som_mind_partner(ram,0,-1)==1);
    assert(som_mind_partner(ram,0,1)==-1);                 /* 1 is the familiar, 2 too far */
    assert(som_mind_betray(&s,ram,0,1,9,1000,-1));
    unsigned long f=1000;unsigned struck=0,ended=0;
    for(;f<1100&&!(struck&1u);f++){
        struck=som_mind_tick(&s,ram,f,-1,NULL,&ended);
        assert(mon(0)[0x1AC]==0xFF&&mon(0)[0x1ED]==0x7F);
    }
    assert(struck&1u);
    assert(mon(1)[0x1F1]==9&&som_mind_u16(mon(0)+2)>=140-SOM_MIND_REACH);
    for(unsigned i=0;i<20;i++)som_mind_tick(&s,ram,f++,-1,NULL,&ended);
    assert(!s.effect[0]&&mon(0)[0x1ED]==24);              /* once, then itself again */
    assert(mon(1)[0x1F1]==9);

    /* Betray with three marks: three strikes, SOM_MIND_STRIKE_GAP apart. */
    mon(1)[0x1F1]=0;
    assert(som_mind_betray(&s,ram,0,3,5,2000,-1));
    unsigned hits=0;
    for(f=2000;f<2300;f++)if(som_mind_tick(&s,ram,f,-1,NULL,NULL)&1u)hits++;
    assert(hits==3&&mon(1)[0x1F1]==15);

    /* Hold: native speed zeroed, no bite, it stays put; it ends on time. */
    put(0,0x01,100,100);mon(0)[0x06]=3;
    assert(som_mind_hold(&s,ram,0,3000,-1));
    for(f=3000;f<3000+SOM_MIND_HOLD_FRAMES-1;f++){
        som_mind_tick(&s,ram,f,-1,NULL,NULL);
        assert(!mon(0)[0x06]&&mon(0)[0x1ED]==0x7F&&som_mind_u16(mon(0)+2)==100);
    }
    som_mind_tick(&s,ram,3000+SOM_MIND_HOLD_FRAMES,-1,NULL,&ended);
    assert(!s.effect[0]&&(ended&1u));

    /* Panic: away from where the fear came from, stopped by walls. */
    put(0,0x01,290,100);
    assert(som_mind_panic(&s,ram,0,250,100,4000,-1));
    for(f=4000;f<4040;f++)som_mind_tick(&s,ram,f,-1,walls_at_x,NULL);
    assert(som_mind_u16(mon(0)+2)>=296&&som_mind_u16(mon(0)+2)<300);

    /* Swap Minds: places traded, each turns on the other. */
    put(0,0x01,100,100);put(1,0x02,140,120);
    assert(som_mind_swap(&s,ram,0,7,5000,-1)==1);
    assert(som_mind_u16(mon(0)+2)==140&&som_mind_u16(mon(1)+2)==100);
    assert(s.effect[0]==SOM_MIND_BETRAY&&s.target[0]==1&&s.effect[1]==SOM_MIND_BETRAY&&s.target[1]==0);

    /* No boss minds; a monster that dies or changes ends its effect. */
    put(2,SOM_MIND_BOSS,120,100);
    assert(!som_mind_hold(&s,ram,2,6000,-1)&&!som_mind_betray(&s,ram,2,1,5,6000,-1));
    put(0,0x01,100,100);put(1,0x02,140,100);
    assert(som_mind_hold(&s,ram,0,6000,-1));
    mon(0)[0x180]=0x09;som_mind_tick(&s,ram,6001,-1,NULL,&ended);
    assert(!s.effect[0]&&(ended&1u));

    /* Mirror: armed for one hero only, for SOM_MIND_MIRROR_FRAMES. */
    som_mind_mirror(&s,2,7000);
    assert(som_mind_mirror_armed(&s,2,7001)&&!som_mind_mirror_armed(&s,0,7001));
    assert(!som_mind_mirror_armed(&s,2,7000+SOM_MIND_MIRROR_FRAMES));
    puts("PASS: Mentalist marks, misdirection, betray, hold, panic, swap and mirror");
    return 0;
}
