#include <assert.h>
#include <stdint.h>
#include "../src/hardware_layout.h"
static VideoRect input={12,8,1280,720},output={0,0,1920,1080};
static int fail_set,bad_read,calls;
static int set(void *h,unsigned p,VideoRect *i,VideoRect *o){
    assert(h==(void*)1&&p==1);calls++;
    if(fail_set)return -1;
    input=*i;output=*o;return 0;
}
static int get(void *h,unsigned p,VideoRect *i,VideoRect *o){
    assert(h==(void*)1&&p==1);*i=input;*o=output;
    if(bad_read&&calls)o->w--;
    return 0;
}
int main(void){
    VideoRect r;
    viewport_handle=(void*)1;viewport_plane=1;scaling_set=set;scaling_get=get;
    assert(!video_rect(VIEW_FIT,1280,534,1,1,1920,1080,&r));
    assert(!hardware_viewport(&r,1920,1080,1280,720));
    assert(input.x==0&&input.y==0&&input.w==1920&&input.h==1080); /* full display-space source */
    assert(output.y==139&&output.w==1920&&output.h==801);
    assert(!video_rect(VIEW_STRETCH,1280,534,1,1,1920,1080,&r));
    assert(!hardware_viewport(&r,1920,1080,1280,720)&&output.h==1080);
    assert(!video_rect(VIEW_PIXEL,1280,534,1,1,1920,1080,&r));
    assert(!hardware_viewport(&r,1920,1080,1280,720)&&output.x==320&&output.y==273&&output.w==1280);
    fail_set=1;assert(hardware_viewport(&r,1920,1080,1280,720)<0);fail_set=0;
    restore_viewport();assert(!viewport_handle&&input.w==1280&&input.x==12&&output.w==1920&&output.h==1080);
    assert(hardware_viewport(&r,1920,1080,1280,720)<0);
    viewport_handle=(void*)1;calls=0;bad_read=1;
    assert(hardware_viewport(&r,1920,1080,1280,720)<0&&calls==2);
    assert(output.w==1920&&output.h==1080);
    /* The initial output is an inset viewport, not a coordinate space.
     * Stretch must remove the inset; Fit and Pixel stay centered in HDMI. */
    viewport_handle=(void*)1;viewport_plane=1;calls=0;bad_read=0;
    scaling_saved=0;
    input=(VideoRect){0,0,720,480};output=(VideoRect){38,21,1844,1037};
    assert(!video_rect(VIEW_FIT,1280,720,1,1,1920,1080,&r));
    assert(!hardware_viewport(&r,1920,1080,1280,720));
    assert(output.x==0&&output.y==0&&output.w==1920&&output.h==1080);
    assert(!video_rect(VIEW_PIXEL,1280,720,1,1,1920,1080,&r));
    assert(!hardware_viewport(&r,1920,1080,1280,720));
    assert(output.x==320&&output.y==180&&output.w==1280&&output.h==720);
    assert(input.x==0&&input.y==0&&input.w==1920&&input.h==1080);
    assert(!video_rect(VIEW_FIT,640,480,1,1,1920,1080,&r));
    assert(!hardware_viewport(&r,1920,1080,640,480));
    assert(output.x==240&&output.y==0&&output.w==1440&&output.h==1080);
    restore_viewport();
    assert(input.w==720&&input.h==480);
    assert(output.x==38&&output.y==21&&output.w==1844&&output.h==1037);
    /* Actual 640x480 decode under 1920x1080 output. */
    viewport_handle=(void*)1;viewport_plane=1;calls=0;bad_read=0;scaling_saved=0;
    input=(VideoRect){0,0,1920,1080};output=(VideoRect){0,0,1920,1080};
    assert(!video_rect(VIEW_FIT,640,480,1,1,1920,1080,&r));
    assert(!hardware_viewport(&r,1920,1080,640,480));
    assert(input.w==1920&&input.h==1080); /* firmware coordinates, not decoded pixels */
    assert(output.x==240&&output.w==1440&&output.h==1080);
    restore_viewport();
    puts("Hardware scaling tests passed");
}
