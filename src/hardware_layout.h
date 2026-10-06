/* Optional Steam Link Marvell viewport adapter. The public SLVideo API has no
 * video rectangle setter. Interpose the rectangle call SLVideo already makes,
 * preserving its arguments and return value, to obtain the actual live handle.
 * No private C++ object offsets or guessed scale-mode enums are used.
 * ABI: Valve SDK's published SLVideo debug types and PEAgent call sites.
 */
#ifndef HARDWARE_LAYOUT_H
#define HARDWARE_LAYOUT_H
#include <dlfcn.h>
#include "video_layout.h"
typedef int (*ViewportCall)(void *,unsigned,VideoRect *);
static ViewportCall viewport_set,viewport_get;
static void *viewport_handle;
static unsigned viewport_plane;
static VideoRect viewport_original;
static int viewport_ready;

int MV_PE_VideoSetUnderScanWindow(void *handle,unsigned plane,VideoRect *rect) {
    if(!viewport_set)viewport_set=(ViewportCall)dlsym(RTLD_NEXT,"MV_PE_VideoSetUnderScanWindow");
    if(!viewport_set)return -1;
    int rc=viewport_set(handle,plane,rect);
    if(!rc&&!viewport_ready){
        viewport_get=(ViewportCall)dlsym(RTLD_NEXT,"MV_PE_VideoGetUnderScanWindow");
        if(viewport_get&&!viewport_get(handle,plane,&viewport_original)){
            viewport_handle=handle;viewport_plane=plane;viewport_ready=1;
        }
    }
    return rc;
}
static int hardware_viewport(VideoRect *rect) {
    VideoRect actual;
    if(!viewport_ready)return -1;
    if(viewport_set(viewport_handle,viewport_plane,rect))return -1;
    if(viewport_get(viewport_handle,viewport_plane,&actual)||memcmp(rect,&actual,sizeof(actual))){
        viewport_set(viewport_handle,viewport_plane,&viewport_original);return -1;
    }
    return 0;
}
static void restore_viewport(void) {
    if(viewport_ready)viewport_set(viewport_handle,viewport_plane,&viewport_original);
    viewport_ready=0;viewport_handle=NULL;
}
#endif
