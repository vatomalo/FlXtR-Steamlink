/* The camera director's planner (src/som_director.h). */
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "../src/som_director.h"

static void plan_is_grammar(const SomDirector *d,unsigned prev){
    assert(som_dir_next(prev)>>d->plan[0]&1u);
    for(unsigned j=0;j+1u<SOM_DIR_HORIZON;j++)
        assert(som_dir_next(d->plan[j])>>d->plan[j+1u]&1u);
}
/* Plays `frames` frames of a scene; returns the shots seen (bits). Checks
 * that every finished run of a shot lasted its minimum and maximum beats. */
static unsigned play(SomDirector *d,const SomDirScene *sc,unsigned long *frame,unsigned frames){
    unsigned seen=0,run_shot=SOM_DIR_SHOTS,run_beats=0;
    unsigned long last_beat=~0ul;
    for(unsigned f=0;f<frames;f++,(*frame)++){
        const unsigned s=som_dir_frame(d,sc,*frame);
        seen|=1u<<s;
        if(d->beat_start!=last_beat){
            last_beat=d->beat_start;
            if(s==run_shot)run_beats++;
            else{
                if(run_shot<SOM_DIR_SHOTS&&run_beats){
                    assert(run_beats>=som_dir_min[run_shot]);
                    assert(!som_dir_max[run_shot]||run_beats<=som_dir_max[run_shot]);
                }
                run_shot=s;run_beats=1;
            }
            assert(som_dir_scene_shots(sc)>>s&1u||s==SOM_DIR_WIDE||s==SOM_DIR_PULL);
        }
    }
    return seen;
}

int main(void){
    SomDirector d;som_dir_reset(&d,7);
    SomDirScene fight={SOM_DIR_FIGHT,0xFFFFu,0,2};
    SomDirScene talk={SOM_DIR_TALK,0xFFFFu,0,0};
    SomDirScene peace={SOM_DIR_PEACE,0xFFFFu,1,0};
    SomDirScene hold={SOM_DIR_HOLD,0xFFFFu,0,0};
    unsigned long frame=1;

    /* Grammar: every solved plan chains legally. */
    for(unsigned i=0;i<200u;i++){
        const unsigned prev=d.now;
        d.valid=0;som_dir_solve(&d,&fight,prev);plan_is_grammar(&d,prev);
        d.now=d.plan[0];
    }

    /* A fight uses its own shots and varies; holds keep their lengths. */
    som_dir_reset(&d,11);
    unsigned seen=play(&d,&fight,&frame,60u*60u);
    assert(seen&(1u<<SOM_DIR_MEDIUM)&&seen&(1u<<SOM_DIR_LEAD)&&seen&(1u<<SOM_DIR_FOE));
    assert(!(seen&(1u<<SOM_DIR_TWO))&&!(seen&(1u<<SOM_DIR_PUNCH)));

    /* A whack punches in now, holds, and pulls out within three beats. */
    som_dir_event(&d,SOM_DIR_EV_WHACK,&fight,frame);
    assert(SOM_DIR_SHOT_OF(d.now)==SOM_DIR_PUNCH);
    int pulled=0;
    for(unsigned f=0;f<SOM_DIR_BEAT*4u;f++,frame++)
        if(som_dir_frame(&d,&fight,frame)==SOM_DIR_PULL)pulled=1;
    assert(pulled);

    /* Dialogue: two-shot and speaker take turns, nothing else but wide. */
    som_dir_reset(&d,3);
    seen=play(&d,&talk,&frame,60u*30u);
    assert(seen&(1u<<SOM_DIR_TWO)&&seen&(1u<<SOM_DIR_SPEAKER));
    assert(!(seen&~((1u<<SOM_DIR_TWO)|(1u<<SOM_DIR_SPEAKER)|(1u<<SOM_DIR_WIDE)|(1u<<SOM_DIR_PULL))));

    /* Menus: wide only, at once. */
    seen=play(&d,&hold,&frame,SOM_DIR_BEAT*6u);
    assert(SOM_DIR_SHOT_OF(d.now)==SOM_DIR_WIDE);

    /* Arriving: an establishing pull-out of 3-4 beats, then free. */
    som_dir_event(&d,SOM_DIR_EV_ARRIVE,&peace,frame);
    {
        unsigned beats=0;unsigned long last=~0ul;
        for(unsigned f=0;f<SOM_DIR_BEAT*8u;f++,frame++){
            const unsigned s2=som_dir_frame(&d,&peace,frame);
            if(d.beat_start!=last){last=d.beat_start;if(s2==SOM_DIR_ESTABLISH)beats++;}
        }
        assert(beats>=3u&&beats<=4u);
    }

    /* Nothing fits but wide (no 1.5x, crowds): the fight stays wide. */
    SomDirScene tight={SOM_DIR_FIGHT,SOM_DIR_BIT(SOM_DIR_WIDE),0,3};
    som_dir_reset(&d,5);
    seen=play(&d,&tight,&frame,SOM_DIR_BEAT*20u);
    assert(!(seen&~((1u<<SOM_DIR_WIDE)|(1u<<SOM_DIR_PULL))));

    /* A shot the scene takes away (the dialogue ends) goes at once. */
    som_dir_reset(&d,9);
    (void)play(&d,&talk,&frame,SOM_DIR_BEAT*3u);
    assert(SOM_DIR_SHOT_OF(d.now)!=SOM_DIR_FIGHT);
    (void)som_dir_frame(&d,&fight,frame++);
    assert(som_dir_scene_shots(&fight)>>SOM_DIR_SHOT_OF(d.now)&1u);

    /* Cost: a solve per beat must be cheap on a Cortex-A9. */
    clock_t t0=clock();
    for(unsigned i=0;i<2000u;i++){d.valid=0;som_dir_solve(&d,&fight,d.now);d.now=d.plan[0];}
    const double us=(double)(clock()-t0)*1e6/CLOCKS_PER_SEC/2000.0;
    printf("solve: %.1f us on this machine, %u fallbacks\n",us,d.fallbacks);
    assert(us<2000.0);
    puts("PASS: camera director plans, grammar, events, dialogue and cost");
    return 0;
}
