#include <assert.h>
#include <stdint.h>
#include "../src/media_clock.h"
int main(void) {
    MediaClock c={0};
    media_clock_start(&c,10000000,1000000);
    assert(media_clock_time(&c,1500000,0)==10500000);
    media_clock_buffer_wait(&c,500000);
    assert(media_clock_time(&c,2000000,0)==10500000);
    media_clock_audio_start(&c,11000000);
    c.queued_frames_total=48000;
    /* One second queued, none has reached the speakers. */
    assert(media_clock_time(&c,2000000,192000)==11000000);
    /* The queued-byte count measures played PCM frames. */
    assert(media_clock_time(&c,2000000,96000)==11500000);
    media_clock_pause(&c,1,2000000,96000);
    assert(media_clock_time(&c,6000000,0)==11500000);
    media_clock_pause(&c,0,6000000,96000);
    assert(media_clock_time(&c,6000000,96000)==11500000);
    assert(media_clock_time(&c,7000000,0)==12000000);
    return 0;
}
