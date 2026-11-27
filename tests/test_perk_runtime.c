/* Perk runtime and time effects (no ROM):
 *   cc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_perk_runtime.c -o build/test-perk-runtime
 * Each SUPPORTED/IMPLEMENTED perk has a row in PERK_CASES below that sets up
 * the situation its description names and checks the action it must give;
 * data/perks/verified.json lists the perks whose row passes here. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "som_perk_runtime.h"
#include "som_temporal.h"

static SomPerkRuntime rt;
static SomPerkAction got[SOM_PRT_ACTIONS];
static unsigned n_got;

static unsigned id_of(const char *key){
    for(unsigned i=0;i<SOM_PERK_DEF_COUNT;i++)if(!strcmp(som_perk_defs[i].key,key))return som_perk_defs[i].id;
    fprintf(stderr,"no perk %s\n",key);assert(0);return 0;
}
static void equip(unsigned who,const char *key){
    uint16_t ids[SOM_PERK_MAX_EQUIPPED];unsigned n=som_prt_ids(&rt,who,ids);
    ids[n++]=(uint16_t)id_of(key);
    assert(som_prt_set_raw(&rt,who,ids,n)==n);
}
static SomPerkEv ev_of(unsigned type,unsigned frame){
    SomPerkEv e;memset(&e,0,sizeof(e));
    e.type=(uint8_t)type;e.who=0;e.slot=0;e.target_id=7;e.amount=40;e.hp=80;e.hp_max=100;
    e.target_hp=60;e.target_max=100;e.dist=20;e.weapon=SOM_PW_SWORD;e.frame=frame;
    return e;
}
static unsigned emit(const SomPerkEv *e){
    const unsigned f=som_prt_emit(&rt,e);
    n_got=som_prt_take(&rt,got,SOM_PRT_ACTIONS);
    return f;
}
static const SomPerkAction *find(unsigned type){
    for(unsigned i=0;i<n_got;i++)if(got[i].type==type)return &got[i];
    return NULL;
}
/* the bonus damage an ATTACK_HIT gave (DAMAGE on its own slot from the hit itself) */
static unsigned hit_bonus(void){
    for(unsigned i=0;i<n_got;i++)if(got[i].type==SOM_PACT_DAMAGE&&got[i].bonus)return got[i].amount;
    return 0;
}

