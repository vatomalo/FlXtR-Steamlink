/* Raids: carried raiders fit/placed/kept drawn, end; champions and blows. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/som_raid.h"

static uint8_t ram[0x20000];
static uint8_t vram[0x10000];

static void put16(unsigned a,unsigned v){ram[a]=(uint8_t)v;ram[a+1]=(uint8_t)(v>>8);}
static void town(void){
    memset(ram,0,sizeof(ram));memset(vram,0,sizeof(vram));
    ram[0xE000]=1;put16(0xDC,131);put16(0xC0,1024);put16(0xC2,1024);
    put16(0xE002,300);put16(0xE004,300);
}
static void carry(SomFamiliarCarry *c,unsigned palette,unsigned tile){
    memset(c,0,sizeof(*c));
    c->valid=1;c->has_vram=1;
    c->object[0]=1;c->object[0x180]=0x00;c->object[0x184]=60;c->object[0x8B]=(uint8_t)(0x20|palette<<1);
    c->object[0x8A]=(uint8_t)tile;
    c->tile_count=4;
    const unsigned ids[4]={tile,tile+1,tile+16,tile+17};
    for(unsigned i=0;i<4;i++){c->tile_id[i]=(uint16_t)ids[i];memset(c->tile_data[i],0x11*(i+1),32);}
    c->palette_address=0xDF00u+32u*palette;
    for(unsigned i=0;i<32;i++)c->sprite_palette[i]=(uint8_t)(0x80+i);
}
static int all_kinds(unsigned kind,void *ctx){(void)ctx;return kind>=0x80;}
int main(void){
    (void)som_npc_apply_walk;(void)som_npc_parse_walk;(void)som_npc_rom_verified;
    (void)som_npc_walk_refusal;(void)som_familiar_tick;(void)som_familiar_tint;
    (void)som_familiar_summoner;(void)som_familiar_capture_vram;(void)som_familiar_save;
    (void)som_familiar_restore;
    SomRaid r;som_raid_reset(&r);
    carry(&r.carry[0],5,0xA0);carry(&r.carry[1],6,0xA4);r.captured[0]=r.captured[1]=1;

    /* Fit: a villager on palette 5 or on tile 0xA1 blocks raider 0 only. */
    town();
    assert(som_raid_fits(&r.carry[0],ram)&&som_raid_fits(&r.carry[1],ram));
    ram[0xF000]=1;ram[0xF08B]=0x2A;                         /* NPC slot 8, palette 5 */
    assert(!som_raid_fits(&r.carry[0],ram)&&som_raid_fits(&r.carry[1],ram));
    ram[0xF08B]=0x20;put16(0xF08A,0xA1|0x2000);             /* tile 0xA1 in use */
    assert(!som_raid_fits(&r.carry[0],ram));

    /* Start: villager in monster slot 0 (object 3) is kept; raiders go to 1, 2. */
    town();
    ram[0xE600]=1;ram[0xE63D]=0xC7;ram[0xE68B]=0x20;        /* villager, palette 0 */
    assert(som_raid_start(&r,ram,vram)==2&&r.active&&r.map==131);
    assert(r.slot[0]==1&&r.slot[1]==2&&ram[0xE63D]==0xC7);
    assert(som_fam_u16(ram+0xE802)==348&&som_fam_u16(ram+0xE804)==300);   /* 48 px right */
    assert(som_fam_u16(ram+0xEA02)==252);                   /* the other one left */
    assert(som_fam_u16(ram+0xE982)==60&&ram[0xE9AC]==0xFF);
    assert(vram[0xC000+0xA0*32]==0x11&&ram[0xDFA0]==0x80&&ram[0x0700+5*32]==0x80);

    /* Ticks keep tiles drawn; a death leaves one; both: repelled. */
    memset(vram+0xC000+0xA0*32,0,32);
    assert(som_raid_tick(&r,ram,vram,10)==SOM_RAID_GOING&&vram[0xC000+0xA0*32]==0x11);
    ram[0xEB80]=0xF9;
    assert(som_raid_tick(&r,ram,vram,11)==SOM_RAID_GOING&&r.slot[1]==-1);

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
    assert(som_raid_tick(&r,ram,vram,12)==SOM_RAID_REPELLED&&!r.active&&r.ended_at==12);

    /* Leaving the map ends it too. */
    town();assert(som_raid_start(&r,ram,vram)==2);
    put16(0xDC,130);assert(som_raid_tick(&r,ram,vram,20)==SOM_RAID_LEFT&&!r.active);

    /* Nothing captured or no room: no raid. */
    town();r.captured[0]=r.captured[1]=0;assert(som_raid_start(&r,ram,vram)==0&&!r.active);
    puts("som raid: ok");
    return 0;
}
