#include <assert.h>
#include <stdio.h>
#include "../src/adaptive_video.h"
int main(void){
    AdaptiveVideo a;int why;
    adaptive_init(&a,720,720,7,0,0);
    assert(!adaptive_sample(&a,1000000,900000,4,4,20,1,&why)); /* startup excluded */
    assert(!adaptive_sample(&a,11000000,0,0,0,20,1,&why));
    assert(!adaptive_sample(&a,100000000,0,0,0,20,1,&why));
    assert(adaptive_sample(&a,101000000,0,0,0,20,1,&why)==1080&&why==4);
    adaptive_init(&a,1080,1080,7,0,0);
    assert(adaptive_sample(&a,11000000,800000,0,0,0,1,&why)==480&&why==1);
    assert(a.blocked&4);
    adaptive_init(&a,480,480,7,4,0);
    assert(!adaptive_sample(&a,11000000,0,0,0,20,1,&why));
    assert(adaptive_sample(&a,56000000,0,0,0,20,1,&why)==720&&why==4);
    adaptive_init(&a,720,720,7,4,0);
    assert(!adaptive_sample(&a,11000000,0,0,0,20,1,&why));
    assert(!adaptive_sample(&a,120000000,0,0,0,20,1,&why)); /* failed 1080 probe blocked */
    adaptive_init(&a,720,720,7,0,0);
    assert(!adaptive_sample(&a,11000000,0,1,0,20,1,&why));
    assert(!adaptive_sample(&a,12000000,0,1,0,20,1,&why));
    assert(adaptive_sample(&a,13000000,0,1,0,20,1,&why)==480&&why==2);
    adaptive_init(&a,720,720,7,0,0);
    assert(adaptive_sample(&a,11000000,0,0,3,20,1,&why)==480&&why==3);
    adaptive_init(&a,720,720,2,0,0);
    assert(!adaptive_sample(&a,11000000,900000,0,0,0,1,&why)); /* fixed 720 source */
    adaptive_init(&a,1080,1080,6,0,0);
    assert(adaptive_sample(&a,11000000,900000,0,0,0,1,&why)==720); /* no 480 exists */
    adaptive_init(&a,720,1080,7,0,0);
    assert(!(a.available&4)); /* resolution request fell back: don't reprobe */
    adaptive_init(&a,480,720,7,0,0);
    assert(a.available==1);
    assert(!adaptive_sample(&a,11000000,0,0,0,20,1,&why));
    assert(!adaptive_sample(&a,200000000,0,0,0,20,1,&why));
    adaptive_init(&a,480,480,7,0,0);
    assert(!adaptive_sample(&a,11000000,0,0,0,20,1,&why));
    assert(!adaptive_sample(&a,56000000,0,0,0,5,1,&why));
    assert(!adaptive_sample(&a,100000000,0,0,0,20,1,&why));
    assert(!adaptive_sample(&a,145000000,0,0,0,20,0,&why)); /* near EOF */
    adaptive_init(&a,360,480,3,0,0);
    assert(!adaptive_sample(&a,11000000,0,0,0,20,1,&why));
    assert(adaptive_sample(&a,56000000,0,0,0,20,1,&why)==720); /* don't reprobe 480 for 360p */
    adaptive_init(&a,480,480,7,6,0);
    assert(!adaptive_sample(&a,11000000,0,0,0,20,1,&why));
    assert(!adaptive_sample(&a,200000000,0,0,0,20,1,&why));
    assert(!adaptive_sample(&a,301000000,0,0,0,20,1,&why));
    assert(adaptive_sample(&a,346000000,0,0,0,20,1,&why)==720); /* retry after 5min backoff */
    adaptive_hold(&a,350000000);
    assert(!adaptive_sample(&a,351000000,900000,4,3,0,1,&why)); /* pause/seek excluded */
    puts("PASS: warmup, stable 480/720/1080 ladder, starvation/decoder/audio signals, rendition floors, failed-tier backoff, cache/EOF/pause guards");
}
