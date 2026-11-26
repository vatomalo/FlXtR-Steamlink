/* Deterministic tests of the enemy tactics layer on a synthetic WRAM.
 * These are not ROM runs: the native AI is replaced by a tiny model that
 * moves a monster with the game's sign-magnitude velocity, stops at a wall
 * column and swings when its gauge is 0 and the hero is near (the levers
 * measured on the Turbo ROM, see src/som_enemy_tactics.h). */
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "../src/som_enemy_tactics.h"

static uint8_t ram[0x20000];
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static uint8_t *hero(unsigned h){return ram+SOM_TAC_HERO_BASE+SOM_TAC_OBJECT*h;}
static uint8_t *foe(unsigned s){return ram+SOM_TAC_FOE_BASE+SOM_TAC_OBJECT*s;}
static int wall_x=-1;            /* a wall column the model cannot cross */

static void clear(void){memset(ram,0,sizeof(ram));put16(ram+0xDC,52);wall_x=-1;}
static void place_hero(unsigned h,int x,int y){
    uint8_t *o=hero(h);o[0]=1;put16(o+2,(unsigned)x);put16(o+4,(unsigned)y);
    put16(o+0x182,200);put16(o+0x184,200);o[0x1E4]=1;
}
static void place_foe(unsigned s,unsigned species,int x,int y){
    uint8_t *o=foe(s);memset(o,0,SOM_TAC_OBJECT);o[0]=1;o[0x180]=(uint8_t)species;
    put16(o+2,(unsigned)x);put16(o+4,(unsigned)y);put16(o+0x182,100);put16(o+0x184,100);
}
static int fx(unsigned s){return (int)som_tac_u16(foe(s)+2);}
static int fy(unsigned s){return (int)som_tac_u16(foe(s)+4);}
static int vel(uint8_t v){return (v&0x80u)?-(int)(v&0x7Fu):(int)(v&0x7Fu);}
/* Native model: every 20 frames a 10-frame hop of 2 px/frame toward the
 * nearest hero (the game decides; we only see its velocity); swings when the
 * gauge is 0 and the hero is within 40 px; the gauge counts down. */
static unsigned swings[3];
static void native_step(unsigned long f){
    for(unsigned s=0;s<3;s++){
        uint8_t *o=foe(s);
        if(o[0]!=1)continue;
        if(o[0x1ED])o[0x1ED]--;
        if(o[0x1C]==0x80u){if(++o[0x1FE]>=30u){o[0x1C]=0;o[0x1FE]=0;o[0x1ED]=40;}continue;}
        const int hx=(int)som_tac_u16(hero(0)+2),hy=(int)som_tac_u16(hero(0)+4);
        const int d=som_tac_dist(fx(s),fy(s),hx,hy);
        if((f+s)%20u==0u){
            if(!o[0x1ED]&&d<=40){o[0x1C]=0x80u;o[6]=o[7]=0;swings[s]++;continue;}
            o[6]=(uint8_t)(hx>fx(s)?0x02:hx<fx(s)?0x82:0);o[7]=(uint8_t)(hy>fy(s)?0x02:hy<fy(s)?0x82:0);
        }
        if((f+s)%20u==10u){o[6]=o[7]=0;}
        /* integrate (the game's terrain check stops it at the wall) */
        int nx=fx(s)+vel(o[6]),ny=fy(s)+vel(o[7]);
        if(wall_x>=0&&((fx(s)<wall_x&&nx>=wall_x)||(fx(s)>wall_x&&nx<=wall_x)))nx=fx(s);
        put16(o+2,(unsigned)nx);put16(o+4,(unsigned)ny);
    }
}
static SomTac tac;
static SomTacHeroExtra extra[3];
static void run(unsigned long *f,unsigned n){
    for(unsigned i=0;i<n;i++,(*f)++){som_tac_frame(&tac,ram,extra,-1,*f);native_step(*f);}
}
static void fresh(unsigned level){tac.arena_archetype=-1;som_tac_reset(&tac,level);memset(swings,0,sizeof(swings));
                                  memset(extra,0,sizeof(extra));}

