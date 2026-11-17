/* Raids: carried raiders fit/placed/kept drawn, end; champions and blows. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/som_raid.h"

static uint8_t ram[0x20000];

static void put16(unsigned a,unsigned v){ram[a]=(uint8_t)v;ram[a+1]=(uint8_t)(v>>8);}
static void town(void){
    memset(ram,0,sizeof(ram));
    ram[0xE000]=1;put16(0xDC,131);put16(0xC0,1024);put16(0xC2,1024);
    put16(0xE002,300);put16(0xE004,300);
}
static void carry(SomFamiliarCarry *c,unsigned palette,unsigned tile){
    memset(c,0,sizeof(*c));
    c->valid=1;
    c->object[0]=1;c->object[0x180]=0x00;c->object[0x184]=60;c->object[0x8B]=(uint8_t)(0x20|palette<<1);
    c->object[0x8A]=(uint8_t)tile;c->object[0x8B]=(uint8_t)(0x20|palette<<1);
    memset(c->tiles,0x5A,sizeof(c->tiles));
    for(unsigned i=0;i<32;i++)c->palette[i]=(uint8_t)(0x80+i);
}
static int all_kinds(unsigned kind,void *ctx){(void)ctx;return kind>=0x80;}
int main(void){
    (void)som_npc_apply_walk;(void)som_npc_parse_walk;(void)som_npc_rom_verified;
    (void)som_npc_walk_refusal;(void)som_familiar_tick;(void)som_familiar_tint;
    (void)som_familiar_summoner;(void)som_familiar_capture_vram;(void)som_familiar_save;
    (void)som_familiar_restore;(void)som_familiar_reapply;
    SomRaid r;som_raid_reset(&r);
    carry(&r.carry[0],5,0xA0);carry(&r.carry[1],6,0xA4);r.captured[0]=r.captured[1]=1;

    /* Palettes: 5-7 not used by any active object nor taken. */
    town();
    assert(som_raid_free_palette(ram,0)==5&&som_raid_free_palette(ram,1u<<5)==6);
    ram[0xE600]=1;ram[0xE68B]=0x2C;                         /* villager on palette 6 */
    assert(som_raid_free_palette(ram,1u<<5)==7);

    /* Start: the villager in monster slot 0 (object 3, palette 6) is kept;
     * raiders go to slots 1 and 2 with their own tile bases, palettes 5, 7. */
    town();
    ram[0xE600]=1;ram[0xE63D]=0xC4;ram[0xE68B]=0x2C;put16(0xE68A,0x2C40);
    put16(0xE88A,0x0848);put16(0xE86E,0x0E80);put16(0xEA8A,0x0860);put16(0xEA6E,0x0EA0);
    assert(som_raid_start(&r,ram)==2&&r.active&&r.map==131);
    assert(r.slot[0]==1&&r.slot[1]==2&&r.palette[0]==5&&r.palette[1]==7);
    assert(ram[0xE63D]==0xC4&&ram[0xE68B]==0x2C&&som_fam_u16(ram+0xE68A)==0x2C40);
    assert(ram[0xE88A]==0x48&&ram[0xE88B]==0x2A&&som_fam_u16(ram+0xE86E)==0x0E80);  /* own base, palette 5 */
    assert(ram[0xE8F8]==0xA2&&ram[0xEA8B]==0x2E&&ram[0xEAF8]==0xE2);
    assert(som_fam_u16(ram+0xE802)==348&&som_fam_u16(ram+0xE804)==300);   /* 48 px right */
    assert(som_fam_u16(ram+0xEA02)==252);                   /* the other one left */
    assert(som_fam_u16(ram+0xE982)==60&&ram[0xE9AC]==0xFF);
    assert(ram[0x5000+0x1000]==0x5A&&ram[0x5000+0x2000]==0x5A&&ram[0x5000]==0);  /* buffers 1, 2 */
    assert(ram[0xDF00+32*5]==0x80&&ram[0x0700+32*7]==0x80&&ram[0x0700+32*6]==0);

    /* Ticks keep the art painted; a death leaves one; both: repelled. */
    ram[0x5000+0x1000]=0;
    assert(som_raid_tick(&r,ram,10)==SOM_RAID_GOING&&ram[0x5000+0x1000]==0x5A);
    ram[0xEB80]=0xF9;
    assert(som_raid_tick(&r,ram,11)==SOM_RAID_GOING&&r.slot[1]==-1);

    /* Champion: static villager in NPC slot 8, 30 px left of raider 0. */
    ram[0xF000]=1;ram[0xF03D]=0xC5;ram[0xF044]=0xC0;put16(0xF002,318);put16(0xF004,300);
    unsigned npc=0,dir=9;int target=-1;
    assert(som_raid_champion(&r,ram,all_kinds,NULL,&npc,&dir,&target));
    assert(npc==8&&dir==2&&target==1);
    ram[0xF044]=0x40;                                       /* wandering: not a champion */
    assert(!som_raid_champion(&r,ram,all_kinds,NULL,&npc,&dir,&target));
    ram[0xF044]=0xC0;
    put16(0xF002,200);assert(!som_raid_champion(&r,ram,all_kinds,NULL,&npc,&dir,&target));
    put16(0xF002,318);
    /* The blow: in reach -> 8 pending damage; once only. */
    r.strike_npc=8;r.strike_target=1;
    assert(som_raid_strike(&r,ram)&&ram[0xE9F1]==8&&r.strike_target==-1);
    assert(!som_raid_strike(&r,ram));
    r.strike_target=1;put16(0xE802,500);
    assert(!som_raid_strike(&r,ram)&&ram[0xE9F1]==8);         /* it moved away */

    ram[0xE980]=0xF9;
    assert(som_raid_tick(&r,ram,12)==SOM_RAID_REPELLED&&!r.active&&r.ended_at==12);

    /* Leaving the map ends it too. */
    town();assert(som_raid_start(&r,ram)==2);
    put16(0xDC,130);assert(som_raid_tick(&r,ram,20)==SOM_RAID_LEFT&&!r.active);
    town();for(unsigned i=0;i<12;i++){ram[0xE000+i*0x200]=1;ram[0xE08B+i*0x200]=(uint8_t)(0x20|(5+i%3)<<1);}
    ram[0xE600]=0;ram[0xE800]=0;                             /* free slots, no free palette */
    assert(som_raid_start(&r,ram)==0);

    /* Nothing captured or no room: no raid. */
    town();r.captured[0]=r.captured[1]=0;assert(som_raid_start(&r,ram)==0&&!r.active);
    puts("som raid: ok");
    return 0;
}
