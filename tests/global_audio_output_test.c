#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../src/global_audio_output.h"
int main(void){
    char dir[]="/tmp/flxtr-audio-output-XXXXXX";
    assert(mkdtemp(dir)!=NULL);
    assert(chdir(dir)==0);
    char output[192];
    flxtr_audio_preference(output,sizeof(output));
    assert(!strcmp(output,"auto"));
    FILE *f=fopen("streaming-audio.txt","w");assert(f);
    fputs("device:USB Audio DAC\n",f);assert(!fclose(f));
    flxtr_audio_preference(output,sizeof(output));
    assert(!strcmp(output,"device:USB Audio DAC"));
    f=fopen("streaming-audio.txt","w");assert(f);
    fputs("hdmi\n",f);assert(!fclose(f));
    flxtr_audio_preference(output,sizeof(output));
    assert(!strcmp(output,"hdmi"));
    f=fopen("streaming-audio.txt","w");assert(f);
    fputs("not-a-valid-sink\n",f);assert(!fclose(f));
    flxtr_audio_preference(output,sizeof(output));
    assert(!strcmp(output,"auto"));
    assert(flxtr_audio_contains("Display HDMI audio","hdmi"));
    assert(!flxtr_audio_contains("Headset","hdmi"));
    assert(SDL_Init(SDL_INIT_AUDIO)==0);
    SDL_AudioSpec want={0},got={0};
    want.freq=48000;want.format=AUDIO_S16SYS;want.channels=2;want.samples=1024;
    SDL_AudioDeviceID device=flxtr_open_audio_output(&want,&got);
    assert(device!=0);
    SDL_CloseAudioDevice(device);
    SDL_Quit();
    assert(unlink("streaming-audio.txt")==0);
    assert(chdir("/")==0);
    assert(rmdir(dir)==0);
    puts("PASS: global Moonlight audio preference, validation, SDL fallback");
    return 0;
}
