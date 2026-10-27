#include <assert.h>
#include <stdint.h>
#include "../src/hardware_layout.h"
#include "../src/viewport_guard.h"
static VideoRect input={0,0,720,480},output={38,21,1844,1037};
static int calls,get_failure,mode=VIEW_STRETCH;
static int set(void *handle,unsigned plane,VideoRect *source,VideoRect *target){
    assert(handle==(void*)1&&plane==1);input=*source;output=*target;calls++;return 0;
}
static int get(void *handle,unsigned plane,VideoRect *source,VideoRect *target){
    assert(handle==(void*)1&&plane==1);if(get_failure)return -1;*source=input;*target=output;return 0;
}
static int apply(void *unused){
    (void)unused;VideoRect rect;if(video_rect(mode,640,480,1,1,1920,1080,&rect))return -2;
    return hardware_youtube_viewport(&rect,1920,1080);
}
static int matches(void *unused){(void)unused;return hardware_viewport_matches();}
int main(void){
    ViewportGuard guard={0};
    viewport_plane=1;scaling_set=set;scaling_get=get;
    assert(apply(NULL)==-1);viewport_guard_arm(&guard,0,-1);
    assert(!viewport_guard_poll(&guard,249999,matches,apply,NULL)&&!calls);
    assert(viewport_guard_poll(&guard,250000,matches,apply,NULL)==-1&&!calls);
    viewport_handle=(void*)1;
    assert(viewport_guard_poll(&guard,500000,matches,apply,NULL)==1&&calls==1);
    assert(output.w==1920&&output.h==1080&&hardware_viewport_matches()==1);
    assert(!viewport_guard_poll(&guard,750000,matches,apply,NULL)&&calls==1);
    /* A successful initial readback can be overwritten when decoding starts. */
    input=(VideoRect){0,0,640,480};output=(VideoRect){38,21,1844,1037};
    assert(viewport_guard_poll(&guard,1000000,matches,apply,NULL)==1&&calls==2);
    assert(input.w==1920&&input.h==1080&&output.x==0&&output.w==1920);
    /* A user mode change replaces the desired target; do not restore Stretch. */
    mode=VIEW_PIXEL;assert(!apply(NULL));viewport_guard_arm(&guard,1100000,0);
    assert(!viewport_guard_poll(&guard,1350000,matches,apply,NULL)&&calls==3);
    input.w=640;
    assert(viewport_guard_poll(&guard,1600000,matches,apply,NULL)==1&&calls==4);
    assert(input.w==1920&&output.x==640&&output.y==300&&output.w==640&&output.h==480);
    get_failure=1;
    assert(!viewport_guard_poll(&guard,1850000,matches,apply,NULL)&&guard.pending);
    assert(viewport_guard_poll(&guard,2100000,matches,apply,NULL)==-1&&calls==4);
    get_failure=0;
    assert(!viewport_guard_poll(&guard,11100001,matches,apply,NULL)&&!guard.active&&calls==4);
    viewport_guard_arm(&guard,12000000,-2);assert(!guard.active);
    restore_viewport();assert(hardware_viewport_matches()==-1);
    assert(input.w==720&&input.h==480&&output.x==38&&output.w==1844);
    puts("PASS: delayed viewport availability, post-submit decoder reset, stable no-op, mode change, source drift, failed query and bounded startup retries");
}
