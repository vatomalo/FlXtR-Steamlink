/* Read-only timing diagnostics at the SDK's existing statistics calls.
 * The SDK allocates 48 bytes for each result. Fields remain raw until their
 * meaning is verified; do not turn an opaque queue value into an A/V offset. */
#ifndef FLXTR_STEAMLINK_VIDEO_STATS_H
#define FLXTR_STEAMLINK_VIDEO_STATS_H
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
typedef int (*VideoStatsCall)(void *,unsigned,void *);
typedef int (*VideoDelayGet)(void *,unsigned,int *);
typedef int (*VideoDelaySet)(void *,unsigned,int);
static void video_delay_trial(void *handle,unsigned stream){
    static void *previous;static unsigned previous_stream;static int tried;
    if(tried&&handle==previous&&stream==previous_stream)return;
    tried=1;previous=handle;previous_stream=stream;
    VideoDelayGet get=(VideoDelayGet)dlsym(RTLD_NEXT,"MV_PE_StreamGetDelay");
    int original=0,rc=get?get(handle,stream,&original):-1;
    fprintf(stderr,"Video stream delay: query_rc=%d raw=%d\n",rc,original);
    FILE *trial=fopen("video-no-delay-test","r");
    if(!trial)return;
    fclose(trial);
    /* Zero has no unit ambiguity. Apply only to this stream, only when the
     * firmware reports a positive delay, and always verify the readback. */
    if(rc||original<=0)return;
    VideoDelaySet set=(VideoDelaySet)dlsym(RTLD_NEXT,"MV_PE_StreamSetDelay");
    if(!set)return;
    int actual=original;rc=set(handle,stream,0);
    int read_rc=get(handle,stream,&actual);
    fprintf(stderr,"Video zero-delay trial: old=%d set_rc=%d read_rc=%d actual=%d\n",original,rc,read_rc,actual);
    if(rc||read_rc||actual){
        int restore=set(handle,stream,original);
        fprintf(stderr,"Video zero-delay trial rejected: restore_rc=%d\n",restore);
    }
}
static void video_stats_log(const char *kind,unsigned stream,const void *result,int64_t *last){
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    int64_t ms=(int64_t)now.tv_sec*1000+now.tv_nsec/1000000;
    if(ms-*last<1000)return;
    *last=ms;uint32_t words[12]={0};
    fprintf(stderr,"Video hardware %s: stream=%u",kind,stream);
    if(!strcmp(kind,"renderer")){
        memcpy(&words[1],(const char*)result+4,4);
        memcpy(&words[5],(const char*)result+20,8);
        fprintf(stderr," field4=%u field20=%u field24=%u",words[1],words[5],words[6]);
    }else{
        memcpy(&words[2],(const char*)result+8,4);
        fprintf(stderr," field8=%u",words[2]);
    }
    fputc('\n',stderr);
}
int MV_PE_GetRendererStatistics(void *handle,unsigned stream,void *result){
    static VideoStatsCall next;static int64_t last;
    if(!next)next=(VideoStatsCall)dlsym(RTLD_NEXT,"MV_PE_GetRendererStatistics");
    if(!next)return -1;
    int rc=next(handle,stream,result);
    if(!rc&&result){video_delay_trial(handle,stream);video_stats_log("renderer",stream,result,&last);}
    return rc;
}
int MV_PE_GetDecoderStatistics(void *handle,unsigned stream,void *result){
    static VideoStatsCall next;static int64_t last;
    if(!next)next=(VideoStatsCall)dlsym(RTLD_NEXT,"MV_PE_GetDecoderStatistics");
    if(!next)return -1;
    int rc=next(handle,stream,result);
    if(!rc&&result)video_stats_log("decoder",stream,result,&last);
    return rc;
}
#endif
