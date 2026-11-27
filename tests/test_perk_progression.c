/* Perk unlocks, capacity and loadouts (no ROM):
 *   cc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_perk_progression.c -o build/test-perk-progression */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "som_perk_runtime.h"
#include "som_disciplines.h"

static unsigned id_of(const char *key){
    for(unsigned i=0;i<SOM_PERK_DEF_COUNT;i++)if(!strcmp(som_perk_defs[i].key,key))return som_perk_defs[i].id;
    assert(0);return 0;
}
/* The view the proxy builds (som_perk_rt_view) from the class menu state. */
static SomPerkUnlockView view_of(const SomDisciplines *d,unsigned who,unsigned hero_level){
    SomPerkUnlockView v;memset(&v,0,sizeof(v));
    v.level=d->level;v.classless=d->classless;v.held=d->held[who];v.hero_level=(uint8_t)hero_level;
    return v;
}
/* a perk the engine runs and one it does not (yet) */
static const SomPerkDef *pick(int runnable,unsigned max_cost){
    for(unsigned i=0;i<SOM_PERK_DEF_COUNT;i++){
        const SomPerkDef *d=&som_perk_defs[i];
        const int ok=d->status==SOM_PSTAT_IMPLEMENTED||d->status==SOM_PSTAT_SUPPORTED_PENDING_TEST;
        if(ok==runnable&&d->cost<=max_cost&&!(d->flags&SOM_PDF_UNIVERSAL)&&!d->prereq[0])return d;
    }
    assert(0);return NULL;
}