/* ------------------------------------------------------------ per perk */
typedef struct {
    const char *key;
    uint8_t type;            /* the event that should set it off */
    uint32_t facts;
    int weapon, charge, combo, dist, hp, thp, still, energy, amount;
    uint8_t pre;             /* 1: a hit just before (chains) 2: weapon switch before 3: 3 hits before
                                4: an old hit (idle) 5: bullet time running then over
                                6: two hits with two other weapons */
    uint8_t repeat;          /* emit this many times (every_n) */
    uint8_t expect;          /* action type; 0xFF: a bonus on the hit; 0xFE: a temp, seen on the next hit */
    int8_t sign;             /* for 0xFF: +1 more damage, -1 a refund */
} PerkCase;
#define H SOM_PEV_ATTACK_HIT
#define K SOM_PEV_ENEMY_KILLED
#define D SOM_PEV_DAMAGE_POST
#define BONUS 0xFFu
#define TEMP 0xFEu
static const PerkCase PERK_CASES[]={
    {"swordsmanship.battle_maiden",H,SOM_PFACT_ALLY_LOW_HP_NEAR,1,0,0,20,80,60,0,0,40,0,1,BONUS,1},
    {"swordsmanship.meteor_strike",H,0,1,3,0,20,80,60,0,0,40,0,1,SOM_PACT_AREA,0},
    {"swordsmanship.valhalla",K,SOM_PFACT_TARGET_BOSS,1,0,0,20,80,0,0,0,40,0,1,TEMP,0},
    {"swordsmanship.last_stand",H,0,1,0,0,20,20,60,0,0,40,0,1,BONUS,1},
    {"martial_arts.inner_peace",SOM_PEV_FRAME_PASSIVE,SOM_PFACT_OUT_OF_COMBAT,0,0,0,20,80,60,0,0,0,0,1,SOM_PACT_HEAL,0},
    {"martial_arts.empty_hand",H,0,0,0,0,20,80,60,0,0,40,0,1,BONUS,1},
    {"martial_arts.flowing_fist",H,0,0,0,0,20,80,60,0,0,40,1,1,SOM_PACT_GAUGE,0},
    {"martial_arts.iron_palm",H,0,0,2,0,20,80,60,0,0,40,0,1,SOM_PACT_STAGGER,0},
    {"martial_arts.falling_mountain",H,SOM_PFACT_TARGET_STAGGERED,0,0,0,20,80,60,0,0,40,0,1,BONUS,1},
    {"martial_arts.qi_burst",H,0,0,0,0,20,80,60,0,0,40,0,5,SOM_PACT_AREA,0},
    {"martial_arts.rising_dragon",SOM_PEV_COMBO_FINISHER,0,0,0,3,20,80,60,0,0,40,0,1,SOM_PACT_STAGGER,0},
    {"martial_arts.reversal",SOM_PEV_PERFECT_BLOCK,0,0,0,0,20,80,60,0,0,10,1,1,SOM_PACT_DAMAGE,0},
    {"martial_arts.thunder_step",SOM_PEV_INTERCEPT,0,0,0,0,20,80,60,0,0,30,0,1,SOM_PACT_STAGGER,0},
    {"martial_arts.hundred_hands_of_time",SOM_PEV_INTERCEPT,SOM_PFACT_BULLET_TIME,0,0,0,20,80,60,0,0,30,0,1,SOM_PACT_BULLET_EXTEND,0},
    {"martial_arts.diamond_body",D,0,0,0,0,20,60,60,0,80,20,0,1,SOM_PACT_GIVE_BACK,0},
    {"ninjutsu.silent_death",H,SOM_PFACT_TARGET_UNAWARE,1,0,0,20,80,60,0,0,40,0,1,BONUS,1},
    {"ninjutsu.shadow_clone",H,0,1,0,2,20,80,60,0,0,40,0,4,SOM_PACT_DAMAGE,0},
    {"divine_arts.retribution",D,0,1,0,0,20,60,60,0,0,20,0,1,TEMP,0},
    {"divine_arts.providence",K,0,1,0,0,20,80,0,0,0,40,0,10,SOM_PACT_PARTY_HEAL,0},
    {"dark_arts.stardust",H,0,1,0,0,20,80,60,0,0,40,0,6,SOM_PACT_DAMAGE,0},
    {"summoning.pack_tactics",H,SOM_PFACT_ALLY_NEAR_TARGET,1,0,0,20,80,60,0,0,40,0,1,BONUS,1},
    {"dance.battle_rhythm",H,0,1,0,0,20,80,60,0,0,40,1,1,BONUS,1},
    {"dance.crescendo",H,0,1,0,5,20,80,60,0,0,40,0,1,BONUS,1},
    {"dance.encore",SOM_PEV_COMBO_FINISHER,0,1,0,3,20,80,60,0,0,40,0,3,SOM_PACT_DAMAGE,0},
    {"dance.grand_finale",SOM_PEV_COMBO_FINISHER,0,1,0,10,20,80,60,0,0,40,0,1,SOM_PACT_AREA,0},
    {"weapon_flow.versatility",SOM_PEV_WEAPON_SWITCH,0,2,0,0,20,80,60,0,0,40,0,1,SOM_PACT_GAUGE,0},
    {"weapon_flow.arsenal_mastery",SOM_PEV_WEAPON_SWITCH,0,2,0,0,20,80,60,0,0,40,0,1,TEMP,0},
    {"weapon_flow.chain_reaction",H,0,2,0,0,20,80,60,0,0,40,1,1,SOM_PACT_STAGGER,0},
    {"weapon_flow.arsenal_overflow",H,0,3,0,3,20,80,60,0,0,40,6,1,SOM_PACT_ENERGY,0},
    {"weapon_flow.dancing_steel",SOM_PEV_WEAPON_SWITCH,0,2,0,2,20,80,60,0,0,40,0,1,SOM_PACT_AREA,0},
    {"weapon_flow.endless_arsenal",SOM_PEV_WEAPON_SWITCH,0,2,0,0,20,80,60,0,0,40,0,1,SOM_PACT_GAUGE,0},
    {"roguecraft.backstab",H,SOM_PFACT_BEHIND,1,0,0,20,80,60,0,0,40,0,1,BONUS,1},
    {"roguecraft.opportunist",H,SOM_PFACT_TARGET_RECOVERING,1,0,0,20,80,60,0,0,40,0,1,BONUS,1},
    {"roguecraft.ambush",H,0,1,0,0,20,80,60,0,0,40,0,1,SOM_PACT_STAGGER,0},
    {"roguecraft.hemorrhage",H,0,1,0,0,20,80,60,0,0,40,3,1,SOM_PACT_DAMAGE,0},
    {"roguecraft.knife_in_the_dark",H,SOM_PFACT_BEHIND,1,0,0,20,80,60,0,0,40,0,1,BONUS,1},
    {"bushido.iaijutsu_focus",H,0,1,0,0,20,80,60,0,0,40,4,1,BONUS,1},
    {"bushido.deaths_edge",H,0,1,0,0,20,80,20,0,0,40,0,1,BONUS,1},
    {"bushido.zanshin",K,0,1,0,0,20,80,0,0,0,40,0,1,SOM_PACT_GAUGE,0},
    {"bushido.duelist",H,SOM_PFACT_TARGET_ISOLATED,1,0,0,20,80,60,0,0,40,0,1,BONUS,1},
    {"bushido.unwavering_spirit",D,SOM_PFACT_CHARGING,1,0,0,20,60,60,0,0,20,0,1,SOM_PACT_GIVE_BACK,0},
    {"bushido.bushido",SOM_PEV_PERFECT_BLOCK,0,1,0,0,20,80,60,0,0,10,0,1,TEMP,0},
    {"bushido.severance",H,0,1,2,0,20,80,60,0,0,40,0,1,SOM_PACT_DAMAGE,0},
    {"bushido.tsubame_gaeshi",SOM_PEV_COUNTER,0,1,0,0,20,80,60,0,0,30,0,1,SOM_PACT_DAMAGE,0},
    {"bushido.sword_saint",H,0,1,0,0,20,80,60,0,0,40,0,1,BONUS,1},
    {"bushido.kirisute",K,0,1,2,0,20,80,0,0,0,40,0,1,SOM_PACT_DAMAGE,0},
    {"bushido.okuri_kaze",H,SOM_PFACT_BULLET_TIME,1,0,0,20,80,60,0,0,40,5,1,SOM_PACT_DAMAGE,0},
    {"bushido.ichi_no_tachi",H,0,1,0,0,20,80,60,0,0,40,0,1,BONUS,1},
    {"archery.quickdraw",SOM_PEV_WEAPON_SWITCH,0,5,0,0,20,80,60,0,0,40,0,1,SOM_PACT_GAUGE,0},
    {"archery.deadeye",H,0,5,0,0,90,80,60,0,0,40,0,1,BONUS,1},
    {"archery.last_bullet",H,0,5,0,0,90,80,60,0,0,40,0,6,BONUS,1},
    {"archery.gun_kata",H,0,5,0,0,20,80,60,0,0,40,0,1,SOM_PACT_STAGGER,0},
    {"archery.gun_and_blade",H,0,5,0,0,20,80,60,0,0,40,1,1,BONUS,1},
    {"archery.ricochet",H,0,5,0,0,90,80,60,0,0,40,0,1,SOM_PACT_DAMAGE,0},
    {"archery.piercing_shot",H,0,5,0,0,90,80,60,0,0,40,0,1,SOM_PACT_DAMAGE,0},
    {"vampire_hunting.patient_hunter",H,0,4,0,0,20,80,60,100,0,40,0,1,BONUS,1},
    {"vampire_hunting.stake_through_the_heart",H,SOM_PFACT_TARGET_UNDEAD,4,0,0,20,80,10,0,0,40,0,1,SOM_PACT_EXECUTE,0},
    {"vampire_hunting.night_hunter",H,SOM_PFACT_TARGET_UNDEAD,4,0,0,20,80,60,0,0,40,0,1,BONUS,1},
    {"necromancy.soul_harvest",K,0,1,0,0,20,80,0,0,0,40,0,1,SOM_PACT_HEAL,0},
    {"necromancy.bone_armour",K,0,1,0,0,20,80,0,0,0,40,0,1,0,0},
    {"necromancy.death_echo",K,0,1,0,0,20,80,0,0,0,40,0,1,SOM_PACT_AREA,0},
    {"necromancy.spectral_blade",H,0,1,0,0,20,80,60,0,0,40,0,5,SOM_PACT_DAMAGE,0},
    {"artifice.reinforced_armour",D,0,1,0,0,20,60,60,0,0,40,0,1,SOM_PACT_GIVE_BACK,0},
    {"psionics.psychic_shield",D,0,1,0,0,20,60,60,0,0,40,0,1,SOM_PACT_GIVE_BACK,0},
    {"psionics.chrono_counter",SOM_PEV_PERFECT_BLOCK,0,1,0,0,20,80,60,0,0,10,0,1,SOM_PACT_BULLET,0},
    {"psionics.temporal_distortion",H,0,0,0,0,20,80,60,0,0,40,6,1,SOM_PACT_TIME,0},
    {"psionics.psychic_acceleration",SOM_PEV_FRAME_PASSIVE,SOM_PFACT_OWN_TIME_EFFECT,0,0,0,20,80,60,0,0,0,0,1,SOM_PACT_GAUGE,0},
    {"psionics.time_fracture",SOM_PEV_COMBO_FINISHER,0,0,0,3,20,80,60,0,0,40,0,1,SOM_PACT_TIME,0},
    {"universal.full_force",H,0,1,0,0,20,100,60,0,0,40,0,1,BONUS,1},
    {"universal.second_wind",D,0,1,0,0,20,15,60,0,0,40,0,1,SOM_PACT_HEAL,0},
    {"universal.clean_fight",SOM_PEV_COMBAT_END,0,1,0,0,20,80,60,0,0,0,0,1,TEMP,0},
};
#define N_CASES (sizeof(PERK_CASES)/sizeof(PERK_CASES[0]))

