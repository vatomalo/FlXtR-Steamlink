/* Compile against the exact patched RetroArch source used by the ARM build. */
#include "../build/retroarch-audio-src/audio/drivers/sdl_audio.c"
#include <assert.h>
static unsigned callback_calls,callback_occupancy;
static bool callback_active,callback_low;
static void buffer_status(bool active,unsigned occupancy,bool low){
    callback_calls++;callback_active=active;callback_occupancy=occupancy;callback_low=low;
}
int main(void){
    unsigned rate=0;
    int16_t pcm[800*2]={0};
    assert(SDL_setenv("FLXTR_AUDIO_TRACE","1",1)==0);
    void (*callback)(bool,unsigned,bool)=buffer_status;
    assert(flxtr_audio_buffer_register(&callback));
    flxtr_audio_buffer_notify();assert(callback_calls==1&&!callback_active&&!callback_low);
    void *a=audio_sdl.init(NULL,44100,64,0,&rate);
    assert(a&&rate==48000&&!audio_sdl.use_float(a));
    assert(audio_sdl.buffer_size(a)>0&&audio_sdl.alive(a));
    flxtr_audio_buffer_notify();assert(callback_active&&callback_low&&callback_occupancy==0);
    for(int i=0;i<12;i++)assert(audio_sdl.write(a,pcm,sizeof(pcm))==sizeof(pcm));
    assert(((FlxtrAudio *)a)->total_bytes==12*sizeof(pcm));
    assert(((FlxtrAudio *)a)->max_write_us>0);
    assert(((FlxtrAudio *)a)->last_submit>0);
    assert(audio_sdl.stop(a)&&!audio_sdl.alive(a));
    audio_sdl.set_nonblock_state(a,true);
    while(audio_sdl.write(a,pcm,sizeof(pcm))>0){}
    assert(audio_sdl.write_avail(a)<4);
    flxtr_audio_buffer_notify();assert(!callback_active&&!callback_low);
    ((FlxtrAudio *)a)->output.paused=0;
    audio_sdl.set_nonblock_state(a,false);
    flxtr_audio_buffer_notify();assert(callback_active&&!callback_low&&callback_occupancy==100);
    ((FlxtrAudio *)a)->output.paused=1;
    assert(audio_sdl.start(a,false));
    assert(((FlxtrAudio *)a)->last_submit==0);
    audio_sdl.free(a);
    flxtr_audio_buffer_notify();assert(!callback_active&&!callback_low);
    assert(flxtr_audio_buffer_register(NULL));
    unsigned previous_calls=callback_calls;
    flxtr_audio_buffer_notify();assert(callback_calls==previous_calls);
    assert(SDL_setenv("FLXTR_AUDIO_PROFILE","low",1)==0);
    assert(!audio_sdl.init(NULL,48000,64,0,&rate));
    assert(SDL_setenv("FLXTR_AUDIO_LOW_QUEUE_MS","32",1)==0);
    assert(SDL_setenv("FLXTR_AUDIO_LOW_PERIOD","512",1)==0);
    a=audio_sdl.init(NULL,48000,64,0,&rate);assert(a);audio_sdl.free(a);
    assert(SDL_setenv("FLXTR_AUDIO_PROFILE","unknown",1)==0);
    assert(!audio_sdl.init(NULL,48000,64,0,&rate));
    return 0;
}
