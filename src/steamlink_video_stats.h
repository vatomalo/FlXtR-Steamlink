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
    if(!rc&&result)video_stats_log("renderer",stream,result,&last);
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
