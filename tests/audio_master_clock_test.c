#include <assert.h>
#include <stdio.h>
#include "../src/audio_master_clock.h"
int main(void){
    AudioMasterClock c;audio_master_reset(&c,0,20000);
    assert(audio_master_read(&c,0)==0); /* no wall-clock startup advance */
    audio_master_append(&c,0,24000,48000);
    assert(audio_master_read(&c,480000)==0);
    assert(audio_master_read(&c,230000)==250000);
    assert(audio_master_read(&c,230000)==250000); /* paused queue */
    assert(audio_master_read(&c,0)==480000);
    for(int i=0;i<100000;i++)assert(audio_master_read(&c,0)==480000); /* starvation freezes */
    audio_master_append(&c,500000,24000,48000);
    assert(audio_master_read(&c,500000)==480000);
    assert(audio_master_read(&c,250000)==730000);
    /* Fractional microseconds must not accumulate over a long film. */
    audio_master_reset(&c,7000000,0);
    for(int i=0;i<1350000;i++)audio_master_append(&c,7000000,1024,48000);
    assert(c.tail==7000000+28800LL*1000000); /* eight hours, exact */
    audio_master_reset(&c,15000000,20000);
    audio_master_append(&c,15000000,24000,48000);
    assert(audio_master_read(&c,250000)==15230000);
    assert(audio_master_read(&c,500000)==15230000); /* never backstep within epoch */
    audio_master_reset(&c,0,20000);audio_master_append(&c,0,24000,48000);
    assert(audio_master_read_at(&c,500000,1000000)==0);
    assert(audio_master_read_at(&c,480000,1020000)==0);
    assert(audio_master_read_at(&c,480000,1030000)==10000); /* smooth between callbacks */
    assert(audio_master_read_at(&c,460000,1040000)==20000);
    assert(audio_master_read_at(&c,460000,1050000)==30000);
    assert(audio_master_read_at(&c,0,1500000)==480000);
    assert(audio_master_read_at(&c,0,1510000)==490000);
    assert(audio_master_read_at(&c,0,1520000)==500000);
    assert(audio_master_read_at(&c,0,9000000)==500000); /* no invented wall-time progress */
    /* AAC 44.1 -> 48 kHz refills can change fractional microseconds even
     * though the device has consumed no additional frames. */
    AudioMasterClock rounded,exact;
    audio_master_reset(&rounded,0,21333);audio_master_reset(&exact,0,21333);
    audio_master_append(&rounded,0,9600,48000);audio_master_append(&exact,0,9600,48000);
    int64_t queued_frames=9600-2048;
    audio_master_read_at(&rounded,queued_frames*1000000/48000,1000000);
    assert(audio_master_read_frames_at(&exact,queued_frames,48000,1000000)==21333);
    audio_master_append(&rounded,200000,3343,48000);audio_master_append(&exact,200000,3343,48000);
    queued_frames+=3343;
    audio_master_read_at(&rounded,queued_frames*1000000/48000,1008000);
    assert(audio_master_read_frames_at(&exact,queued_frames,48000,1008000)==29333);
    audio_master_append(&rounded,269645,1115,48000);audio_master_append(&exact,269645,1115,48000);
    queued_frames+=1115;
    int64_t old=audio_master_read_at(&rounded,queued_frames*1000000/48000,1016000);
    int64_t corrected=audio_master_read_frames_at(&exact,queued_frames,48000,1016000);
    assert(corrected==37333);assert(corrected-old==15999);
    printf("Refill regression: old clock lag=%lld us; corrected lag=0 us\n",(long long)(corrected-old));
    assert(audio_master_read_frames_at(&exact,queued_frames,48000,9000000)==42666);
    /* Drained queues reach their exact sample tail, then freeze. */
    audio_master_read_frames_at(&exact,0,48000,10000000);
    assert(audio_master_read_frames_at(&exact,0,48000,11000000)==exact.tail);
    assert(audio_master_read_frames_at(&exact,0,48000,12000000)==exact.tail);
    puts("PASS: audio sample clock, startup/pause/underrun freeze, eight-hour precision, seek reset and monotonicity");
}
