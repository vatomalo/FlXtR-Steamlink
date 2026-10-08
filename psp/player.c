#include "app.h"
#include "tilefinch/media_backend.h"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <string.h>
/* CPU copy to scanout avoids retaining a firmware surface after its read lease. */
static void present(MediaPlayback *p,const MediaVideoFrame *f){
    if(f->width<=0 || f->height<=0 || !f->pixels)return;
    if(!media_playback_borrow_video_slot(p,(unsigned)f->slot,f->generation))return;
    unsigned int *dst=(void*)0x44000000;
    int w=480,h=f->height*480/f->width;
    if(h>272){h=272;w=f->width*272/f->height;}
    int left=(480-w)/2,top=(272-h)/2;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){
        int at=(y*f->height/h)*f->stride_pixels+x*f->width/w;
        unsigned int color;
        if(f->format==MEDIA_PIXEL_RGB565){
            unsigned int v=((const unsigned short*)f->pixels)[at];
            color=((v>>11)*255/31)|(((v>>5)&63)*255/63<<8)|((v&31)*255/31<<16);
        }else color=((const unsigned int*)f->pixels)[at];
        dst[(y+top)*512+x+left]=color;
    }
    media_playback_note_frame_displayed(p,f,MEDIA_PSP_PRESENT_PATH_SOFTWARE);
    media_playback_release_video_read(p,(unsigned)f->slot);
}
void play_url(const char *url){
    Stream stream;Budget budget;MediaBackend backend={0};MediaPlayback *p=NULL;
    MediaMp4Demux *demux=NULL;char error[256]={0};
    budget_init(&budget,12u*1024u*1024u);
    status("Opening stream (Circle cancels)...");
    if(!stream_open(&stream,url))return;
    MediaRangeReader reader={.opaque=&stream,.length=stream.length,.read=stream_read};
    MediaMp4Limits limits=media_mp4_default_limits();
    limits.maximum_sample_bytes=512*1024;
    demux=media_mp4_open(&budget,&reader,&limits,error,sizeof(error));
    if(!demux)goto done;
    MediaPspPrepareResult prepared=MEDIA_PSP_PREPARE_PENDING;
    for(int i=0;i<1000 && !atomic_load(&quitting);i++){
        prepared=media_psp_backend_prepare_pump(error,sizeof(error));
        if(prepared!=MEDIA_PSP_PREPARE_PENDING)break;
        sceKernelDelayThread(10000);
    }
    if(prepared!=MEDIA_PSP_PREPARE_READY){if(!error[0])strcpy(error,"Firmware decoder preparation timed out");goto done;}
    if(!media_psp_backend_create(&budget,demux,&backend,error,sizeof(error)))goto done;
    MediaPlaybackOptions options={.decode_lead_us=250000,.maximum_packet_bytes=512*1024};
    p=media_playback_create(&budget,demux,&backend,&options,error,sizeof(error));
    if(!p)goto done;
    pspDebugScreenClear();media_playback_set_playing(p,true);
    uint64_t base=sceKernelGetSystemTimeWide(),paused_at=0;bool paused=false;unsigned previous=PSP_CTRL_CROSS;
    while(!atomic_load(&quitting) && !media_playback_ended(p)){
        SceCtrlData pad;sceCtrlPeekBufferPositive(&pad,1);unsigned pressed=pad.Buttons&~previous;previous=pad.Buttons;
        if(pressed&PSP_CTRL_CIRCLE)break;
        uint64_t now=sceKernelGetSystemTimeWide();
        if(pressed&PSP_CTRL_CROSS){paused=!paused;if(paused)paused_at=now;else base+=now-paused_at;media_playback_set_playing(p,!paused);}
        if(!paused){
            uint64_t clock=now-base,audio=0;
            if(media_playback_audio_cursor_us(p,&audio))clock=audio;
            media_playback_set_presentation_clock_us(p,clock);
            if(media_playback_advance_bounded(p,clock,8,error,sizeof(error))==MEDIA_PLAYBACK_ADVANCE_ERROR)break;
            MediaVideoFrame frame;
            if(media_playback_take_video_frame(p,&frame))present(p,&frame);
        }
        sceKernelDelayThread(1000);
    }
done:
    if(p)media_playback_destroy(p);
    else if(backend.destroy)backend.destroy(backend.opaque);
    if(demux)media_mp4_close(demux);
    stream_close(&stream);
    status("%s",error[0]?error:"Playback finished");
}
