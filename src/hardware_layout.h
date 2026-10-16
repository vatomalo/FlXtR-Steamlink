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
static int hardware_viewport(VideoRect *rect,int display_w,int display_h) {
    if(!viewport_handle||!scaling_set||!scaling_get||display_w<1||display_h<1||
       display_w>32767||display_h>32767)return -1;
    VideoRect before_source,before_output,actual_source,actual_output;
    int rc=scaling_get(viewport_handle,viewport_plane,&before_source,&before_output);
    if(rc)return -1;
    if(!scaling_saved){
        scaling_original_source=before_source;scaling_original_output=before_output;scaling_saved=1;
    }
    /* Preserve the firmware's input crop. Overwriting it with display
     * dimensions makes the output appear tiny or misaligned on some devices.
     * Only change the destination rectangle when selecting a view mode. */
    VideoRect source=scaling_original_source;
    fprintf(stderr,"Video scaler: input %d,%d %dx%d / previous output %d,%d %dx%d / target %d,%d %dx%d / display %dx%d\n",
        source.x,source.y,source.w,source.h,
        before_output.x,before_output.y,before_output.w,before_output.h,
        rect->x,rect->y,rect->w,rect->h,display_w,display_h);
    rc=scaling_set(viewport_handle,viewport_plane,&source,rect);
    if(rc){fprintf(stderr,"Video scaling failed: 0x%x\n",(unsigned)rc);return -1;}
    rc=scaling_get(viewport_handle,viewport_plane,&actual_source,&actual_output);
    if(rc||memcmp(&source,&actual_source,sizeof(source))||memcmp(rect,&actual_output,sizeof(*rect))){
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
