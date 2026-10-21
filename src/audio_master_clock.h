/* Pure audio timeline: microseconds in the media timeline, never wall time.
 * SDL reports queued PCM, not physical DAC position; subtract one output period
 * as an explicit estimate. A drained queue freezes instead of inventing time. */
#ifndef FLXTR_AUDIO_MASTER_CLOCK_H
#define FLXTR_AUDIO_MASTER_CLOCK_H
#include <stdint.h>
typedef struct {int ready;int64_t tail,position,latency,anchor,samples;} AudioMasterClock;
static inline void audio_master_reset(AudioMasterClock *c,int64_t position,int64_t latency){
    c->ready=0;c->tail=c->position=c->anchor=position;c->samples=0;c->latency=latency;
}
static inline void audio_master_append(AudioMasterClock *c,int64_t first,int samples,int rate){
    if(!c->ready){c->anchor=c->tail=c->position=first;c->ready=1;}
    c->samples+=samples;c->tail=c->anchor+c->samples*1000000/rate;
}
static inline int64_t audio_master_read(AudioMasterClock *c,int64_t queued_us){
    if(!c->ready)return c->position;
    int64_t next=c->tail-queued_us-c->latency;
    if(next>c->tail)next=c->tail;
    if(next>c->position)c->position=next;
    return c->position;
}
#endif
