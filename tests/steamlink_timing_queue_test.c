#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <SLVideo.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../src/steamlink_av_sync.h"
static void video_packet(int64_t ts,int key){
    AVPacket *p=av_packet_alloc();assert(p&&av_new_packet(p,20)==0);
    memset(p->data,1,20);p->pts=p->dts=ts;p->flags=key?AV_PKT_FLAG_KEY:0;
    assert(!sync_video_queue(p,AV_TIME_BASE_Q,0));av_packet_free(&p);
}
int main(void){
    setenv("SDL_AUDIODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER));
    SDL_AudioSpec wanted={0};wanted.freq=48000;wanted.channels=2;wanted.format=AUDIO_S16SYS;wanted.samples=1024;
    SDL_AudioDeviceID device=SDL_OpenAudioDevice(NULL,0,&wanted,NULL,0);assert(device);
    CSLVideoContext *context=SLVideo_CreateContext();
    CSLVideoStream *video=SLVideo_CreateStream(context,k_ESLVideoFormatH264,0);
    AVRational fps={30,1};
    assert(!sync_open(device,1024,context,video,fps,0));sync_pause(1);
    for(int i=0;i<130;i++)video_packet(i*33333LL,i==0||i==129);
    pthread_mutex_lock(&av_sync.mutex);
    assert(av_sync.count==1&&av_sync.queue[av_sync.head].reset&&av_sync.dropped==129);
    assert(av_sync.bytes<=SYNC_VIDEO_BYTES);
    pthread_mutex_unlock(&av_sync.mutex);
    /* A paused worker may not submit the queued future keyframe. */
    SDL_Delay(30);assert(sync_position()==0);
    video=sync_close();SLVideo_FreeStream(video);
    video=SLVideo_CreateStream(context,k_ESLVideoFormatH264,0);
    assert(!sync_open(device,1024,context,video,fps,0));sync_pause(1);
    uint8_t pcm[48000*4/5]={0};assert(!sync_audio_queue(pcm,9600,0));sync_audio_start(1);
    assert(sync_position()==0);SDL_Delay(30);assert(sync_position()==0);
    video_packet(0,1);sync_pause(0);SDL_Delay(100);
    assert(sync_position()>20000&&sync_position()<=200000);
    sync_pause(1);int64_t frozen=sync_position();SDL_Delay(50);assert(sync_position()==frozen);
    video=sync_close();SLVideo_FreeStream(video);SDL_ClearQueuedAudio(device);
    /* A stale chain is skipped only to an already queued keyframe. */
    video=SLVideo_CreateStream(context,k_ESLVideoFormatH264,0);
    assert(!sync_open(device,1024,context,video,fps,0));sync_pause(1);
    video_packet(0,0);video_packet(200000,0);video_packet(800000,1);
    pthread_mutex_lock(&av_sync.mutex);av_sync.pause_position=1000000;av_sync.clock.position=1000000;av_sync.clock.ready=1;av_sync.clock.tail=1000000;pthread_mutex_unlock(&av_sync.mutex);
    sync_pause(0);SDL_Delay(60);
    pthread_mutex_lock(&av_sync.mutex);
    assert(av_sync.frames==1&&av_sync.dropped==2&&av_sync.resets==1);
    pthread_mutex_unlock(&av_sync.mutex);
    video=sync_close();SLVideo_FreeStream(video);SLVideo_FreeContext(context);
    SDL_CloseAudioDevice(device);SDL_Quit();
    puts("PASS: bounded encoded queue, independent keyframe recovery, pause clock and worker shutdown");
}
