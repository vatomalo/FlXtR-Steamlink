/* Marvell scaler adapter for Valve SLVideo. The public SLVideo API only
 * exposes overlay sizing. Capture the live handle from SLVideo's own underscan
 * initialization; never depend on offsets inside its private C++ objects.
 * The scaler ABI is two eight-byte x/y/width/height rectangles (source, output),
 * verified against the SDK's PEAgent entry points and device readback.
 */
#ifndef HARDWARE_LAYOUT_H
#define HARDWARE_LAYOUT_H
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include "video_layout.h"
typedef int (*ViewportCall)(void *,unsigned,VideoRect *);
typedef int (*ScalingCall)(void *,unsigned,VideoRect *,VideoRect *);
static ViewportCall viewport_set;
static ScalingCall scaling_set,scaling_get;
static void *viewport_handle;
static unsigned viewport_plane;
static VideoRect scaling_original_source,scaling_original_output;
static int scaling_saved;

int MV_PE_VideoSetUnderScanWindow(void *handle,unsigned plane,VideoRect *rect) {
    if(!viewport_set)viewport_set=(ViewportCall)dlsym(RTLD_NEXT,"MV_PE_VideoSetUnderScanWindow");
    if(!viewport_set)return -1;
    int rc=viewport_set(handle,plane,rect);
    if(!rc&&!viewport_handle){
        viewport_handle=handle;viewport_plane=plane;
        scaling_set=(ScalingCall)dlsym(RTLD_NEXT,"MV_PE_VideoSetScaling");
        scaling_get=(ScalingCall)dlsym(RTLD_NEXT,"MV_PE_VideoGetScaling");
    }
    return rc;
}
static int hardware_viewport(VideoRect *rect,int display_w,int display_h,int decoded_w,int decoded_h) {
    if(!viewport_handle||!scaling_set||!scaling_get||display_w<1||display_h<1||
       display_w>32767||display_h>32767)return -1;
    VideoRect before_source,before_output,actual_source,actual_output;
    int rc=scaling_get(viewport_handle,viewport_plane,&before_source,&before_output);
    if(rc)return -1;
    if(!scaling_saved){
        scaling_original_source=before_source;scaling_original_output=before_output;scaling_saved=1;
    }
    /* The source rectangle is the *decoded visible picture*, not the
     * scaler's initial underscan/crop rectangle. Reusing the firmware crop
     * means that Fit, Stretch and even 1:1 can permanently lose image edges.
     * Keep the full input frame for every mode; only vary the destination.
     * The scaler may have its own output coordinate space, handled below. */
    if(decoded_w<1||decoded_h<1||decoded_w>32767||decoded_h>32767)return -1;
    VideoRect source={0,0,(int16_t)decoded_w,(int16_t)decoded_h};
    VideoRect native=scaling_original_output;
    if(native.w<=0||native.h<=0)return -1;
    /* SLVideo's output plane may use an internal coordinate space (e.g. 1280x720
     * while HDMI is 1920x1080). Translate desired HDMI-space corners through
     * the firmware's initial output rectangle, retaining its own origin. */
    int left=native.x+((int64_t)rect->x*native.w+display_w/2)/display_w;
    int top=native.y+((int64_t)rect->y*native.h+display_h/2)/display_h;
    int right=native.x+((int64_t)(rect->x+rect->w)*native.w+display_w/2)/display_w;
    int bottom=native.y+((int64_t)(rect->y+rect->h)*native.h+display_h/2)/display_h;
    if(left<INT16_MIN||top<INT16_MIN||right>INT16_MAX||bottom>INT16_MAX||right<=left||bottom<=top)return -1;
    VideoRect mapped={(int16_t)left,(int16_t)top,(int16_t)(right-left),(int16_t)(bottom-top)};
    fprintf(stderr,"Video scaler: input %d,%d %dx%d / original output %d,%d %dx%d / requested %d,%d %dx%d / mapped %d,%d %dx%d / display %dx%d\n",
        source.x,source.y,source.w,source.h,
        native.x,native.y,native.w,native.h,
        rect->x,rect->y,rect->w,rect->h,
        mapped.x,mapped.y,mapped.w,mapped.h,display_w,display_h);
    rc=scaling_set(viewport_handle,viewport_plane,&source,&mapped);
    if(rc){fprintf(stderr,"Video scaling failed: 0x%x\n",(unsigned)rc);return -1;}
    rc=scaling_get(viewport_handle,viewport_plane,&actual_source,&actual_output);
    if(rc||memcmp(&source,&actual_source,sizeof(source))||memcmp(&mapped,&actual_output,sizeof(mapped))){
        fprintf(stderr,"Video scaling readback did not match; restoring previous size\n");
        scaling_set(viewport_handle,viewport_plane,&before_source,&before_output);return -1;
    }
    return 0;
}
static void restore_viewport(void) {
    if(viewport_handle&&scaling_saved&&scaling_set)
        scaling_set(viewport_handle,viewport_plane,&scaling_original_source,&scaling_original_output);
    scaling_saved=0;viewport_handle=NULL;
}
#endif
