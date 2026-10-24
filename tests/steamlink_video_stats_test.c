#define _POSIX_C_SOURCE 200809L
#define dlsym stats_dlsym
#include "../src/steamlink_video_stats.h"
#undef dlsym
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
static int calls,result_code,delay_enabled,delay_value=250,set_calls,fail_zero;
static int fake_stats(void *handle,unsigned stream,void *result){
    assert(handle==(void*)1&&stream>=7&&stream<=10);calls++;
    if(!result_code)memset(result,0x5a,48);
    return result_code;
}
static int get_delay(void *handle,unsigned stream,int *value){
    assert(handle==(void*)1&&stream>=7&&stream<=10);*value=delay_value;return 0;
}
static int set_delay(void *handle,unsigned stream,int value){
    assert(handle==(void*)1&&stream>=7&&stream<=10);set_calls++;
    if(fail_zero&&!value)return -9;
    delay_value=value;return 0;
}
void *stats_dlsym(void *handle,const char *name){
    assert(handle==RTLD_NEXT);
    if(!strcmp(name,"MV_PE_StreamGetDelay"))return delay_enabled?(void*)get_delay:NULL;
    if(!strcmp(name,"MV_PE_StreamSetDelay"))return (void*)set_delay;
    assert(!strcmp(name,"MV_PE_GetRendererStatistics")||!strcmp(name,"MV_PE_GetDecoderStatistics"));
    return (void*)fake_stats;
}
int main(void){
    char temp[]="/tmp/flxtr-video-stats-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    uint32_t words[12],expected[12];memset(expected,0x5a,sizeof(expected));
    assert(!MV_PE_GetRendererStatistics((void*)1,7,words));
    assert(!memcmp(words,expected,sizeof(words)));
    assert(!MV_PE_GetDecoderStatistics((void*)1,7,words));
    assert(!memcmp(words,expected,sizeof(words))&&calls==2);
    result_code=-9;
    assert(MV_PE_GetRendererStatistics((void*)1,7,words)==-9);
    assert(MV_PE_GetDecoderStatistics((void*)1,7,words)==-9);
    assert(!memcmp(words,expected,sizeof(words))&&calls==4);
    result_code=0;delay_enabled=1;
    assert(!MV_PE_GetRendererStatistics((void*)1,8,words));
    assert(!set_calls&&delay_value==250); /* no trial file: read only */
    FILE *trial=fopen("video-no-delay-test","w");assert(trial);fclose(trial);
    assert(!MV_PE_GetRendererStatistics((void*)1,9,words));
    assert(set_calls==1&&delay_value==0);
    assert(!MV_PE_GetRendererStatistics((void*)1,9,words));assert(set_calls==1);
    delay_value=250;fail_zero=1;
    assert(!MV_PE_GetRendererStatistics((void*)1,10,words));
    assert(set_calls==3&&delay_value==250); /* failed trial restores original */
    assert(!memcmp(words,expected,sizeof(words)));
    unlink("video-no-delay-test");assert(!chdir("/tmp"));assert(!rmdir(temp));
    puts("PASS: video statistics diagnostics preserve SDK buffers, arguments and return codes");
}