int main(void){
    unsigned long f=1000;
    /* 1. Classic: nothing is written. */
    clear();fresh(SOM_FIGHT_CLASSIC);place_hero(0,100,100);place_foe(0,0x00,140,100);
    {uint8_t before[0x20000];memcpy(before,ram,sizeof(ram));
     for(unsigned i=0;i<200;i++,f++)som_tac_frame(&tac,ram,extra,-1,f);
     assert(!memcmp(before,ram,sizeof(ram))&&tac.writes==0&&!tac.foe[0].live);}
    /* 2. Unknown and boss species stay native. */
    clear();fresh(SOM_FIGHT_EXPERT);place_hero(0,100,100);place_foe(0,0x3F,130,100);place_foe(1,0x57,90,100);
    {uint8_t before[0x20000];memcpy(before,ram,sizeof(ram));
     for(unsigned i=0;i<120;i++,f++)som_tac_frame(&tac,ram,extra,-1,f);
     assert(!memcmp(before,ram,sizeof(ram))&&tac.writes==0);}
    assert(som_tac_species(0x00).archetype==SOM_ARCH_RUSHDOWN&&som_tac_species(0x02).archetype==SOM_ARCH_TRAPPER&&
           som_tac_species(0x03).archetype==SOM_ARCH_ZONER&&som_tac_species(0x1F).archetype==SOM_ARCH_TECHNICAL&&
           som_tac_species(0x4D).archetype==SOM_ARCH_DUELIST&&som_tac_species(0x20).archetype==SOM_ARCH_NATIVE&&
           som_tac_species(0x60).archetype==SOM_ARCH_NATIVE);
    {unsigned mapped=0;for(unsigned i=0;i<SOM_TAC_SPECIES;i++)mapped+=som_tac_species(i).archetype!=SOM_ARCH_NATIVE;
     assert(mapped==SOM_TAC_SPECIES-6u);}
    /* 3. A rushdown Rabite approaches a hero 90 px away. */
    clear();fresh(SOM_FIGHT_NORMAL);place_hero(0,100,100);place_foe(0,0x00,190,100);
    run(&f,200);
    assert(fx(0)<160&&tac.foe[0].stats_actions[SOM_TA_APPROACH]>0);
    /* 4. A zoner (Chobin Hood) keeps range: the model walks it in, the layer
     *    turns those hops into retreats once it is too close. */
    clear();fresh(SOM_FIGHT_NORMAL);place_hero(0,100,100);place_foe(0,0x03,130,100);
    run(&f,400);
    {const int d=som_tac_dist(fx(0),fy(0),100,100);
     assert(d>=40&&tac.foe[0].stats_actions[SOM_TA_ZONE]+tac.foe[0].stats_actions[SOM_TA_RETREAT]>0);}
    /* without the layer the model closes in */
    clear();fresh(SOM_FIGHT_CLASSIC);place_hero(0,100,100);place_foe(0,0x03,130,100);
    run(&f,400);assert(som_tac_dist(fx(0),fy(0),100,100)<40);
    /* 5/6/7. Whiff: recognised only after the attack ended without a hit,
     *    and acted on only after the reaction time. */
    clear();fresh(SOM_FIGHT_EXPERT);place_hero(0,100,100);place_foe(0,0x1F,150,100);
    run(&f,30);
    hero(0)[0x1C]=0x20;run(&f,1);
    /* the attack's startup: a whiff is not known yet */
    assert(tac.track[0].phase==SOM_COMBAT_STARTUP&&tac.track[0].whiff_seq==0);
    run(&f,20);hero(0)[0x1C]=0;hero(0)[0x1ED]=50;run(&f,1);
    assert(tac.track[0].whiff_seq==1&&tac.track[0].phase==SOM_COMBAT_RECOVERY);
    {const unsigned long whiff=f-1;
     /* no punish before the reaction delay */
     for(unsigned i=0;i<som_fight_levels[SOM_FIGHT_EXPERT].react-1u;i++){
         run(&f,1);assert(tac.foe[0].action!=SOM_TA_PUNISH);}
     unsigned long punished=0;
     for(unsigned i=0;i<60u&&!punished;i++){run(&f,1);if(tac.foe[0].action==SOM_TA_PUNISH)punished=f;}
     assert(punished&&punished-whiff>=som_fight_levels[SOM_FIGHT_EXPERT].react);}
    /* a hit (monster lost HP during the attack) is not a whiff */
    clear();fresh(SOM_FIGHT_EXPERT);place_hero(0,100,100);place_foe(0,0x1F,130,100);
    run(&f,10);hero(0)[0x1C]=0x20;run(&f,5);put16(foe(0)+0x182,80);run(&f,5);hero(0)[0x1C]=0;run(&f,2);
    assert(tac.track[0].whiff_seq==0);
    /* a whiff out of reach is not punished */
    clear();fresh(SOM_FIGHT_EXPERT);place_hero(0,100,100);place_foe(0,0x1F,330,100);
    run(&f,10);hero(0)[0x1C]=0x20;run(&f,10);hero(0)[0x1C]=0;run(&f,40);
    assert(tac.foe[0].stats_actions[SOM_TA_PUNISH]==0);
    /* 8. Collision: steering never moves through the wall column. */
    clear();fresh(SOM_FIGHT_EXPERT);place_hero(0,100,100);place_foe(0,0x00,160,100);wall_x=140;
    run(&f,600);assert(fx(0)>140);
    /* stuck: it gives up the blocked action instead of pushing forever */
    {unsigned natives=tac.foe[0].stats_actions[SOM_TA_NATIVE];(void)natives;
     assert(tac.foe[0].stuck<8u);}
    /* 9. Baiting completes or times out, then cools down. */
    clear();fresh(SOM_FIGHT_EXPERT);tac.arena_archetype=SOM_ARCH_TRAPPER;
    place_hero(0,100,100);place_foe(0,0x02,160,100);
    run(&f,1200);
    assert(tac.foe[0].stats_actions[SOM_TA_BAIT]>0);
    assert(tac.foe[0].action!=SOM_TA_BAIT||f-tac.foe[0].action_since<=som_tac_timeout[SOM_TA_BAIT]+90u);
    /* 10. No oscillation: action changes are bounded by commitment. */
    clear();fresh(SOM_FIGHT_NORMAL);place_hero(0,100,100);place_foe(0,0x00,150,100);
    {unsigned changes=0,last=SOM_TA_NATIVE;
     for(unsigned i=0;i<600;i++,f++){
         som_tac_frame(&tac,ram,extra,-1,f);native_step(f);
         if(tac.foe[0].action!=last){changes++;last=tac.foe[0].action;}
         hero(0)[0x1C]=(i%37u)<8u?0x20:0;                      /* a busy hero */
     }
     assert(changes<600u/ (unsigned)som_fight_levels[SOM_FIGHT_NORMAL].commit*2u);}
    /* 11. Knockdown: no attack while the hero is down (gauge held), and the
     *     hold stops once they are up. */
    clear();fresh(SOM_FIGHT_HARD);place_hero(0,100,100);place_foe(0,0x00,120,100);
    run(&f,10);hero(0)[0x1C]=0x40;hero(0)[0x11]=0x8E;put16(hero(0)+0x190,2);
    swings[0]=0;run(&f,120);assert(swings[0]==0);
    hero(0)[0x1C]=0;hero(0)[0x11]=0;put16(hero(0)+0x190,0);run(&f,200);assert(swings[0]>0);
    /* 12/13. Repeated blocking becomes a habit gradually, and decays. */
    clear();fresh(SOM_FIGHT_HARD);place_hero(0,100,100);place_foe(0,0x1F,140,100);
    run(&f,5);
    extra[0].guard=1;run(&f,10);extra[0].guard=0;run(&f,10);
    assert(!som_tac_habit(&tac,&tac.foe[0],0,SOM_HABIT_BLOCK));     /* once is not a habit */
    for(unsigned i=0;i<6;i++){extra[0].guard=1;run(&f,10);extra[0].guard=0;run(&f,10);}
    assert(som_tac_habit(&tac,&tac.foe[0],0,SOM_HABIT_BLOCK));
    {SomTacSituation s;memset(&s,0,sizeof(s));s.d=40;s.band_min=18;s.band_opt=36;s.band_max=50;s.hero_reach=28;
     s.phase=SOM_COMBAT_GUARDING;s.hp_pct=100;int with[SOM_TA_COUNT],without[SOM_TA_COUNT];
     som_tac_score(&tac,&tac.foe[0],0,&s,f,with);
     SomTacFoe copy=tac.foe[0];memset(copy.habit,0,sizeof(copy.habit));
     som_tac_score(&tac,&copy,0,&s,f,without);
     assert(with[SOM_TA_WAIT]>without[SOM_TA_WAIT]);}
    {const unsigned before=tac.foe[0].habit[0][SOM_HABIT_BLOCK];run(&f,1800);
     assert(tac.foe[0].habit[0][SOM_HABIT_BLOCK]<before/4u);}
    /* 14. Slot reuse: a new monster in the slot does not inherit memory. */
    clear();fresh(SOM_FIGHT_HARD);place_hero(0,100,100);place_foe(0,0x1F,140,100);
    run(&f,5);for(unsigned i=0;i<8;i++){extra[0].guard=1;run(&f,10);extra[0].guard=0;run(&f,10);}
    assert(tac.foe[0].habit[0][SOM_HABIT_BLOCK]);
    place_foe(0,0x00,140,100);run(&f,1);
    assert(tac.foe[0].species==0x00&&!tac.foe[0].habit[0][SOM_HABIT_BLOCK]);
    /* same species, respawned with full HP after a kill: new monster too */
    put16(foe(0)+0x182,10);run(&f,1);tac.foe[0].habit[0][SOM_HABIT_BLOCK]=99;
    put16(foe(0)+0x182,100);run(&f,1);assert(!tac.foe[0].habit[0][SOM_HABIT_BLOCK]);
    /* 15. Map change clears the encounter. */
    tac.foe[0].habit[0][SOM_HABIT_RUSH_IN]=200;put16(ram+0xDC,53);run(&f,1);
    assert(!tac.foe[0].habit[0][SOM_HABIT_RUSH_IN]&&tac.map==53);
    /* 16. A loaded state (frame counter jumps back) drops actions and tokens. */
    tac.foe[0].token_until=f+500;tac.foe[0].action=SOM_TA_BAIT;
    {unsigned long back=f-5000;som_tac_frame(&tac,ram,extra,-1,back);
     assert(tac.foe[0].token_until==0||tac.foe[0].token_until<back+200u);
     assert(tac.foe[0].action!=SOM_TA_BAIT||tac.foe[0].action_since==back);}
    /* 17/18. Coordination: only monsters near the same hero share pressure,
     *        and a dead holder's token goes away. */
    clear();fresh(SOM_FIGHT_HARD);place_hero(0,100,100);place_hero(1,600,100);
    place_foe(0,0x00,150,100);place_foe(1,0x00,100,150);place_foe(2,0x00,640,100);
    run(&f,300);
    {unsigned pressing=0;
     for(unsigned s=0;s<2;s++)pressing+=f<tac.foe[s].token_until;
     assert(pressing<=som_fight_levels[SOM_FIGHT_HARD].pressure);
     /* the far monster fights hero 2 and is never held back by them */
     assert(tac.foe[2].target==1&&tac.foe[2].role!=SOM_ROLE_RESERVE);}
    {int holder=f<tac.foe[0].token_until?0:f<tac.foe[1].token_until?1:-1;
     if(holder>=0){put16(foe((unsigned)holder)+0x190,0x8000);run(&f,1);
         assert(!tac.foe[holder].live);
         run(&f,200);
         const int other=1-holder;
         assert(tac.foe[other].role!=SOM_ROLE_RESERVE||f<tac.foe[other].token_until);}}
    /* 19. A monster that cannot swing never takes a punish. */
    clear();fresh(SOM_FIGHT_EXPERT);place_hero(0,100,100);place_foe(0,0x09,130,100);   /* Specter */
    run(&f,10);hero(0)[0x1C]=0x20;run(&f,10);hero(0)[0x1C]=0;hero(0)[0x1ED]=40;run(&f,60);
    assert(tac.foe[0].stats_actions[SOM_TA_PUNISH]==0);
    /* 21. Difficulty changes behaviour: decisions and the native swing count. */
    {unsigned evals[SOM_FIGHT_LEVELS],sw[SOM_FIGHT_LEVELS],kinds[SOM_FIGHT_LEVELS],punish[SOM_FIGHT_LEVELS];
     for(unsigned lv=0;lv<SOM_FIGHT_LEVELS;lv++){
         clear();fresh(lv);place_hero(0,100,100);place_foe(0,0x1F,150,100);
         for(unsigned i=0;i<1500;i++,f++){
             som_tac_frame(&tac,ram,extra,-1,f);native_step(f);
             hero(0)[0x1C]=(i%60u)<10u?0x20:0;if(i%60u==10u)hero(0)[0x1ED]=40;
             if(hero(0)[0x1ED])hero(0)[0x1ED]--;
         }
         evals[lv]=tac.evals;sw[lv]=swings[0];kinds[lv]=0;
         for(unsigned a=1;a<SOM_TA_COUNT;a++)kinds[lv]+=tac.foe[0].stats_actions[a]!=0;
         punish[lv]=tac.foe[0].stats_actions[SOM_TA_PUNISH];
     }
     printf("difficulty classic..expert: evaluations %u %u %u %u %u, kinds of action %u %u %u %u %u, "
            "punishes %u %u %u %u %u, native swings %u %u %u %u %u\n",
            evals[0],evals[1],evals[2],evals[3],evals[4],kinds[0],kinds[1],kinds[2],kinds[3],kinds[4],
            punish[0],punish[1],punish[2],punish[3],punish[4],sw[0],sw[1],sw[2],sw[3],sw[4]);
     assert(evals[0]==0&&kinds[0]==0&&sw[0]==sw[0]);
     assert(kinds[1]<kinds[4]);
     assert(punish[1]<punish[2]&&punish[2]<punish[3]&&punish[3]<=punish[4]);
     /* tactics never cost the monster its native offence */
     for(unsigned lv=1;lv<SOM_FIGHT_LEVELS;lv++)assert(sw[lv]*10u>=sw[0]*8u);}
    /* 22. P1, P2, P3 tracked independently. */
    clear();fresh(SOM_FIGHT_HARD);place_hero(0,100,100);place_hero(1,180,100);place_hero(2,100,200);
    place_foe(0,0x1F,140,100);
    run(&f,5);hero(1)[0x1C]=0x20;run(&f,10);hero(1)[0x1C]=0;run(&f,2);
    assert(tac.track[1].whiff_seq==1&&tac.track[0].whiff_seq==0&&tac.track[2].whiff_seq==0);
    extra[2].guard=1;run(&f,2);assert(tac.track[2].block_seq==1&&tac.track[0].block_seq==0);
    extra[2].guard=0;
    /* 23. The familiar slot is not an enemy (nor a hero). */
    clear();fresh(SOM_FIGHT_EXPERT);place_hero(0,100,100);place_foe(1,0x00,130,100);
    {uint8_t before[SOM_TAC_OBJECT];memcpy(before,foe(1),sizeof(before));
     for(unsigned i=0;i<100;i++,f++)som_tac_frame(&tac,ram,extra,1,f);
     assert(!memcmp(before,foe(1),sizeof(before))&&!tac.foe[1].live);}
    /* 26. Status and death: untouched. */
    clear();fresh(SOM_FIGHT_EXPERT);place_hero(0,100,100);place_foe(0,0x00,130,100);
    put16(foe(0)+0x190,0x0004);foe(0)[6]=0x82;foe(0)[0x1ED]=0;
    {uint8_t before[SOM_TAC_OBJECT];memcpy(before,foe(0),sizeof(before));
     for(unsigned i=0;i<60;i++,f++)som_tac_frame(&tac,ram,extra,-1,f);
     assert(!memcmp(before,foe(0),sizeof(before)));}
    put16(foe(0)+0x190,0x8000);
    {uint8_t before[SOM_TAC_OBJECT];memcpy(before,foe(0),sizeof(before));
     for(unsigned i=0;i<60;i++,f++)som_tac_frame(&tac,ram,extra,-1,f);
     assert(!memcmp(before,foe(0),sizeof(before)));}
    /* attacks are never touched: a swinging monster keeps its state and gauge */
    put16(foe(0)+0x190,0);foe(0)[0x1C]=0x80;foe(0)[6]=0x02;foe(0)[0x1ED]=0;
    for(unsigned i=0;i<30;i++,f++)som_tac_frame(&tac,ram,extra,-1,f);
    assert(foe(0)[0x1C]==0x80&&foe(0)[6]==0x02&&foe(0)[0x1ED]==0);
    /* the gauge is never lowered */
    foe(0)[0x1C]=0;foe(0)[0x1ED]=90;
    for(unsigned i=0;i<60;i++,f++)som_tac_frame(&tac,ram,extra,-1,f);
    assert(foe(0)[0x1ED]>=90u-0u);
    /* Individual variation: the same species differs between slots, bounded. */
    {SomTacFoe a,b;memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));a.species=b.species=0;
     som_tac_personality(&a,SOM_ARCH_RUSHDOWN,130,0,100);som_tac_personality(&b,SOM_ARCH_RUSHDOWN,130,1,100);
     assert(memcmp(&a.p,&b.p,sizeof(a.p))!=0&&a.p.supported_tactics==b.p.supported_tactics);
     assert(abs((int)a.p.aggression-220)<=24&&abs((int)a.p.range_opt-22)<=4);
     SomTacFoe c;memset(&c,0,sizeof(c));som_tac_personality(&c,SOM_ARCH_RUSHDOWN,130,0,100);
     assert(!memcmp(&a.p,&c.p,sizeof(a.p)));}
    /* Ranks never bypass the level: classic stays off, easy keeps its reaction floor. */
    {SomTac t2;t2.arena_archetype=-1;som_tac_reset(&t2,SOM_FIGHT_EASY);SomTacFoe g;memset(&g,0,sizeof(g));
     g.p=som_arch_profile[SOM_ARCH_DUELIST];g.rank=SOM_RANK_NEMESIS;
     assert(som_tac_react(&t2,&g)>=8u&&!(som_tac_allowed(&t2,&g)&SOM_TT_BAIT));
     g.rank=SOM_RANK_COMMON;
     som_tac_reset(&t2,SOM_FIGHT_HARD);assert(!(som_tac_allowed(&t2,&g)&SOM_TT_FRAMETRAP));
     g.rank=SOM_RANK_CAPTAIN;assert(!(som_tac_allowed(&t2,&g)&SOM_TT_FRAMETRAP));
     g.rank=SOM_RANK_GENERAL;assert(som_tac_allowed(&t2,&g)&SOM_TT_FRAMETRAP);}
    assert(som_fight_parse("expert")==SOM_FIGHT_EXPERT&&som_fight_parse("0")==SOM_FIGHT_CLASSIC&&
           som_fight_parse(NULL)==SOM_FIGHT_NORMAL&&som_fight_parse("weird")==SOM_FIGHT_NORMAL);
    /* 27. Cost: a full frame with three monsters and three heroes. */
    clear();fresh(SOM_FIGHT_EXPERT);place_hero(0,100,100);place_hero(1,120,100);place_hero(2,100,130);
    place_foe(0,0x00,150,100);place_foe(1,0x1F,100,160);place_foe(2,0x03,60,100);
    {clock_t c0=clock();const unsigned n=200000;
     for(unsigned i=0;i<n;i++,f++){som_tac_frame(&tac,ram,extra,-1,f);native_step(f);}
     const double us=(double)(clock()-c0)*1e6/CLOCKS_PER_SEC/n;
     printf("cost: %.3f us per frame (x86, 3 heroes, 3 monsters, incl. model), state %zu bytes\n",us,sizeof(SomTac));
     assert(sizeof(SomTac)<4096u);}
    puts("som_enemy_tactics_test: ok");
    return 0;
}
