/* Monk's Intercept windows, target, landing spot and damage. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/som_intercept.h"
static uint8_t ram[0x20000];
static void put16(unsigned p,unsigned v){ram[p]=(uint8_t)v;ram[p+1u]=(uint8_t)(v>>8);}
static void monster(unsigned k,unsigned x,unsigned y){
    const unsigned b=SOM_ICPT_MONSTERS+SOM_ICPT_OBJECT*k;
    ram[b]=1;ram[b+0x180]=0;put16(b+2,x);put16(b+4,y);put16(b+0x182,20);
}
int main(void){
    SomIntercept s;memset(&s,0,sizeof s);
    uint8_t *hero=ram+0xE000u;hero[0]=1;put16(0xE002u,100);put16(0xE004u,100);
    monster(0,140,100);monster(1,400,100);monster(2,100,60);
    som_icpt_scan(&s,ram,hero,10,-1);
    assert(som_icpt_target(&s)<0);
    ram[0xE61Cu]=0x80u;ram[0xE81Cu]=0x80u;              /* 0 near, 1 too far */
    som_icpt_scan(&s,ram,hero,11,-1);
    assert(s.opened[0]==11&&!s.opened[1]&&som_icpt_target(&s)==0);
    ram[0xEA1Cu]=0x80u;                                 /* 2 starts later */
    som_icpt_scan(&s,ram,hero,13,-1);
    assert(s.opened[2]==13&&som_icpt_target(&s)==0);   /* the oldest first */
    som_icpt_scan(&s,ram,hero,11+SOM_ICPT_WINDOW+1,-1); /* 0 ran out */
    assert(!s.opened[0]&&som_icpt_target(&s)==2);
    ram[0xEA1Cu]=0;som_icpt_scan(&s,ram,hero,30,-1);   /* 2 ended */
    assert(som_icpt_target(&s)<0);
    ram[0xEA1Cu]=0x80u;som_icpt_scan(&s,ram,hero,31,2); /* the familiar never */
    assert(!s.opened[2]);
    int x,y;uint8_t f;
    som_icpt_stand(hero,ram+SOM_ICPT_MONSTERS,&x,&y,&f);
    assert(x==120&&y==100&&f==0x02u);                   /* monster right: land left of it, face right */
    som_icpt_stand(hero,ram+SOM_ICPT_MONSTERS+2u*SOM_ICPT_OBJECT,&x,&y,&f);
    assert(x==100&&y==80&&f==0x00u);                    /* monster above: land below, face up */
    assert(som_icpt_damage(0,0,1)==16&&som_icpt_damage(0,1,1)==23&&som_icpt_damage(5,0,9)==34);
    puts("som intercept: ok");
    return 0;
}