static int run_case(const PerkCase *c,int with_perk){
    som_prt_init(&rt);
    if(with_perk)equip(0,c->key);
    uint32_t f=1000;
    SomPerkEv e=ev_of(c->type,f);
    e.facts=c->facts;e.weapon=(uint8_t)c->weapon;e.charge=(uint8_t)c->charge;e.combo=(uint8_t)c->combo;
    e.dist=(uint16_t)c->dist;e.hp=(uint16_t)c->hp;e.target_hp=(uint16_t)c->thp;e.still_frames=(uint16_t)c->still;
    e.energy_pct=(uint8_t)c->energy;e.amount=(uint16_t)c->amount;
    if(c->type==K)e.target_hp=0;
    if(c->pre==1){SomPerkEv p=e;p.type=H;p.frame=f-20;p.weapon=(uint8_t)(c->weapon==5?SOM_PW_SWORD:c->weapon==2?SOM_PW_SWORD:c->weapon);
                  p.facts=0;emit(&p);}
    if(c->pre==3)for(unsigned i=0;i<3u;i++){SomPerkEv p=e;p.type=H;p.weapon=(uint8_t)i;p.frame=f-30+i*10u;p.facts=0;
                                             p.combo=(uint8_t)(i+1u);emit(&p);}
    if(c->pre==6)for(unsigned i=0;i<2u;i++){SomPerkEv p=e;p.type=H;p.weapon=(uint8_t)i;p.frame=f-20+i*10u;p.facts=0;
                                             p.combo=(uint8_t)(i+1u);emit(&p);}
    if(c->pre==4){SomPerkEv p=e;p.type=H;p.frame=f-400;p.facts=0;emit(&p);}
    unsigned fired=0;
    for(unsigned r=0;r<(c->repeat?c->repeat:1u);r++){
        e.frame=f+r*2u;
        if(c->type==K)e.target_id=(uint8_t)(7u+r);              /* ten different kills */
        fired+=emit(&e);
        if(c->expect==SOM_PACT_DAMAGE&&c->pre==5){             /* held back until bullet time ends */
            assert(!find(SOM_PACT_DAMAGE)||find(SOM_PACT_DAMAGE)->bonus);
            som_prt_tick(&rt,e.frame+1,1);n_got=som_prt_take(&rt,got,SOM_PRT_ACTIONS);
            assert(!find(SOM_PACT_DAMAGE));
            som_prt_tick(&rt,e.frame+2,0);n_got=som_prt_take(&rt,got,SOM_PRT_ACTIONS);
        }
        if(r+1u<(c->repeat?c->repeat:1u)&&c->expect!=BONUS&&find(c->expect)&&with_perk)return -1;  /* too soon */
    }
    if(c->expect==BONUS){
        int b=(int)hit_bonus();
        if(!b)for(unsigned i=0;i<n_got;i++)if(got[i].type==SOM_PACT_REFUND)b=-(int)got[i].amount;
        return c->sign>0?b>0:b<0;
    }
    if(c->expect==TEMP){                                       /* seen on the next hit */
        SomPerkEv h=ev_of(H,e.frame+5);h.weapon=(uint8_t)c->weapon;
        emit(&h);
        return hit_bonus()>0;
    }
    if(!c->expect){                                            /* a shield pool: the next hurt gives back */
        SomPerkEv h=ev_of(D,e.frame+5);
        emit(&h);
        return find(SOM_PACT_GIVE_BACK)!=NULL;
    }
    (void)fired;
    if(c->expect==SOM_PACT_DAMAGE){
        for(unsigned i=0;i<n_got;i++)if(got[i].type==SOM_PACT_DAMAGE&&!got[i].bonus)return 1;
        som_prt_tick(&rt,e.frame+1000u,0);n_got=som_prt_take(&rt,got,SOM_PRT_ACTIONS);   /* delayed */
        for(unsigned i=0;i<n_got;i++)if(got[i].type==SOM_PACT_DAMAGE&&!got[i].bonus)return 1;
        return 0;
    }
    return find(c->expect)!=NULL;
}

