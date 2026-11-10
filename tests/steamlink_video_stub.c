/* Test-only SLVideo sink: verifies player demux/control lifecycle, not hardware display. */
#define _POSIX_C_SOURCE 200809L
#include <SLVideo.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
CSLVideoContext *SLVideo_CreateContext(void){fputs("TEST SLVIDEO CONTEXT OPEN\n",stderr);return (CSLVideoContext*)malloc(1);}
void SLVideo_FreeContext(CSLVideoContext *c){free(c);}
CSLVideoStream *SLVideo_CreateStream(CSLVideoContext *c,ESLVideoFormat f,int low){(void)c;(void)f;(void)low;return (CSLVideoStream*)malloc(1);}
uint32_t SLVideo_GetQueuedVideoMS(CSLVideoStream *s){(void)s;return 0;}
void SLVideo_FreeStream(CSLVideoStream *s){free(s);}
void SLVideo_GetDisplayResolution(CSLVideoContext *c,int *w,int *h){(void)c;*w=1280;*h=720;}
void SLVideo_SetStreamTargetFramerate(CSLVideoStream *s,unsigned n,unsigned d){(void)s;(void)n;(void)d;}
int SLVideo_BeginFrame(CSLVideoStream *s,int n){(void)s;return n>0&&!getenv("FLXTR_TEST_VIDEO_REJECT")?0:-1;}
int SLVideo_WriteFrameData(CSLVideoStream *s,void *p,int n){(void)s;return p&&n>0?0:-1;}
int SLVideo_SubmitFrame(CSLVideoStream *s){
    static unsigned frames;(void)s;frames++;
    if(getenv("FLXTR_TEST_VIDEO_STALL")&&frames>=120&&frames<=125){
        struct timespec delay={0,300000000};nanosleep(&delay,NULL);
    }
    return 0;
}
struct CSLVideoOverlay {int width,height;uint32_t *pixels;};
CSLVideoOverlay *SLVideo_CreateOverlay(CSLVideoContext *c,int w,int h){
    (void)c;CSLVideoOverlay *p=calloc(1,sizeof(*p));if(!p)return NULL;
    p->width=w;p->height=h;p->pixels=calloc((size_t)w*h,4);if(!p->pixels){free(p);return NULL;}return p;
}
void SLVideo_FreeOverlay(CSLVideoOverlay *p){if(p){free(p->pixels);free(p);}}
void SLVideo_HideOverlay(CSLVideoOverlay *p){(void)p;}
void SLVideo_ShowOverlay(CSLVideoOverlay *p){(void)p;}
void SLVideo_GetOverlayPixels(CSLVideoOverlay *p,uint32_t **pixels,int *pitch){*pixels=p->pixels;*pitch=p->width*4;}
void SLVideo_SetOverlayDisplayArea(CSLVideoOverlay *p,float a,float b,float c,float d){(void)p;(void)a;(void)b;(void)c;(void)d;}

#ifdef FLXTR_TEST_AUDIO_TRACE
#include <SDL.h>
int __real_SDL_QueueAudio(SDL_AudioDeviceID device,const void *data,Uint32 size);
int __wrap_SDL_QueueAudio(SDL_AudioDeviceID device,const void *data,Uint32 size){
    static int started;static struct timespec epoch;
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    if(!started){epoch=now;started=1;}
    double elapsed=now.tv_sec-epoch.tv_sec+(now.tv_nsec-epoch.tv_nsec)/1e9;
    if(elapsed>1&&!SDL_GetQueuedAudioSize(device))
        fprintf(stderr,"TEST AUDIO QUEUE EMPTY at %.3f\n",elapsed);
    return __real_SDL_QueueAudio(device,data,size);
}
#endif
