/* Test-only SLVideo sink: verifies player demux/control lifecycle, not hardware display. */
#include <SLVideo.h>
#include <stdlib.h>
#include <stdio.h>
CSLVideoContext *SLVideo_CreateContext(void){fputs("TEST SLVIDEO CONTEXT OPEN\n",stderr);return (CSLVideoContext*)malloc(1);}
void SLVideo_FreeContext(CSLVideoContext *c){free(c);}
CSLVideoStream *SLVideo_CreateStream(CSLVideoContext *c,ESLVideoFormat f,int low){(void)c;(void)f;(void)low;return (CSLVideoStream*)malloc(1);}
void SLVideo_FreeStream(CSLVideoStream *s){free(s);}
void SLVideo_GetDisplayResolution(CSLVideoContext *c,int *w,int *h){(void)c;*w=1280;*h=720;}
void SLVideo_SetStreamTargetFramerate(CSLVideoStream *s,unsigned n,unsigned d){(void)s;(void)n;(void)d;}
int SLVideo_BeginFrame(CSLVideoStream *s,int n){(void)s;return n>0?0:-1;}
int SLVideo_WriteFrameData(CSLVideoStream *s,void *p,int n){(void)s;return p&&n>0?0:-1;}
int SLVideo_SubmitFrame(CSLVideoStream *s){(void)s;return 0;}
CSLVideoOverlay *SLVideo_CreateOverlay(CSLVideoContext *c,int w,int h){(void)c;(void)w;(void)h;return NULL;}
void SLVideo_FreeOverlay(CSLVideoOverlay *p){(void)p;}
void SLVideo_HideOverlay(CSLVideoOverlay *p){(void)p;}
void SLVideo_ShowOverlay(CSLVideoOverlay *p){(void)p;}
void SLVideo_GetOverlayPixels(CSLVideoOverlay *p,uint32_t **pixels,int *pitch){(void)p;*pixels=NULL;*pitch=0;}
void SLVideo_SetOverlayDisplayArea(CSLVideoOverlay *p,float a,float b,float c,float d){(void)p;(void)a;(void)b;(void)c;(void)d;}
