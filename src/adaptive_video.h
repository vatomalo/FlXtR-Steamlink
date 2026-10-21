/* Pure policy, using monotonic microseconds. Signals are playback-health
 * proxies; SLVideo does not expose a displayed-frame/drop counter. */
#ifndef FLXTR_ADAPTIVE_VIDEO_H
#define FLXTR_ADAPTIVE_VIDEO_H
#include <stdint.h>
typedef struct {
    int actual,target,available,blocked,slow,underruns,last_slow,last_underruns;
    int64_t armed,window,healthy,starved,report,last_starved,retry;
} AdaptiveVideo;
static int adaptive_bit(int height){return height<=480?1:height<=720?2:4;}
static void adaptive_hold(AdaptiveVideo *a,int64_t now){
    a->armed=now+10000000;a->window=a->armed;a->healthy=0;
    a->starved=0;a->slow=a->underruns=0;
}
static void adaptive_init(AdaptiveVideo *a,int actual,int target,int available,int blocked,int64_t now){
    *a=(AdaptiveVideo){.actual=actual,.target=target,.available=available,.blocked=blocked};
    /* A requested YouTube tier may be unavailable after resolution. */
    if(target>actual&&adaptive_bit(target)!=adaptive_bit(actual))
        a->available&=adaptive_bit(actual)*2-1;
    a->retry=blocked?now+300000000:0;
    adaptive_hold(a,now);
}
/* reason: 1 network starvation, 2 decoder backpressure, 3 audio underrun,
 * 4 stable upgrade. Returns 0 when no usable rendition change is justified. */
static int adaptive_sample(AdaptiveVideo *a,int64_t now,int64_t starved,int slow,int underrun,
                           double ahead,int allow_upgrade,int *reason){
    *reason=0;
    if(now<a->armed)return 0;
    if(a->blocked&&a->retry&&now>=a->retry){a->blocked=0;a->retry=0;a->healthy=0;}
    if(now-a->window>=10000000){
        a->window=now;a->starved=0;a->slow=a->underruns=0;
    }
    a->starved+=starved;a->slow+=slow;a->underruns+=underrun;
    if(starved||slow||underrun||ahead<12)a->healthy=0;
    else if(!a->healthy)a->healthy=now;
    int trouble=a->starved>=750000||a->slow>=3||a->underruns>=3;
    if(trouble){
        *reason=a->starved>=750000?1:a->slow>=3?2:3;
        a->last_starved=a->starved;a->last_slow=a->slow;a->last_underruns=a->underruns;
        a->blocked|=a->actual<=480?7:a->actual<=720?6:4;
        a->retry=now+300000000;
        a->healthy=0;a->window=now;a->starved=0;a->slow=a->underruns=0;
        const int levels[]={480,720,1080};
        for(int i=0;i<3;i++)if(levels[i]<a->actual&&(a->available&(1<<i)))return levels[i];
        return 0; /* Only one rendition: do not restart into the same stream. */
    }
    if(!allow_upgrade||!a->healthy)return 0;
    const int levels[]={480,720,1080};
    for(int i=0;i<3;i++){
        if(adaptive_bit(levels[i])<=adaptive_bit(a->actual)||!(a->available&(1<<i))||(a->blocked&(1<<i)))continue;
        int64_t stable=levels[i]==720?45000000:90000000;
        if(now-a->healthy>=stable){*reason=4;return levels[i];}
        break;
    }
    return 0;
}
#endif
