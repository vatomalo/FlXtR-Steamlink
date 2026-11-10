/* Pure audio timeline: microseconds in the media timeline, never wall time.
 * SDL reports queued PCM, not physical DAC position; subtract one output period
 * as an explicit estimate. A drained queue freezes instead of inventing time. */
#ifndef FLXTR_AUDIO_MASTER_CLOCK_H
#define FLXTR_AUDIO_MASTER_CLOCK_H
#include <stdint.h>
typedef struct {int ready,observing;int64_t tail,position,latency,anchor,samples,observed,observed_at;} AudioMasterClock;
static inline void audio_master_reset(AudioMasterClock *c,int64_t position,int64_t latency){
    c->ready=c->observing=0;c->observed=c->observed_at=0;c->tail=c->position=c->anchor=position;c->samples=0;c->latency=latency;
}
static inline void audio_master_append(AudioMasterClock *c,int64_t first,int samples,int rate){
    if(!c->ready){c->anchor=c->tail=c->position=first;c->ready=1;}
    c->samples+=samples;c->tail=c->anchor+c->samples*1000000/rate;
}
/* Interpolate only within the output period whose samples SDL has handed to
 * the device. This removes callback-sized video pacing steps without letting
 * wall time run beyond confirmed audio. Refill changes tail and queue equally. */
static inline int64_t audio_master_interpolate(AudioMasterClock *c,int64_t head,int64_t now){
    if(!c->ready)return c->position;
    if(!c->observing||head!=c->observed){
        c->observing=1;c->observed=head;c->observed_at=now;
    }
    int64_t elapsed=now-c->observed_at;
    if(elapsed<0)elapsed=0;
    if(elapsed>c->latency)elapsed=c->latency;
    int64_t next=head+elapsed;
    if(next>c->tail)next=c->tail;
    if(next>c->position)c->position=next;
    return c->position;
}
static inline int64_t audio_master_read_at(AudioMasterClock *c,int64_t queued_us,int64_t now){
    return audio_master_interpolate(c,c->tail-queued_us-c->latency,now);
}
/* Subtract queued frames before converting to microseconds. Converting tail
 * and queue separately introduces +/-1 us rounding changes on PCM refills.
 * Those changes look like new device callbacks and repeatedly reset the
 * interpolation anchor, delaying video by up to an output period. */
static inline int64_t audio_master_read_frames_at(AudioMasterClock *c,int64_t queued_frames,int rate,int64_t now){
    int64_t head=c->anchor+(c->samples-queued_frames)*1000000/rate-c->latency;
    return audio_master_interpolate(c,head,now);
}
static inline int64_t audio_master_read(AudioMasterClock *c,int64_t queued_us){
    if(!c->ready)return c->position;
    int64_t next=c->tail-queued_us-c->latency;
    if(next>c->tail)next=c->tail;
    if(next>c->position)c->position=next;
    return c->position;
}
#endif