static void test_cases(void){
    unsigned checked=0;
    for(unsigned i=0;i<N_CASES;i++){
        const PerkCase *c=&PERK_CASES[i];
        const int with=run_case(c,1),without=run_case(c,0);
        if(with!=1||without!=0){
            fprintf(stderr,"FAIL %s: with the perk %d, without %d\n",c->key,with,without);
            assert(0);
        }
        printf("ok %s\n",c->key);
        checked++;
    }
    /* every perk the engine runs has a row here */
    for(unsigned i=0;i<SOM_PERK_DEF_COUNT;i++){
        const SomPerkDef *d=&som_perk_defs[i];
        if(d->status!=SOM_PSTAT_IMPLEMENTED&&d->status!=SOM_PSTAT_SUPPORTED_PENDING_TEST)continue;
        int row=0;for(unsigned k=0;k<N_CASES;k++)if(!strcmp(PERK_CASES[k].key,d->key))row=1;
        if(!row){fprintf(stderr,"no test row for %s\n",d->key);assert(0);}
    }
    printf("%u perk rows\n",checked);
}

/* ------------------------------------------------------------ rules */
static void test_registry(void){
    for(unsigned i=1;i<SOM_PERK_DEF_COUNT;i++)assert(som_perk_defs[i-1].id<som_perk_defs[i].id);
    for(unsigned i=0;i<SOM_PERK_DEF_COUNT;i++)assert(som_perk_def_by_id(som_perk_defs[i].id)==&som_perk_defs[i]);
    assert(!som_perk_def_by_id(0)&&!som_perk_def_by_id(65535));
    assert(SOM_PERK_DEF_COUNT>=200u);
}
static void test_cooldown_and_charges(void){
    som_prt_init(&rt);equip(0,"bushido.bushido");
    SomPerkEv b=ev_of(SOM_PEV_PERFECT_BLOCK,100);
    assert(emit(&b)==1);
    b.frame=130;assert(emit(&b)==0);                      /* 60-frame cooldown */
    SomPerkEv h=ev_of(H,140);emit(&h);
    assert(hit_bonus()==40u*35u/100u);                     /* the one charge */
    h.frame=150;emit(&h);assert(hit_bonus()==0);           /* spent */
    b.frame=200;assert(emit(&b)==1);
    h.frame=500;emit(&h);assert(hit_bonus()==0);           /* 3 s later: expired */
}
static void test_stacks(void){
    som_prt_init(&rt);equip(0,"bushido.sword_saint");
    SomPerkEv h=ev_of(H,100);
    const unsigned want[7]={1,2,3,4,5,5,5};
    for(unsigned i=0;i<7u;i++){h.frame=100+i*10u;emit(&h);assert(hit_bonus()==40u*4u*want[i]/100u);}
    SomPerkEv d=ev_of(D,200);emit(&d);                    /* hurt: cleared */
    h.frame=210;emit(&h);assert(hit_bonus()==40u*4u/100u);
    h.weapon=SOM_PW_AXE;h.frame=220;emit(&h);assert(hit_bonus()==0);   /* sword only */
}
static void test_ownership_and_order(void){
    som_prt_init(&rt);
    equip(1,"bushido.deaths_edge");
    SomPerkEv h=ev_of(H,100);h.target_hp=10;
    emit(&h);assert(hit_bonus()==0);                       /* the girl's perk, the boy's hit */
    h.who=1;emit(&h);assert(hit_bonus()==10&&got[0].who==1&&got[0].perk_id==id_of("bushido.deaths_edge"));
    equip(0,"martial_arts.qi_burst");equip(0,"dark_arts.stardust");
    assert(rt.hero[1].n==1&&rt.hero[0].n==2);
    /* deterministic: same events give the same actions in the same order */
    SomPerkAction a1[SOM_PRT_ACTIONS],a2[SOM_PRT_ACTIONS];unsigned n1=0,n2=0;
    for(int pass=0;pass<2;pass++){
        som_prt_init(&rt);equip(0,"martial_arts.qi_burst");equip(0,"dark_arts.stardust");
        SomPerkAction *out=pass?a2:a1;unsigned *n=pass?&n2:&n1;
        for(unsigned i=0;i<30u;i++){SomPerkEv e=ev_of(H,100+i);som_prt_emit(&rt,&e);
            unsigned k=som_prt_take(&rt,out+*n,SOM_PRT_ACTIONS-*n);*n+=k;}
    }
    assert(n1==n2&&n1>0&&!memcmp(a1,a2,n1*sizeof(a1[0])));
}
static void test_recursion_and_budgets(void){
    som_prt_init(&rt);equip(0,"necromancy.death_echo");
    SomPerkEv k=ev_of(K,100);k.target_hp=0;
    assert(emit(&k)==1&&find(SOM_PACT_AREA));
    k.slot=1;k.depth=1;k.frame=101;                         /* a kill by the echo itself */
    assert(emit(&k)==0&&!find(SOM_PACT_AREA));
    k.slot=2;k.depth=SOM_PRT_MAX_DEPTH+1u;k.frame=102;assert(emit(&k)==0);
    /* no double reward for one monster */
    k.slot=0;k.depth=0;k.frame=110;assert(emit(&k)==0);
    /* budgets: 12 hit perks, many events in one frame */
    som_prt_init(&rt);
    const char *many[]={"bushido.deaths_edge","bushido.duelist","martial_arts.empty_hand","roguecraft.backstab",
                        "universal.full_force","summoning.pack_tactics","ninjutsu.silent_death","dance.crescendo",
                        "vampire_hunting.night_hunter","martial_arts.falling_mountain","archery.deadeye","bushido.ichi_no_tachi"};
    for(unsigned i=0;i<12u;i++)equip(0,many[i]);
    assert(rt.hero[0].n==SOM_PERK_MAX_EQUIPPED);
    unsigned total=0;
    for(unsigned i=0;i<10u;i++){
        SomPerkEv h=ev_of(H,500);h.facts=0xFFFFFFFFu;h.target_hp=1;h.combo=5;h.dist=90;h.hp=100;h.weapon=SOM_PW_GLOVE;
        total+=som_prt_emit(&rt,&h);
    }
    assert(total<=SOM_PRT_FRAME_BUDGET&&rt.skipped_budget>0);
    assert(rt.n_act<=SOM_PRT_ACTIONS);
    som_prt_take(&rt,NULL,0);
}
static void test_bounded_queues(void){
    som_prt_init(&rt);equip(0,"roguecraft.hemorrhage");
    for(unsigned i=0;i<60u;i++){                              /* 3 ticks each, 16 places */
        SomPerkEv h=ev_of(H,1000+i*200u);
        for(unsigned j=0;j<3u;j++){h.frame+=10;som_prt_emit(&rt,&h);}
        som_prt_take(&rt,NULL,0);
    }
    unsigned used=0;for(unsigned i=0;i<SOM_PRT_DELAYED;i++)used+=rt.delayed[i].used!=0;
    assert(used==SOM_PRT_DELAYED&&rt.dropped_delayed>0);
    som_prt_tick(&rt,1u<<30,0);
    assert(rt.n_act<=SOM_PRT_ACTIONS);
    som_prt_take(&rt,NULL,0);
    for(unsigned i=0;i<SOM_PRT_DELAYED;i++)assert(!rt.delayed[i].used);
    /* the action queue never grows: more than 32 actions are dropped */
    for(unsigned i=0;i<40u;i++){SomPerkAction a;memset(&a,0,sizeof(a));a.type=SOM_PACT_HEAL;som_prt_push(&rt,&a);}
    assert(rt.n_act==SOM_PRT_ACTIONS&&rt.dropped_actions>=8u);
    som_prt_take(&rt,NULL,0);
    /* the slot changed hands: its delayed damage is void */
    som_prt_init(&rt);equip(0,"roguecraft.hemorrhage");
    SomPerkEv h=ev_of(H,100);for(unsigned j=0;j<3u;j++){h.frame+=10;som_prt_emit(&rt,&h);}
    som_prt_forget_slot(&rt,0);som_prt_tick(&rt,100000,0);n_got=som_prt_take(&rt,got,SOM_PRT_ACTIONS);
    assert(!find(SOM_PACT_DAMAGE));
}
static void test_flush(void){
    som_prt_init(&rt);equip(0,"bushido.bushido");equip(0,"necromancy.bone_armour");
    SomPerkEv b=ev_of(SOM_PEV_PERFECT_BLOCK,100);emit(&b);
    SomPerkEv k=ev_of(K,110);k.target_hp=0;emit(&k);
    assert(rt.hero[0].shield>0);
    som_prt_flush(&rt);                                       /* a state load */
    assert(rt.hero[0].n==2&&!rt.hero[0].shield);
    SomPerkEv h=ev_of(H,120);emit(&h);assert(hit_bonus()==0);
    b.frame=121;assert(emit(&b)==1);                          /* cooldowns reset too */
}
static void test_execute_never_on_bosses(void){
    som_prt_init(&rt);equip(0,"vampire_hunting.stake_through_the_heart");
    SomPerkEv h=ev_of(H,100);h.facts=SOM_PFACT_TARGET_UNDEAD|SOM_PFACT_TARGET_BOSS;h.target_hp=5;
    emit(&h);assert(!find(SOM_PACT_EXECUTE));
    h.facts=SOM_PFACT_TARGET_UNDEAD;h.target_hp=30;emit(&h);assert(!find(SOM_PACT_EXECUTE));  /* 30% */
}

