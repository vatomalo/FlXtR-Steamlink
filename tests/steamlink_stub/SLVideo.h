/* Test declarations for the SLVideo subset used by our player. */
#ifndef FLXTR_TEST_SLVIDEO_H
#define FLXTR_TEST_SLVIDEO_H
#include <stdint.h>
typedef struct CSLVideoContext CSLVideoContext;
typedef struct CSLVideoStream CSLVideoStream;
typedef struct CSLVideoOverlay CSLVideoOverlay;
typedef enum {k_ESLVideoFormatH264} ESLVideoFormat;
CSLVideoContext *SLVideo_CreateContext(void);
void SLVideo_FreeContext(CSLVideoContext *);
void SLVideo_GetDisplayResolution(CSLVideoContext *,int *,int *);
CSLVideoStream *SLVideo_CreateStream(CSLVideoContext *,ESLVideoFormat,int);
void SLVideo_FreeStream(CSLVideoStream *);
void SLVideo_SetStreamTargetFramerate(CSLVideoStream *,unsigned,unsigned);
uint32_t SLVideo_GetQueuedVideoMS(CSLVideoStream *);
int SLVideo_BeginFrame(CSLVideoStream *,int);
int SLVideo_WriteFrameData(CSLVideoStream *,void *,int);
int SLVideo_SubmitFrame(CSLVideoStream *);
CSLVideoOverlay *SLVideo_CreateOverlay(CSLVideoContext *,int,int);
void SLVideo_FreeOverlay(CSLVideoOverlay *);
void SLVideo_HideOverlay(CSLVideoOverlay *);
void SLVideo_ShowOverlay(CSLVideoOverlay *);
void SLVideo_GetOverlayPixels(CSLVideoOverlay *,uint32_t **,int *);
void SLVideo_SetOverlayDisplayArea(CSLVideoOverlay *,float,float,float,float);
#endif