static void test_capacity(void){
    assert(som_prt_capacity(1)==6u&&som_prt_capacity(24)==12u&&som_prt_capacity(48)==18u);
    assert(som_prt_capacity(96)==30u&&som_prt_capacity(255)==SOM_PERK_CAPACITY_MAX);
}
static void test_unlocks(void){
    SomDisciplines d;memset(&d,0,sizeof(d));
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++)d.level[i]=1;
    const SomPerkDef *deaths=som_perk_def_by_id(id_of("bushido.deaths_edge"));      /* novice */
    const SomPerkDef *bushido=som_perk_def_by_id(id_of("bushido.bushido"));         /* adept, 10 */
    const SomPerkDef *okuri=som_perk_def_by_id(id_of("bushido.okuri_kaze"));        /* needs Bushido */
    SomPerkUnlockView v=view_of(&d,0,10);
    assert(som_prt_unlocked(deaths,&v)&&!som_prt_unlocked(bushido,&v));
    d.level[SOM_DISC_BUSHIDO]=10;v=view_of(&d,0,10);
    assert(som_prt_unlocked(bushido,&v)&&!som_prt_unlocked(okuri,&v));
    d.level[SOM_DISC_BUSHIDO]=28;v=view_of(&d,0,10);
    assert(som_prt_unlocked(okuri,&v));
    /* classless games: the hero must have had the class (old saves: open) */
    d.classless=1;v=view_of(&d,0,10);
    assert(!som_prt_unlocked(deaths,&v));
    d.held[1]=(uint16_t)(1u<<SOM_DISC_BUSHIDO);
    assert(!som_prt_unlocked(deaths,&v)&&som_prt_unlocked(deaths,&(SomPerkUnlockView){d.level,d.held[1],1,10}));
    /* universal perks follow the hero's own level */
    const SomPerkDef *wind=som_perk_def_by_id(id_of("universal.second_wind"));      /* level 5 */
    assert(!som_prt_unlocked(wind,&(SomPerkUnlockView){d.level,0,1,4})&&
           som_prt_unlocked(wind,&(SomPerkUnlockView){d.level,0,1,5}));
    /* switching the active class changes nothing: the view never reads it */
    d.classless=0;d.selected=SOM_DISC_BUSHIDO;
    SomPerkUnlockView a=view_of(&d,0,10);d.selected=SOM_DISC_MARTIAL_ARTS;
    SomPerkUnlockView b=view_of(&d,0,10);
    for(unsigned i=0;i<SOM_PERK_DEF_COUNT;i++)
        assert(som_prt_unlocked(&som_perk_defs[i],&a)==som_prt_unlocked(&som_perk_defs[i],&b));
}
static void test_equip_rules(void){
    SomDisciplines d;memset(&d,0,sizeof(d));
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++)d.level[i]=100;
    SomPerkRuntime rt;som_prt_init(&rt);
    const SomPerkUnlockView v=view_of(&d,0,1);              /* capacity 6 */
    const SomPerkDef *off=pick(0,8),*on=pick(1,2);
    /* only perks the engine runs, and only once a test passed or the switch is on */
    assert(som_prt_equip(&rt,0,off->id,&v)==SOM_PEQ_UNAVAILABLE);
    if(on->status==SOM_PSTAT_SUPPORTED_PENDING_TEST){
        assert(som_prt_equip(&rt,0,on->id,&v)==SOM_PEQ_UNAVAILABLE);
        rt.pending_ok=1;
    }
    assert(som_prt_equip(&rt,0,on->id,&v)==SOM_PEQ_OK);
    assert(som_prt_equip(&rt,0,on->id,&v)==SOM_PEQ_DUPLICATE);
    assert(som_prt_equip(&rt,0,99,&v)==SOM_PEQ_UNKNOWN);
    rt.pending_ok=1;
    /* capacity: fill to 6 */
    unsigned used=on->cost;
    for(unsigned i=0;i<SOM_PERK_DEF_COUNT&&used<6u;i++){
        const SomPerkDef *p=&som_perk_defs[i];
        if(p->status!=SOM_PSTAT_SUPPORTED_PENDING_TEST&&p->status!=SOM_PSTAT_IMPLEMENTED)continue;
        if(!som_prt_unlocked(p,&v)||som_prt_has(&rt,0,p->id))continue;
        const int r=som_prt_equip(&rt,0,p->id,&v);
        if(used+p->cost<=6u){assert(r==SOM_PEQ_OK);used+=p->cost;}
        else assert(r==SOM_PEQ_CAPACITY);
    }
    assert(som_prt_used(&rt,0)==used&&used<=6u);
    const SomPerkDef *one=NULL;
    for(unsigned i=0;i<SOM_PERK_DEF_COUNT&&!one;i++){
        const SomPerkDef *p=&som_perk_defs[i];
        if((p->status==SOM_PSTAT_SUPPORTED_PENDING_TEST||p->status==SOM_PSTAT_IMPLEMENTED)&&
           !som_prt_has(&rt,0,p->id)&&som_prt_unlocked(p,&v))one=p;
    }
    if(used==6u)assert(som_prt_equip(&rt,0,one->id,&v)==SOM_PEQ_CAPACITY);
    /* a bigger hero has room */
    assert(som_prt_equip(&rt,0,one->id,&(SomPerkUnlockView){d.level,0,0,99})==SOM_PEQ_OK);
    /* exclusive groups: two finisher pulls never together */
    SomPerkRuntime g;som_prt_init(&g);g.pending_ok=1;
    const uint16_t well=(uint16_t)id_of("psionics.gravity_well");
    assert(som_prt_set_raw(&g,0,&well,1)==1);
    assert(som_prt_check(&g,0,id_of("dark_arts.event_horizon"),&v)==SOM_PEQ_GROUP);
    assert(som_prt_check(&g,0,id_of("psionics.temporal_field"),&v)==SOM_PEQ_GROUP);
    /* unequip */
    assert(som_prt_unequip(&rt,0,on->id)&&!som_prt_has(&rt,0,on->id)&&!som_prt_unequip(&rt,0,on->id));
}
static void test_heroes_independent(void){
    SomDisciplines d;memset(&d,0,sizeof(d));
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++)d.level[i]=100;
    SomPerkRuntime rt;som_prt_init(&rt);rt.pending_ok=1;
    const unsigned a=id_of("bushido.deaths_edge"),b=id_of("martial_arts.empty_hand"),c=id_of("archery.deadeye");
    assert(som_prt_equip(&rt,0,a,&(SomPerkUnlockView){d.level,0,0,1})==SOM_PEQ_OK);
    assert(som_prt_equip(&rt,1,b,&(SomPerkUnlockView){d.level,0,0,1})==SOM_PEQ_OK);
    assert(som_prt_equip(&rt,2,c,&(SomPerkUnlockView){d.level,0,0,1})==SOM_PEQ_OK);
    assert(som_prt_equip(&rt,2,a,&(SomPerkUnlockView){d.level,0,0,1})==SOM_PEQ_OK);   /* own copy */
    assert(som_prt_has(&rt,0,a)&&!som_prt_has(&rt,0,b)&&som_prt_has(&rt,1,b)&&som_prt_has(&rt,2,a));
    som_prt_clear_hero(&rt,1);
    assert(!rt.hero[1].n&&rt.hero[0].n==1&&rt.hero[2].n==2);
    /* an event of hero 2 never uses hero 0's state */
    SomPerkEv e;memset(&e,0,sizeof(e));e.type=SOM_PEV_ATTACK_HIT;e.who=2;e.slot=0;e.amount=40;
    e.target_hp=5;e.target_max=100;e.hp=50;e.hp_max=100;e.frame=10;e.dist=90;e.weapon=SOM_PW_BOW;
    som_prt_emit(&rt,&e);
    assert(rt.hero[2].last_hit_frame==10u&&!rt.hero[0].last_hit_frame);
}
static void test_revalidate(void){
    SomDisciplines d;memset(&d,0,sizeof(d));
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++)d.level[i]=50;
    SomPerkRuntime rt;som_prt_init(&rt);rt.pending_ok=1;
    const uint16_t ids[4]={(uint16_t)id_of("bushido.bushido"),(uint16_t)id_of("bushido.deaths_edge"),
                           (uint16_t)id_of("bushido.okuri_kaze"),1};          /* 1: no such perk */
    assert(som_prt_set_raw(&rt,0,ids,4)==3);
    d.level[SOM_DISC_BUSHIDO]=12;                            /* another save: lower level */
    const unsigned dropped=som_prt_revalidate(&rt,0,&(SomPerkUnlockView){d.level,0,0,30});
    assert(dropped==1&&som_prt_has(&rt,0,ids[0])&&som_prt_has(&rt,0,ids[1])&&!som_prt_has(&rt,0,ids[2]));
    /* over capacity after a load: the last ones go */
    assert(som_prt_revalidate(&rt,0,&(SomPerkUnlockView){d.level,0,0,1})==0);
    assert(som_prt_used(&rt,0)<=som_prt_capacity(1));
    rt.pending_ok=0;
    const unsigned left=rt.hero[0].n;
    const unsigned gone=som_prt_revalidate(&rt,0,&(SomPerkUnlockView){d.level,0,0,30});
    for(unsigned e=0;e<rt.hero[0].n;e++)assert(som_prt_def(&rt.hero[0],e)->status==SOM_PSTAT_IMPLEMENTED);
    assert(gone+rt.hero[0].n==left);
}
static void test_ai_tactics(void){
    SomPerkRuntime rt;som_prt_init(&rt);
    const uint16_t b=(uint16_t)id_of("bushido.bushido"),s=(uint16_t)id_of("roguecraft.backstab");
    som_prt_set_raw(&rt,1,&b,1);som_prt_set_raw(&rt,2,&s,1);
    assert(som_prt_tactic(&rt,1,SOM_PAI_VALUE_BLOCK)&&!som_prt_tactic(&rt,0,SOM_PAI_VALUE_BLOCK));
    assert(som_prt_tactic(&rt,2,SOM_PAI_SEEK_REAR));
}

int main(void){
    test_capacity();
    test_unlocks();
    test_equip_rules();
    test_heroes_independent();
    test_revalidate();
    test_ai_tactics();
    printf("PASS: perk unlocks, capacity, loadouts, per-hero state, AI tactics\n");
    return 0;
}
