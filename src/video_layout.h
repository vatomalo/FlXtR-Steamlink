#ifndef VIDEO_LAYOUT_H
#define VIDEO_LAYOUT_H
#include <stdint.h>
enum { VIEW_FIT, VIEW_STRETCH, VIEW_PIXEL, VIEW_COUNT };
static const char *const view_names[]={"FULLSCREEN FIT","STRETCH","1:1 PIXELS"};
typedef struct { int16_t x,y,w,h; } VideoRect;
/* A 1:1 image larger than the display cannot fit this hardware viewport. */
static inline int video_rect(int mode,int sw,int sh,int sn,int sd,int dw,int dh,VideoRect *r) {
    if(mode<0||mode>=VIEW_COUNT||sw<=0||sh<=0||dw<=0||dh<=0||dw>32767||dh>32767)return -1;
    int w=dw,h=dh;
    if(mode==VIEW_PIXEL){if(sw>dw||sh>dh)return -1;w=sw;h=sh;}
    else if(mode==VIEW_FIT){
        if(sn<=0||sd<=0)sn=sd=1;
        double aspect=(double)sw*sn/((double)sh*sd);
        if(aspect>(double)dw/dh)h=(int)(dw/aspect+0.5);
        else w=(int)(dh*aspect+0.5);
    }
    if(w<1||h<1)return -1;
    *r=(VideoRect){(dw-w)/2,(dh-h)/2,w,h};return 0;
}
#endif
