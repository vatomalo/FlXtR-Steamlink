/* Adapter for RetroArch 9750719074041e0f1949cf9535d1e04945f6213b.
 * Replaces audio/drivers/sdl_audio.c in an isolated build; ident stays sdl2.
 * Original FlXtR code, MIT. Linking RetroArch remains subject to its GPLv3+. */
#include "../audio_driver.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "steamlink_sdl_audio.c"
typedef struct {SteamlinkSDLAudio output;int nonblock,trace;Uint32 traced;} FlxtrAudio;
static unsigned setting(const char *name,unsigned fallback){
    const char *text=getenv(name);char *end;unsigned long value;
    if(!text)return fallback;
    value=strtoul(text,&end,10);
    if(!*text||*end||value>UINT_MAX)return 0;
    return (unsigned)value;
}
static void *flxtr_init(const char *device,unsigned rate,unsigned latency,
                       unsigned block_frames,unsigned *new_rate){
    FlxtrAudio *a;
    unsigned period=1024;
    const char *profile=getenv("FLXTR_AUDIO_PROFILE");
    (void)rate;(void)block_frames;
    /* No invented tuning: named profiles require device-measured values.
     * Default balanced inherits RetroArch's latency and player's period. */
    if(profile){
        const char *prefix;
        char name[80];
        if(!strcmp(profile,"low"))prefix="LOW";
        else if(!strcmp(profile,"balanced"))prefix="BALANCED";
        else if(!strcmp(profile,"compatibility"))prefix="COMPATIBILITY";
        else {fprintf(stderr,"[steamlink-sdl] Unknown audio profile\n");return NULL;}
        snprintf(name,sizeof(name),"FLXTR_AUDIO_%s_QUEUE_MS",prefix);
        latency=setting(name,0);
        snprintf(name,sizeof(name),"FLXTR_AUDIO_%s_PERIOD",prefix);
        period=setting(name,0);
        if(!latency||!period){
            fprintf(stderr,"[steamlink-sdl] Profile requires measured QUEUE_MS and PERIOD values\n");return NULL;
        }
    }
    a=(FlxtrAudio *)calloc(1,sizeof(*a));if(!a)return NULL;
    a->trace=getenv("FLXTR_AUDIO_TRACE")&&!strcmp(getenv("FLXTR_AUDIO_TRACE"),"1");
    if(steamlink_sdl_audio_open(&a->output,device,48000,period,latency)<0){free(a);return NULL;}
    *new_rate=(unsigned)a->output.spec.freq;
    if(steamlink_sdl_audio_pause(&a->output,0)<0){steamlink_sdl_audio_close(&a->output);free(a);return NULL;}
    return a;
}
static ssize_t flxtr_write(void *data,const void *buf,size_t size){
    FlxtrAudio *a=(FlxtrAudio *)data;
    ptrdiff_t written=steamlink_sdl_audio_write(&a->output,buf,size,a->nonblock);
    if(a->trace&&written>0&&(Uint32)(SDL_GetTicks()-a->traced)>=1000){
        unsigned peak=0;
        for(size_t i=0;i<(size_t)written;i+=2){
            int16_t sample;unsigned magnitude;
            memcpy(&sample,(const unsigned char *)buf+i,2);
            magnitude=(unsigned)(sample<0?-(int)sample:sample);
            if(magnitude>peak)peak=magnitude;
        }
        fprintf(stderr,"[steamlink-sdl] trace: submitted=%ld bytes peak=%u/32768 queued=%zu/%u; TV audibility UNCONFIRMED\n",
            (long)written,peak,steamlink_sdl_audio_queued(&a->output),a->output.capacity);
        a->traced=SDL_GetTicks();
    }
    return (ssize_t)written;
}
static bool flxtr_stop(void *data){return steamlink_sdl_audio_pause(&((FlxtrAudio *)data)->output,1)==0;}
static bool flxtr_start(void *data,bool shutdown){(void)shutdown;return steamlink_sdl_audio_pause(&((FlxtrAudio *)data)->output,0)==0;}
static bool flxtr_alive(void *data){
    FlxtrAudio *a=(FlxtrAudio *)data;
    return a&&!a->output.paused&&SDL_GetAudioDeviceStatus(a->output.device)==SDL_AUDIO_PLAYING;
}
static void flxtr_nonblock(void *data,bool state){((FlxtrAudio *)data)->nonblock=state;}
static void flxtr_free(void *data){
    FlxtrAudio *a=(FlxtrAudio *)data;
    if(a){fprintf(stderr,"[steamlink-sdl] close: possible-underruns=%u errors=%u\n",a->output.underruns,a->output.errors);steamlink_sdl_audio_close(&a->output);free(a);}
}
static bool flxtr_float(void *data){(void)data;return false;}
static size_t flxtr_avail(void *data){return steamlink_sdl_audio_available(&((FlxtrAudio *)data)->output);}
static size_t flxtr_size(void *data){return ((FlxtrAudio *)data)->output.capacity;}
audio_driver_t audio_sdl={flxtr_init,flxtr_write,flxtr_stop,flxtr_start,flxtr_alive,
    flxtr_nonblock,flxtr_free,flxtr_float,"sdl2",NULL,NULL,flxtr_avail,flxtr_size};
