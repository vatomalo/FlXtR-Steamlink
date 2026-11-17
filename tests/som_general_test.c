/* Generals: roster file, election, boost, memory, ranks, adaptation. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/som_general.h"

static uint8_t ram[0x20000];

static void put16(unsigned a,unsigned v){ram[a]=(uint8_t)v;ram[a+1]=(uint8_t)(v>>8);}
static void field(unsigned map){
    memset(ram,0,sizeof(ram));
    ram[0xE000]=1;ram[0x00E4]=1;ram[0x00E6]=0x0F;put16(0xDC,map);
    put16(0xC0,1024);put16(0xC2,1024);                 /* map size; tiles 0 = open */
    put16(0xE182,40);put16(0xE002,200);put16(0xE004,200);/* boy */
    for(int s=0;s<3;s++){                               /* three Rabites, 20 HP */
        const unsigned o=0xE600u+0x200u*(unsigned)s;
        ram[o]=1;ram[o+0x180]=0x00;put16(o+0x182,20);put16(o+0x184,20);
        put16(o+2,300+40*(unsigned)s);put16(o+4,300);
    }
    for(unsigned i=0;i<96;i++)ram[0xDFA0+i]=(uint8_t)(0x40+(i&31));
}
int main(void){
    (void)som_bun_tile_open;
    /* Roster file round trip; bad lines are dropped. */
    SomGeneral g[SOM_GEN_MAX];
    assert(som_gen_defaults(&g[0])==1&&g[0].rank==1&&g[0].home_map==130&&g[0].nemesis==-1);
    g[0].damage[2]=17;g[0].nemesis=2;g[0].wins=1;
    char text[512];
    som_gen_format(g,1,text,sizeof(text));
    SomGeneral back[SOM_GEN_MAX];
    assert(som_gen_parse(text,back,SOM_GEN_MAX)==1);
    assert(!strcmp(back[0].name,"RABBITE ELITE")&&back[0].damage[2]==17&&back[0].nemesis==2&&
           back[0].wins==1&&back[0].rank==1&&back[0].home_map==130&&back[0].species==0);
    assert(som_gen_parse("SOMGENERALS 2\n0 130 1 0 0 0 0 0 0 0 0 0 0 0 0 -1 X\n",back,8)==0);
    assert(som_gen_parse("SOMGENERALS 1\n0 130 9 0 0 0 0 0 0 0 0 0 0 0 0 -1 X\n",back,8)==0); /* rank */
    assert(som_gen_parse("SOMGENERALS 1\ngarbage\n0 130 2 0 0 0 0 0 0 0 0 0 0 0 0 -1 R\n",back,8)==1&&
           back[0].rank==2&&!strcmp(back[0].name,"R"));

    /* Election on the home map: first Rabite, HP x (1 + rank), tinted. */
    som_gen_defaults(&g[0]);
    SomGeneralLive l;som_gen_reset_live(&l);
    field(131);
    assert(som_gen_elect(&g[0],&l,ram,10,-1)==SOM_GEN_NONE);          /* not home */
    field(130);
    ram[0xE600]=0;                                                /* slot 0 empty */
    {SomGeneralLive f;som_gen_reset_live(&f);           /* the familiar's slot is skipped */
     uint8_t keep[sizeof(ram)];memcpy(keep,ram,sizeof(ram));
     assert(som_gen_elect(&g[0],&f,ram,10,1)==SOM_GEN_ELECTED&&f.slot==2);memcpy(ram,keep,sizeof(ram));}
    assert(som_gen_elect(&g[0],&l,ram,10,-1)==SOM_GEN_ELECTED&&l.slot==1);
    assert(som_gen_u16(ram+0xE982)==40&&som_gen_u16(ram+0xE984)==40);
    assert(som_gen_u16(ram+0xE782)==20&&som_gen_u16(ram+0xEB82)==20);  /* others untouched */
    assert(ram[0xDFC2]!=0x42||ram[0xDFC3]!=0x43);                  /* slot 1 palette tinted */
    assert(ram[0xDFA2]==0x42&&ram[0xDFE2]==0x42);                  /* slots 0 and 2 not */
    assert(som_gen_elect(&g[0],&l,ram,11,-1)==SOM_GEN_NONE);          /* already elected */

    /* Cooldown held at 99 - 12 * rank. */
    ram[0xE9ED]=99;
    assert(som_gen_tick(&g[0],&l,ram,12)==SOM_GEN_NONE&&ram[0xE9ED]==87);

    /* Damage by weapon type: 10 with the axe (2), then the sword (1) kills it. */
    ram[0xE1E4]=2;put16(0xE982,30);som_gen_tick(&g[0],&l,ram,13);
    assert(l.damage[2]==10);
    ram[0xE1E4]=1;put16(0xE982,25);som_gen_tick(&g[0],&l,ram,14);
    assert(l.damage[1]==5);
    ram[0xE980]=0xF9;                                              /* dying */
    assert(som_gen_tick(&g[0],&l,ram,15)==SOM_GEN_DEFEATED);
    assert(g[0].defeats==1&&g[0].encounters==1&&g[0].rank==2&&g[0].nemesis==1);  /* 5 + 25 by sword */
    assert(g[0].damage[1]==30&&g[0].damage[2]==10&&l.slot<0&&l.done);
    assert(som_gen_elect(&g[0],&l,ram,16,-1)==SOM_GEN_NONE);          /* one per visit */

    /* Next visit: rank 2 = HP x3, cap 75; the boy holds the sword (nemesis). */
    field(131);som_gen_elect(&g[0],&l,ram,100,-1);
    field(130);
    assert(som_gen_elect(&g[0],&l,ram,101,-1)==SOM_GEN_ELECTED&&l.slot==0);
    assert(som_gen_u16(ram+0xE782)==60);
    ram[0xE1E4]=1;ram[0xE7ED]=60;
    put16(0xE602,220);put16(0xE604,200);                           /* 20 px right of the boy */
    assert(som_gen_tick(&g[0],&l,ram,102)==SOM_GEN_BACKING);
    assert(som_gen_u16(ram+0xE602)==221);
    som_gen_tick(&g[0],&l,ram,103);assert(som_gen_u16(ram+0xE602)==222);
    ram[0xE7ED]=10;                                                /* bite ready: stands */
    assert(som_gen_tick(&g[0],&l,ram,104)==SOM_GEN_STANDING&&som_gen_u16(ram+0xE602)==222);
    ram[0xE1E4]=2;ram[0xE7ED]=60;                                  /* not the nemesis */
    assert(som_gen_tick(&g[0],&l,ram,105)==SOM_GEN_NONE&&som_gen_u16(ram+0xE602)==222);

    /* It knocks the boy out: a win, rank 3. */
    put16(0xE182,0);
    assert(som_gen_tick(&g[0],&l,ram,106)==SOM_GEN_WON&&g[0].wins==1&&g[0].rank==3);

    /* Escape: the hero leaves with it alive; no rank change. */
    field(131);som_gen_elect(&g[0],&l,ram,200,-1);field(130);
    assert(som_gen_elect(&g[0],&l,ram,201,-1)==SOM_GEN_ELECTED);
    put16(0xDC,131);
    assert(som_gen_tick(&g[0],&l,ram,202)==SOM_GEN_ESCAPED&&g[0].escapes==1&&g[0].rank==3);

    /* Vanished (slot cleared or overwritten by a villager): no record. */
    {   SomGeneral v;som_gen_defaults(&v);SomGeneralLive vl;som_gen_reset_live(&vl);
        field(130);assert(som_gen_elect(&v,&vl,ram,300,-1)==SOM_GEN_ELECTED);
        ram[0xE600]=1;ram[0xE780]=0xC4;                          /* a villager kind */
        assert(som_gen_tick(&v,&vl,ram,301)==SOM_GEN_VANISHED&&v.rank==1&&v.defeats==0&&
               v.encounters==0&&vl.slot<0&&vl.done);
        som_gen_reset_live(&vl);field(130);som_gen_elect(&v,&vl,ram,302,-1);
        ram[0xE600]=0;                                           /* cleared */
        assert(som_gen_tick(&v,&vl,ram,303)==SOM_GEN_VANISHED&&v.rank==1);
        som_gen_reset_live(&vl);field(130);som_gen_elect(&v,&vl,ram,304,-1);
        put16(0xE790,0x8000);                                    /* killed: status */
        assert(som_gen_tick(&v,&vl,ram,305)==SOM_GEN_DEFEATED&&v.rank==2);
    }
    /* The game rebuilds stats away from home: boost kept, full stays full. */
    {   SomGeneral v;som_gen_defaults(&v);SomGeneralLive vl;som_gen_reset_live(&vl);
        field(130);assert(som_gen_elect(&v,&vl,ram,400,-1)==SOM_GEN_ELECTED&&vl.max_hp==40);
        put16(0xE782,20);put16(0xE784,20);som_gen_tick(&v,&vl,ram,401);
        assert(som_gen_u16(ram+0xE784)==40&&som_gen_u16(ram+0xE782)==40);
        put16(0xE782,10);put16(0xE784,20);som_gen_tick(&v,&vl,ram,402);
        assert(som_gen_u16(ram+0xE784)==40&&som_gen_u16(ram+0xE782)==10);
    }
    /* Rank is capped. */
    g[0].rank=SOM_GEN_MAX_RANK;SomGeneralLive c;som_gen_reset_live(&c);c.slot=0;
    som_gen_conclude(&g[0],&c,SOM_GEN_DEFEATED);assert(g[0].rank==SOM_GEN_MAX_RANK);
    char label[32];som_gen_label(&g[0],label,sizeof(label));assert(!strcmp(label,"RABBITE ELITE R5"));
    puts("som general: ok");
    return 0;
}
