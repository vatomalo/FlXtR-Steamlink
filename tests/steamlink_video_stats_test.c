#define _POSIX_C_SOURCE 200809L
#define dlsym stats_dlsym
#include "../src/steamlink_video_stats.h"
#undef dlsym
#include <assert.h>
static int calls,result_code;
static int fake_stats(void *handle,unsigned stream,void *result){
    assert(handle==(void*)1&&stream==7);calls++;
    if(!result_code)memset(result,0x5a,48);
    return result_code;
}
void *stats_dlsym(void *handle,const char *name){
    assert(handle==RTLD_NEXT);
    assert(!strcmp(name,"MV_PE_GetRendererStatistics")||!strcmp(name,"MV_PE_GetDecoderStatistics"));
    return (void*)fake_stats;
}
int main(void){
    uint32_t words[12],expected[12];memset(expected,0x5a,sizeof(expected));
    assert(!MV_PE_GetRendererStatistics((void*)1,7,words));
    assert(!memcmp(words,expected,sizeof(words)));
    assert(!MV_PE_GetDecoderStatistics((void*)1,7,words));
    assert(!memcmp(words,expected,sizeof(words))&&calls==2);
    result_code=-9;
    assert(MV_PE_GetRendererStatistics((void*)1,7,words)==-9);
    assert(MV_PE_GetDecoderStatistics((void*)1,7,words)==-9);
    assert(!memcmp(words,expected,sizeof(words))&&calls==4);
    puts("PASS: video statistics diagnostics preserve SDK buffers, arguments and return codes");
}
