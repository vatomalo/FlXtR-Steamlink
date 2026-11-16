/* Optional frame timing for the SoM proxy (docs/SOM-ARM-PERFORMANCE-BASELINE.md).
 *
 * Off by default. FLXTR_SOM_PERF=1 in the environment, or the marker file
 * /tmp/flxtr-som-perf-enable (checked every SOM_PERF_WINDOW frames, one
 * access() per ~10 s), turns it on. While on, every hook is one
 * clock_gettime(CLOCK_MONOTONIC) and a histogram increment; nothing is
 * printed or written per frame. Every SOM_PERF_WINDOW frames the window's
 * aggregates go to /tmp/flxtr-som-perf.json (write + rename, ~2 KB). Build
 * with -DSOM_PERF_DISABLED to compile every hook out.
 *
 * The Steam Link kernel (3.8) has no ARM vDSO, so each clock read is a
 * syscall (~1-2 us); about a dozen per frame.
 *
 * Phases (all wall-clock ms):
 *   interval   retro_run start to the next retro_run start (the real frame
 *              period: includes everything the frontend does between frames)
 *   run        whole retro_run (proxy + core + callbacks)
 *   core       backend_run, minus the video and audio callbacks it made
 *   video      frontend video callback (RetroArch: copy/hand-off, may wait)
 *   audio      frontend audio callback (resample + driver write; with
 *              audio_sync a full buffer blocks here)
 *   overlay    proxy drawing on the frame before handing it on
 *   mix        music mixing into audio batches
 *   pump       music file read into the ring
 *   wram       WRAM / hot snapshot export for the AI
 *   proxy      run - core - video - audio - overlay - mix (proxy's own work) */
#ifndef FLXTR_SOM_PERF_H
#define FLXTR_SOM_PERF_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define SOM_PERF_WINDOW 600u            /* frames per report, ~10 s */
#define SOM_PERF_BUCKET_US 250u         /* histogram resolution */
#define SOM_PERF_BUCKETS 256u           /* 0..64 ms, then overflow */

enum { SOM_PERF_INTERVAL, SOM_PERF_RUN, SOM_PERF_CORE, SOM_PERF_VIDEO, SOM_PERF_AUDIO,
       SOM_PERF_OVERLAY, SOM_PERF_MIX, SOM_PERF_PUMP, SOM_PERF_WRAM, SOM_PERF_PROXY,
       SOM_PERF_PHASES };
static const char *const som_perf_names[SOM_PERF_PHASES]={
    "interval","run","core","video","audio","overlay","mix","pump","wram","proxy"};

typedef struct {
    uint32_t hist[SOM_PERF_BUCKETS+1u];
    uint64_t total_us;
    uint32_t count,max_us;
} SomPerfPhase;

typedef struct {
    SomPerfPhase phase[SOM_PERF_PHASES];
    uint32_t frames;
    uint32_t late,late_streak,late_streak_max;   /* interval > 1.5 x nominal */
    uint64_t audio_frames,music_frames,music_short;
    uint32_t nominal_us;                         /* 1e6 / fps from av_info */
    double window_s;                             /* wall time of the window */
} SomPerfWindow;

static inline uint64_t som_perf_now_us(void){
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000000u+(uint64_t)ts.tv_nsec/1000u;
}
static inline void som_perf_add(SomPerfWindow *w,unsigned phase,uint64_t us){
    if(phase>=SOM_PERF_PHASES)return;
    SomPerfPhase *p=&w->phase[phase];
    const uint64_t b=us/SOM_PERF_BUCKET_US;
    p->hist[b<SOM_PERF_BUCKETS?b:SOM_PERF_BUCKETS]++;
    p->total_us+=us;p->count++;
    if(us>p->max_us)p->max_us=us>0xFFFFFFFFu?0xFFFFFFFFu:(uint32_t)us;
}
/* One frame interval; counts late frames and the longest run of them. */
static inline void som_perf_interval(SomPerfWindow *w,uint64_t us){
    som_perf_add(w,SOM_PERF_INTERVAL,us);
    if(w->nominal_us&&us*2u>(uint64_t)w->nominal_us*3u){
        w->late++;w->late_streak++;
        if(w->late_streak>w->late_streak_max)w->late_streak_max=w->late_streak;
    }else w->late_streak=0;
}
/* Percentile (0..100) in microseconds, bucket upper bound; max for overflow. */
static inline uint32_t som_perf_percentile(const SomPerfPhase *p,unsigned pct){
    if(!p->count)return 0;
    const uint64_t want=((uint64_t)p->count*pct+99u)/100u;
    uint64_t seen=0;
    for(unsigned b=0;b<=SOM_PERF_BUCKETS;b++){
        seen+=p->hist[b];
        if(seen>=want&&seen)return b<SOM_PERF_BUCKETS?(b+1u)*SOM_PERF_BUCKET_US:p->max_us;
    }
    return p->max_us;
}
/* Window as one JSON object. rusage deltas are passed in by the caller. */
static inline int som_perf_json(const SomPerfWindow *w,char *out,size_t size,
                                unsigned long seq,double utime_s,double stime_s,
                                long minflt,long majflt,long nvcsw,long nivcsw){
    size_t n=0;
    int k=snprintf(out,size,
        "{\"seq\":%lu,\"frames\":%u,\"window_s\":%.3f,\"fps\":%.3f,"
        "\"nominal_fps\":%.3f,\"late\":%u,\"late_streak_max\":%u,"
        "\"audio_frames\":%llu,\"audio_rate\":%.1f,\"music_frames\":%llu,"
        "\"music_short\":%llu,\"cpu_user_s\":%.3f,\"cpu_sys_s\":%.3f,"
        "\"minflt\":%ld,\"majflt\":%ld,\"nvcsw\":%ld,\"nivcsw\":%ld,\"phases\":{",
        seq,w->frames,w->window_s,w->window_s>0?w->frames/w->window_s:0.0,
        w->nominal_us?1e6/w->nominal_us:0.0,w->late,w->late_streak_max,
        (unsigned long long)w->audio_frames,
        w->window_s>0?w->audio_frames/w->window_s:0.0,
        (unsigned long long)w->music_frames,(unsigned long long)w->music_short,
        utime_s,stime_s,minflt,majflt,nvcsw,nivcsw);
    if(k<0||(size_t)k>=size)return -1;
    n=(size_t)k;
    for(unsigned i=0;i<SOM_PERF_PHASES;i++){
        const SomPerfPhase *p=&w->phase[i];
        k=snprintf(out+n,size-n,"%s\"%s\":{\"n\":%u,\"mean_ms\":%.3f,\"p50_ms\":%.3f,"
                   "\"p95_ms\":%.3f,\"p99_ms\":%.3f,\"max_ms\":%.3f,\"total_ms\":%.1f}",
                   i?",":"",som_perf_names[i],p->count,
                   p->count?p->total_us/1000.0/p->count:0.0,
                   som_perf_percentile(p,50)/1000.0,som_perf_percentile(p,95)/1000.0,
                   som_perf_percentile(p,99)/1000.0,p->max_us/1000.0,p->total_us/1000.0);
        if(k<0||(size_t)k>=size-n)return -1;
        n+=(size_t)k;
    }
    k=snprintf(out+n,size-n,"}}\n");
    if(k<0||(size_t)k>=size-n)return -1;
    return (int)(n+(size_t)k);
}
#endif
