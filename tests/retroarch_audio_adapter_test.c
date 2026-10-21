/* Compile against the exact patched RetroArch source used by the ARM build. */
#include "../build/retroarch-audio-src/audio/drivers/sdl_audio.c"
#include <assert.h>
int main(void){
    unsigned rate=0;
    int16_t pcm[800*2]={0};
    void *a=audio_sdl.init(NULL,44100,64,0,&rate);
    assert(a&&rate==48000&&!audio_sdl.use_float(a));
    assert(audio_sdl.buffer_size(a)>0&&audio_sdl.alive(a));
    for(int i=0;i<12;i++)assert(audio_sdl.write(a,pcm,sizeof(pcm))==sizeof(pcm));
    assert(audio_sdl.stop(a)&&!audio_sdl.alive(a));
    audio_sdl.set_nonblock_state(a,true);
    while(audio_sdl.write(a,pcm,sizeof(pcm))>0){}
    assert(audio_sdl.write_avail(a)<4);
    assert(audio_sdl.start(a,false));
    audio_sdl.free(a);
    assert(SDL_setenv("FLXTR_AUDIO_PROFILE","low",1)==0);
    assert(!audio_sdl.init(NULL,48000,64,0,&rate));
    assert(SDL_setenv("FLXTR_AUDIO_LOW_QUEUE_MS","32",1)==0);
    assert(SDL_setenv("FLXTR_AUDIO_LOW_PERIOD","512",1)==0);
    a=audio_sdl.init(NULL,48000,64,0,&rate);assert(a);audio_sdl.free(a);
    assert(SDL_setenv("FLXTR_AUDIO_PROFILE","unknown",1)==0);
    assert(!audio_sdl.init(NULL,48000,64,0,&rate));
    return 0;
}
