/* API success is not evidence of audible HDMI sound. Run on the TV. */
#include "steamlink_sdl_audio.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
static unsigned number(const char *text,unsigned limit){
    char *end;unsigned long n=strtoul(text,&end,10);
    return *text&&!*end&&n&&n<=limit?(unsigned)n:0;
}
int main(int argc,char **argv){
    SteamlinkSDLAudio a;
    int16_t pcm[256*2];
    uint64_t frames=0,total;
    unsigned rate=48000,ms=64,seconds=10,period=1024;
    Uint32 began,last;
    int paused=0,drained=0,rc=1;
    if(argc!=1&&argc!=5){fprintf(stderr,"usage: %s [rate queue-ms seconds period-frames]\n",argv[0]);return 2;}
    if(argc==5){rate=number(argv[1],192000);ms=number(argv[2],1000);seconds=number(argv[3],3600);period=number(argv[4],32768);}
    if(!rate||!ms||seconds<3||!period)return 2;
    if(steamlink_sdl_audio_open(&a,NULL,rate,period,ms))return 1;
    /* Verify frame validation without submitting corrupt PCM. */
    if(steamlink_sdl_audio_write(&a,pcm,1,1)!=-1)goto done;
    a.errors=0;
    if(steamlink_sdl_audio_pause(&a,0))goto done;
    total=(uint64_t)a.spec.freq*seconds;
    began=last=SDL_GetTicks();
    while(frames<total){
        size_t count=(size_t)(total-frames);if(count>256)count=256;
        /* Alternate left-only 440 Hz / right-only 660 Hz every second. */
        for(size_t i=0;i<count;i++){
            uint64_t f=frames+i;int right=(int)(f/a.spec.freq)%2;
            double t=(double)f/a.spec.freq;
            int16_t sample=(int16_t)(sin(6.283185307179586*(right?660:440)*t)*6000);
            pcm[2*i]=right?0:sample;pcm[2*i+1]=right?sample:0;
        }
        ptrdiff_t n=steamlink_sdl_audio_write(&a,pcm,count*4,0);
        if(n<=0)goto done;
        frames+=(uint64_t)n/4;
        if(!paused&&frames>=(uint64_t)a.spec.freq){
            size_t before,after;
            if(steamlink_sdl_audio_pause(&a,1))goto done;
            before=steamlink_sdl_audio_queued(&a);SDL_Delay(150);after=steamlink_sdl_audio_queued(&a);
            fprintf(stderr,"pause queue before=%zu after=%zu\n",before,after);
            if(before!=after||steamlink_sdl_audio_pause(&a,0))goto done;
            paused=1;
        }
        if(!drained&&frames>=(uint64_t)a.spec.freq*2){
            Uint32 start=SDL_GetTicks();
            while(steamlink_sdl_audio_queued(&a)){
                if((Uint32)(SDL_GetTicks()-start)>a.wait_ms)goto done;
                SDL_Delay(1);
            }
            SDL_Delay(50);steamlink_sdl_audio_queued(&a);
            if(!a.underruns)goto done;
            fprintf(stderr,"intentional starvation observed; resuming submission\n");drained=1;
        }
        if((Uint32)(SDL_GetTicks()-last)>=1000){
            fprintf(stderr,"elapsed=%u queue=%zu/%u possible-underruns=%u errors=%u\n",
                SDL_GetTicks()-began,steamlink_sdl_audio_queued(&a),a.capacity,a.underruns,a.errors);
            last=SDL_GetTicks();
        }
    }
    {
        Uint32 start=SDL_GetTicks();
        while(steamlink_sdl_audio_queued(&a)){
            if((Uint32)(SDL_GetTicks()-start)>a.wait_ms)goto done;
            SDL_Delay(1);
        }
    }
    rc=0;
done:
    steamlink_sdl_audio_close(&a);
    fprintf(stderr,"API checks: %s; device closed. TV audibility UNCONFIRMED.\n",rc?"FAIL":"PASS");
    fprintf(stderr,"Confirm alternating left/right tones through the TV; record crackles, dropouts and actual rate/period.\n");
    return rc;
}
