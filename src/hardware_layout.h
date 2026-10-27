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
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "video_layout.h"
typedef int (*ViewportCall)(void *,unsigned,VideoRect *);
typedef int (*ScalingCall)(void *,unsigned,VideoRect *,VideoRect *);
static ViewportCall viewport_set;
static ScalingCall scaling_set,scaling_get;
static void *viewport_handle;
static unsigned viewport_plane;
static VideoRect scaling_original_source,scaling_original_output;
static int scaling_saved;
static VideoRect scaling_expected_source,scaling_expected_output;
static int scaling_target_valid;
static inline void viewport_trace(const char *format,...){
    char line[512];int prefix=snprintf(line,sizeof(line),"time=%ld pid=%lu ",(long)time(NULL),(unsigned long)getpid());
    if(prefix<0||(size_t)prefix>=sizeof(line))return;
    va_list args;va_start(args,format);vsnprintf(line+prefix,sizeof(line)-(size_t)prefix,format,args);va_end(args);
    struct stat info;if(!stat("viewport.log",&info)&&info.st_size>32768)rename("viewport.log","viewport.previous.log");
    FILE *log=fopen("viewport.log","a");if(log){fprintf(log,"%s\n",line);fclose(log);}
}
/* Read after decoder startup, not just immediately after SetScaling. */
static inline int hardware_viewport_matches(void){
    if(!scaling_target_valid||!viewport_handle||!scaling_get)return -1;
    VideoRect source,output;
    if(scaling_get(viewport_handle,viewport_plane,&source,&output))return -1;
    if(!memcmp(&source,&scaling_expected_source,sizeof(source))&&!memcmp(&output,&scaling_expected_output,sizeof(output)))return 1;
    viewport_trace("event=drift actual_source=%d,%d,%d,%d actual_output=%d,%d,%d,%d expected_source=%d,%d,%d,%d expected_output=%d,%d,%d,%d",
        source.x,source.y,source.w,source.h,output.x,output.y,output.w,output.h,
        scaling_expected_source.x,scaling_expected_source.y,scaling_expected_source.w,scaling_expected_source.h,
        scaling_expected_output.x,scaling_expected_output.y,scaling_expected_output.w,scaling_expected_output.h);
    return 0;
}

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
static int hardware_viewport_apply(VideoRect *rect,int display_w,int display_h,int decoded_w,int decoded_h,int display_coordinates) {
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
    VideoRect source={0,0,(int16_t)(display_coordinates?display_w:decoded_w),(int16_t)(display_coordinates?display_h:decoded_h)};
    VideoRect native=scaling_original_output;
    if(native.w<=0||native.h<=0)return -1;
    VideoRect mapped=*rect;
    if(!display_coordinates){
    /* SLVideo's output plane may use an internal coordinate space (e.g. 1280x720
     * while HDMI is 1920x1080). Translate desired HDMI-space corners through
     * the firmware's initial output rectangle, retaining its own origin. */
    int left=native.x+((int64_t)rect->x*native.w+display_w/2)/display_w;
    int top=native.y+((int64_t)rect->y*native.h+display_h/2)/display_h;
    int right=native.x+((int64_t)(rect->x+rect->w)*native.w+display_w/2)/display_w;
    int bottom=native.y+((int64_t)(rect->y+rect->h)*native.h+display_h/2)/display_h;
    if(left<INT16_MIN||top<INT16_MIN||right>INT16_MAX||bottom>INT16_MAX||right<=left||bottom<=top)return -1;
    mapped=(VideoRect){(int16_t)left,(int16_t)top,(int16_t)(right-left),(int16_t)(bottom-top)};
    }
    if(mapped.w<=0||mapped.h<=0)return -1;
    fprintf(stderr,"Video scaler: input %d,%d %dx%d / original output %d,%d %dx%d / requested %d,%d %dx%d / mapped %d,%d %dx%d / display %dx%d / coordinates=%s\n",
        source.x,source.y,source.w,source.h,
        native.x,native.y,native.w,native.h,
        rect->x,rect->y,rect->w,rect->h,
        mapped.x,mapped.y,mapped.w,mapped.h,display_w,display_h,display_coordinates?"display":"native");
    rc=scaling_set(viewport_handle,viewport_plane,&source,&mapped);
    if(rc){fprintf(stderr,"Video scaling failed: 0x%x\n",(unsigned)rc);return -1;}
    rc=scaling_get(viewport_handle,viewport_plane,&actual_source,&actual_output);
    if(rc||memcmp(&source,&actual_source,sizeof(source))||memcmp(&mapped,&actual_output,sizeof(mapped))){
        fprintf(stderr,"Video scaling readback did not match; restoring previous size\n");
        scaling_set(viewport_handle,viewport_plane,&before_source,&before_output);return -1;
    }
    scaling_expected_source=source;scaling_expected_output=mapped;scaling_target_valid=1;
    return 0;
}
static inline int hardware_viewport(VideoRect *rect,int display_w,int display_h,int decoded_w,int decoded_h){
    return hardware_viewport_apply(rect,display_w,display_h,decoded_w,decoded_h,0);
}
static void restore_viewport(void) {
    if(viewport_handle&&scaling_saved&&scaling_set)
        scaling_set(viewport_handle,viewport_plane,&scaling_original_source,&scaling_original_output);
    scaling_saved=scaling_target_valid=0;viewport_handle=NULL;
}
static inline int hardware_youtube_viewport(VideoRect *rect,int display_w,int display_h){
    /* PR #21 was verified on YouTube playback: this plane is display-sized.
     * Reusing the underscan bounds here makes Stretch a no-op. Encoded sizes
     * belong only to video_rect() for Fit/Pixel, never to this source window. */
    return hardware_viewport_apply(rect,display_w,display_h,display_w,display_h,1);
}
#endif