/* ------------------------------------------------------------ time */
static void test_bullet_time_unchanged(void){
    /* the old som_bullet_world: frames--, hold when odd */
    unsigned old=150;SomTemporal t;som_time_reset(&t);som_time_world(&t,150);
    for(unsigned f=0;f<160u;f++){
        int was=0;if(old){old--;was=(old&1u)!=0;}
        const int now=som_time_active(&t)?som_time_hold(&t,0,7):0;
        assert(was==now);
        if(som_time_active(&t))som_time_tick(&t);
    }
    assert(!t.world);
}
static void test_slot_time(void){
    SomTemporal t;som_time_reset(&t);
    assert(som_time_apply(&t,1,50,100,0,9,0));
    unsigned held=0;for(unsigned f=0;f<100u;f++){held+=(unsigned)som_time_hold(&t,1,9);som_time_tick(&t);}
    assert(held==50);                                         /* half speed: every other frame */
    assert(!som_time_hold(&t,1,9));                           /* over */
    /* the slowest wins, a weaker one does not cut it short */
    som_time_reset(&t);
    assert(som_time_apply(&t,0,0,60,1,9,0));
    assert(!som_time_apply(&t,0,50,200,0,9,0));
    assert(som_time_frozen(&t,0,9)&&som_time_owned(&t,1)&&!som_time_owned(&t,0));
    /* a freeze is capped, then the monster cannot be frozen for a while */
    som_time_reset(&t);
    assert(som_time_apply(&t,2,0,1000,0,9,0)&&t.slot[2].frames==SOM_TIME_FREEZE_MAX);
    unsigned frozen=0;for(unsigned f=0;f<400u;f++){frozen+=(unsigned)som_time_hold(&t,2,9);som_time_tick(&t);
        if(f==SOM_TIME_FREEZE_MAX+5u)assert(!som_time_apply(&t,2,0,60,0,9,0));}
    assert(frozen==SOM_TIME_FREEZE_MAX);
    assert(som_time_apply(&t,2,0,30,0,9,0));                 /* immunity over */
    /* bosses: a third of a second at most */
    som_time_reset(&t);assert(som_time_apply(&t,0,0,90,0,0x60,1)&&t.slot[0].frames==SOM_TIME_BOSS_FREEZE);
    /* a recycled slot loses the effect */
    som_time_reset(&t);som_time_apply(&t,0,0,60,0,9,0);
    assert(!som_time_hold(&t,0,10)&&som_time_speed(&t,0,10)==100u);
    /* bullet time on top of a half speed stays half; extending is capped */
    som_time_reset(&t);som_time_world(&t,60);
    assert(som_time_world_extend(&t,30,100)==30&&som_time_world_extend(&t,30,100)==10&&t.world==100);
    assert(!som_time_world_extend(&(SomTemporal){0},30,100));
}

int main(void){
    test_registry();
    test_cooldown_and_charges();
    test_stacks();
    test_ownership_and_order();
    test_recursion_and_budgets();
    test_bounded_queues();
    test_flush();
    test_execute_never_on_bosses();
    test_bullet_time_unchanged();
    test_slot_time();
    test_cases();
    printf("PASS: perk runtime, %u perks in the registry\n",SOM_PERK_DEF_COUNT);
    return 0;
}
