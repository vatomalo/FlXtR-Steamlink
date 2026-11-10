/* FlXtR local output test: no PC connection or game is required. */
#include <SDL.h>
#include <SLAudio.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { RATE=48000, SAMPLES=480, CHANNELS=2, FRAMES=300 };

static void tone(int16_t *pcm,int frame){
    /* A quiet, ramped tone: left 440 Hz, then right 660 Hz, then silence. */
    for(int i=0;i<SAMPLES;i++){
        int sample=frame*SAMPLES+i,channel=frame<140?0:1;
        int local=sample%(RATE*3/2),remaining=RATE*3/2-local;
        double ramp=fmin(1.0,fmin(local/480.0,remaining/480.0));
        int16_t value=frame>=280?0:(int16_t)(2400*ramp*sin(6.28318530718*(channel?660:440)*sample/RATE));
        pcm[i*2]=channel?0:value;pcm[i*2+1]=channel?value:0;
    }
}

int main(int argc,char **argv){
    if(argc<2){fprintf(stderr,"Usage: flxtr-audio-test list | hdmi | sdl [exact-device-name]\n");return 2;}
    if(!strcmp(argv[1],"hdmi")){
        CSLAudioContext *context=SLAudio_CreateContext();
        if(!context){fprintf(stderr,"HDMI audio context unavailable\n");return 1;}
        printf("SLAudio configured speakers: %d\n",SLAudio_GetSpeakerCount(context));fflush(stdout);
        CSLAudioStream *stream=SLAudio_CreateStream(context,RATE,CHANNELS,SAMPLES*CHANNELS*2,1);
        if(!stream){SLAudio_FreeContext(context);fprintf(stderr,"HDMI audio stream unavailable\n");return 1;}
        for(int frame=0;frame<FRAMES;frame++){
            /* Bound the queue to 50 ms. The launcher also enforces a timeout. */
            Uint32 start=SDL_GetTicks();
            while(SLAudio_GetQueuedAudioSamples(stream)>RATE/20){
                if(SDL_GetTicks()-start>1000){SLAudio_FreeStream(stream);SLAudio_FreeContext(context);return 1;}
                SDL_Delay(1);
            }
            int16_t *pcm=SLAudio_BeginFrame(stream);
            if(!pcm){SLAudio_FreeStream(stream);SLAudio_FreeContext(context);return 1;}
            tone(pcm,frame);SLAudio_SubmitFrame(stream);
        }
        SDL_Delay(100);SLAudio_FreeStream(stream);SLAudio_FreeContext(context);
    }else if(!strcmp(argv[1],"list")||!strcmp(argv[1],"sdl")){
        if(SDL_InitSubSystem(SDL_INIT_AUDIO)){fprintf(stderr,"SDL audio: %s\n",SDL_GetError());return 1;}
        if(!strcmp(argv[1],"list")){
            for(int i=0;i<SDL_GetNumAudioDevices(0);i++){
                const char *name=SDL_GetAudioDeviceName(i,0);
                if(name&&!strchr(name,'\n')&&!strchr(name,'\r'))puts(name);
            }
            SDL_Quit();return 0;
        }
        const char *device=argc>2?argv[2]:NULL;
        if(device){
            int found=0;
            for(int i=0;i<SDL_GetNumAudioDevices(0);i++){
                const char *name=SDL_GetAudioDeviceName(i,0);
                if(name&&!strcmp(name,device))found=1;
            }
            if(!found){fprintf(stderr,"Selected audio output unavailable: %s\n",device);SDL_Quit();return 1;}
        }
        SDL_AudioSpec want,have;SDL_zero(want);
        want.freq=RATE;want.format=AUDIO_S16SYS;want.channels=CHANNELS;want.samples=SAMPLES;
        SDL_AudioDeviceID output=SDL_OpenAudioDevice(device,0,&want,&have,0);
        if(!output){fprintf(stderr,"SDL open: %s\n",SDL_GetError());SDL_Quit();return 1;}
        printf("SDL driver: %s; output: %s\n",SDL_GetCurrentAudioDriver(),device?device:"default");fflush(stdout);
        SDL_PauseAudioDevice(output,0);
        for(int frame=0;frame<FRAMES;frame++){
            Uint32 start=SDL_GetTicks();
            while(SDL_GetQueuedAudioSize(output)>RATE/20*CHANNELS*2){
                if(SDL_GetTicks()-start>1000){SDL_CloseAudioDevice(output);SDL_Quit();return 1;}
                SDL_Delay(1);
            }
            int16_t pcm[SAMPLES*CHANNELS];tone(pcm,frame);
            if(SDL_QueueAudio(output,pcm,sizeof(pcm))){SDL_CloseAudioDevice(output);SDL_Quit();return 1;}
        }
        SDL_Delay(100);SDL_CloseAudioDevice(output);SDL_Quit();
    }else return 2;
    puts("Test completed; confirm whether both tones were audible.");return 0;
}
