/* Unified monotonic media timeline. Audio playback position is authoritative
 * once decoded PCM is available; otherwise use the monotonic wall clock.
 * All values are in AV_TIME_BASE-style microseconds. */
#ifndef FLXTR_MEDIA_CLOCK_H
#define FLXTR_MEDIA_CLOCK_H
#include <stdint.h>
typedef struct {
    int64_t origin_us, wall_start_us, audio_anchor_us, audio_base_frames;
    uint64_t queued_frames_total;
    int active, audio_valid, paused;
    int64_t frozen_us, pause_started_us;
} MediaClock;
static void media_clock_start(MediaClock *c,int64_t pts_us,int64_t now_us) {
    if(c->active)return;
    c->origin_us=pts_us;c->wall_start_us=now_us;c->active=1;
}
static void media_clock_audio_start(MediaClock *c,int64_t pts_us) {
    if(c->audio_valid)return;
    c->audio_anchor_us=pts_us;
    c->audio_base_frames=(int64_t)c->queued_frames_total;
    c->audio_valid=1;
}
static int64_t media_clock_time(const MediaClock *c,int64_t now_us,uint64_t queued_pcm_bytes) {
    if(!c->active)return 0;
    if(c->paused)return c->frozen_us;
    if(c->audio_valid && c->queued_frames_total>(uint64_t)c->audio_base_frames) {
        int64_t played=(int64_t)c->queued_frames_total-(int64_t)(queued_pcm_bytes/4)-c->audio_base_frames;
        if(played<0)played=0;
        return c->audio_anchor_us+played*1000000LL/48000;
    }
    return c->origin_us+now_us-c->wall_start_us;
}
static void media_clock_pause(MediaClock *c,int paused,int64_t now_us,uint64_t queued_bytes) {
    if(!c->active||c->paused==paused)return;
    if(paused){c->frozen_us=media_clock_time(c,now_us,queued_bytes);c->pause_started_us=now_us;c->paused=1;}
    else {c->wall_start_us+=now_us-c->pause_started_us;c->paused=0;}
}
static void media_clock_buffer_wait(MediaClock *c,int64_t waited_us) {
    if(c->active&&!c->audio_valid&&waited_us>100000)c->wall_start_us+=waited_us;
}
#endif
