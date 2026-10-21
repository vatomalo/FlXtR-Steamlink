#include "steamlink_sdl_audio.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void){
    SteamlinkSDLAudio a;
    unsigned char *pcm;
    unsigned refs;
    assert(SDL_InitSubSystem(SDL_INIT_AUDIO)==0);
    refs=SDL_WasInit(SDL_INIT_AUDIO);
    assert(steamlink_sdl_audio_open(&a,NULL,48000,1000,64)==-1);
    assert(steamlink_sdl_audio_open(&a,NULL,48000,1024,64)==0);
    pcm=calloc(1,a.capacity+4);assert(pcm);
    assert(a.paused&&a.spec.channels==2&&a.spec.format==AUDIO_S16SYS);
    assert(steamlink_sdl_audio_write(&a,pcm,a.capacity+4,1)==a.capacity);
    assert(steamlink_sdl_audio_write(&a,pcm,4,1)==0);
    assert(steamlink_sdl_audio_available(&a)==0);
    assert(steamlink_sdl_audio_write(&a,pcm,3,1)==-1);
    assert(steamlink_sdl_audio_write(&a,NULL,4,1)==-1);
    assert(steamlink_sdl_audio_pause(&a,0)==0);
    assert(steamlink_sdl_audio_write(&a,pcm,a.capacity+4,0)==a.capacity+4);
    assert(steamlink_sdl_audio_pause(&a,1)==0);
    steamlink_sdl_audio_clear(&a);
    assert(steamlink_sdl_audio_queued(&a)==0);
    assert(steamlink_sdl_audio_available(&a)==a.capacity);
    assert(steamlink_sdl_audio_pause(&a,0)==0);
    assert(steamlink_sdl_audio_write(&a,pcm,4,0)==4);
    SDL_Delay(100);assert(steamlink_sdl_audio_queued(&a)==0);
    assert(a.underruns==1);steamlink_sdl_audio_queued(&a);assert(a.underruns==1);
    steamlink_sdl_audio_close(&a);steamlink_sdl_audio_close(&a);
    assert(SDL_WasInit(SDL_INIT_AUDIO)==refs);
    assert(steamlink_sdl_audio_write(&a,pcm,4,0)==-1);
    assert(steamlink_sdl_audio_open(&a,"nonexistent-flxtr-device",48000,1024,64)==-1);
    assert(SDL_WasInit(SDL_INIT_AUDIO)==refs);
    /* Reinitialization after failure uses a fresh device, never duplicates it. */
    assert(steamlink_sdl_audio_open(&a,NULL,44100,512,32)==0);
    steamlink_sdl_audio_close(&a);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);free(pcm);return 0;
}
