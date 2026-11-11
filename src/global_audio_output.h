/* Shared FlXtR audio output preference. Moonlight and the native players
 * intentionally use the same streaming-audio.txt selection. */
#ifndef FLXTR_GLOBAL_AUDIO_OUTPUT_H
#define FLXTR_GLOBAL_AUDIO_OUTPUT_H
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static void flxtr_audio_preference(char *output,size_t capacity){
    if(!capacity)return;
    snprintf(output,capacity,"auto");
    FILE *f=fopen("streaming-audio.txt","r");
    if(!f)return;
    char line[192];
    if(fgets(line,sizeof(line),f)){
        line[strcspn(line,"\r\n")]=0;
        if(!strcmp(line,"auto")||!strcmp(line,"hdmi")||
           !strcmp(line,"hdmi-sdl")||!strcmp(line,"sdl")||
           (!strncmp(line,"device:",7)&&line[7]))
            snprintf(output,capacity,"%s",line);
    }
    fclose(f);
}
static SDL_AudioDeviceID flxtr_open_audio_output(const SDL_AudioSpec *wanted,
                                                  SDL_AudioSpec *obtained){
    char preference[192];
    flxtr_audio_preference(preference,sizeof(preference));
    const int hdmi=!strcmp(preference,"hdmi")||!strcmp(preference,"hdmi-sdl");
    const char *named=!strncmp(preference,"device:",7)?preference+7:NULL;
    /* SDL device names may differ from Moonlight's low-level ALSA sink IDs.
     * Match a live SDL endpoint, never assume a saved enumeration index. */
    const char *selected=NULL;
    int n=SDL_GetNumAudioDevices(0);
    for(int i=0;i<n;i++){
        const char *name=SDL_GetAudioDeviceName(i,0);
        if(!name)continue;
        if(named&&!strcasecmp(name,named)){selected=name;break;}
        if(hdmi&&(strcasestr(name,"hdmi")||strcasestr(name,"display"))){
            selected=name;break;
        }
    }
    if(named&&!selected)
        fprintf(stderr,"FlXtR audio: saved sink '%s' is not exposed by SDL; falling back to system default\n",named);
    if(hdmi&&!selected)
        fprintf(stderr,"FlXtR audio: HDMI is not listed by SDL; falling back to system default\n");
    SDL_AudioDeviceID device=selected?SDL_OpenAudioDevice(selected,0,wanted,obtained,0):0;
    if(!device&&selected){
        fprintf(stderr,"FlXtR audio: cannot open '%s' (%s); retrying default\n",selected,SDL_GetError());
    }
    if(!device)device=SDL_OpenAudioDevice(NULL,0,wanted,obtained,0);
    if(device)fprintf(stderr,"FlXtR audio: preference=%s output=%s\n",
                      preference,selected&&device?selected:"system default");
    return device;
}
#endif
