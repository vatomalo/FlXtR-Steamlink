#include "steamlink_sdl_audio.h"
#include <stdio.h>
#include <string.h>

static int output_error(SteamlinkSDLAudio *a, const char *operation){
    a->errors++;
    fprintf(stderr,"[steamlink-sdl] %s: %s\n",operation,SDL_GetError());
    return -1;
}
int steamlink_sdl_audio_open(SteamlinkSDLAudio *a,const char *device,
                            unsigned rate,unsigned period,unsigned capacity_ms){
    SDL_AudioSpec wanted;
    uint64_t capacity;
    memset(a,0,sizeof(*a));
    if(!rate||rate>192000||capacity_ms<1||capacity_ms>1000||
       (period&&(period>32768||(period&(period-1))))) {
        SDL_SetError("Invalid rate, period or queue capacity");
        return output_error(a,"configuration");
    }
    /* Balance our own subsystem reference even if another SDL user exists. */
    if(SDL_InitSubSystem(SDL_INIT_AUDIO))return output_error(a,"audio init");
    a->owned=1;
    fprintf(stderr,"[steamlink-sdl] driver=%s devices=%d requested=%u Hz stereo S16 period=%u\n",
        SDL_GetCurrentAudioDriver(),SDL_GetNumAudioDevices(0),rate,period?period:1024);
    for(int i=0;i<SDL_GetNumAudioDevices(0);i++)
        fprintf(stderr,"[steamlink-sdl] device[%d]=%s\n",i,SDL_GetAudioDeviceName(i,0));
    SDL_zero(wanted);wanted.freq=(int)rate;wanted.format=AUDIO_S16SYS;
    wanted.channels=2;wanted.samples=(Uint16)(period?period:1024);
    /* Same default-device, callback-free mechanism as player.c. Only rate may
     * negotiate; RetroArch receives obtained.freq and performs resampling. */
    a->device=SDL_OpenAudioDevice(device&&*device?device:NULL,0,&wanted,&a->spec,
                                SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if(!a->device){output_error(a,"open device");goto fail;}
    if(a->spec.format!=AUDIO_S16SYS||a->spec.channels!=2||a->spec.freq<=0||!a->spec.samples){
        SDL_SetError("Device did not provide stereo S16 PCM");
        output_error(a,"obtained format");goto fail;
    }
    capacity=(uint64_t)a->spec.freq*capacity_ms/1000;
    /* At least one SDL period. Hardware buffers are additional and unknown. */
    if(capacity<a->spec.samples)capacity=a->spec.samples;
    a->capacity=(Uint32)capacity*4;
    a->wait_ms=capacity_ms+2u*(unsigned)a->spec.samples*1000u/(unsigned)a->spec.freq+100u;
    a->paused=1;
    fprintf(stderr,"[steamlink-sdl] obtained=%d Hz format=0x%x channels=%u period=%u queue-cap=%u bytes; TV audibility UNCONFIRMED\n",
        a->spec.freq,a->spec.format,a->spec.channels,a->spec.samples,a->capacity);
    return 0;
fail:
    steamlink_sdl_audio_close(a);return -1;
}
size_t steamlink_sdl_audio_queued(SteamlinkSDLAudio *a){
    Uint32 queued=a->device?SDL_GetQueuedAudioSize(a->device):0;
    if(a->device&&!a->paused&&a->fed&&!queued){
        if(!a->empty){
            a->underruns++;
            if(a->underruns<=4||!(a->underruns&(a->underruns-1)))
                fprintf(stderr,"[steamlink-sdl] software queue empty #%u (possible underrun; DAC state unknown)\n",a->underruns);
        }
        a->empty=1;
    }else if(queued)a->empty=0;
    return queued;
}
size_t steamlink_sdl_audio_available(SteamlinkSDLAudio *a){
    size_t queued=steamlink_sdl_audio_queued(a);
    return queued<a->capacity?a->capacity-queued:0;
}
ptrdiff_t steamlink_sdl_audio_write(SteamlinkSDLAudio *a,const void *pcm,size_t bytes,int nonblock){
    size_t written=0;
    Uint32 progress=SDL_GetTicks();
    if(!a->device||bytes%4||(!pcm&&bytes)){
        SDL_SetError("Closed device or incomplete stereo PCM frame");return output_error(a,"write");
    }
    while(written<bytes){
        size_t available,amount;
        if(SDL_GetAudioDeviceStatus(a->device)==SDL_AUDIO_STOPPED){
            SDL_SetError("Audio device stopped; frontend must reinitialize");return output_error(a,"device status");
        }
        available=steamlink_sdl_audio_available(a);
        amount=bytes-written;if(amount>available)amount=available;amount-=amount%4;
        if(amount){
            if(SDL_QueueAudio(a->device,(const Uint8 *)pcm+written,(Uint32)amount))return output_error(a,"queue");
            a->fed=1;a->empty=0;written+=amount;progress=SDL_GetTicks();
        }
        if(nonblock||written==bytes||a->paused)break;
        if(!amount){
            if((Uint32)(SDL_GetTicks()-progress)>a->wait_ms){
                SDL_SetError("Audio queue stalled");return output_error(a,"write timeout");
            }
            SDL_Delay(1);
        }
    }
    return (ptrdiff_t)written;
}
int steamlink_sdl_audio_pause(SteamlinkSDLAudio *a,int pause){
    if(!a->device)return -1;
    SDL_PauseAudioDevice(a->device,pause!=0);a->paused=pause!=0;
    if(SDL_GetAudioDeviceStatus(a->device)==SDL_AUDIO_STOPPED){
        SDL_SetError("Audio device stopped");return output_error(a,"pause/resume");
    }
    return 0;
}
void steamlink_sdl_audio_clear(SteamlinkSDLAudio *a){
    if(a->device)SDL_ClearQueuedAudio(a->device);
    a->fed=a->empty=0;
}
void steamlink_sdl_audio_close(SteamlinkSDLAudio *a){
    if(a->device){SDL_PauseAudioDevice(a->device,1);SDL_ClearQueuedAudio(a->device);SDL_CloseAudioDevice(a->device);}
    if(a->owned)SDL_QuitSubSystem(SDL_INIT_AUDIO);
    a->device=0;a->owned=0;a->paused=1;
}
