/* Audio-master Steam Link scheduler. Only the video worker touches its stream.
 * The main thread feeds PCM and services controls; decoder backpressure cannot
 * block audio. Encoded overflow discards a whole dependency chain until a key
 * frame, never an arbitrary reference frame. No decoded video copies. */
#ifndef FLXTR_STEAMLINK_AV_SYNC_H
#define FLXTR_STEAMLINK_AV_SYNC_H
#include <pthread.h>
#include "audio_master_clock.h"
#define SYNC_VIDEO_PACKETS 128
#define SYNC_VIDEO_BYTES (8*1024*1024)
typedef struct {AVPacket *packet;int64_t dts,pts;int reset;} SyncVideoPacket;
static struct {
    pthread_mutex_t mutex;pthread_t thread;int initialized,threaded,quit,paused,started,eof,error;
    SDL_AudioDeviceID device;AudioMasterClock clock;
    CSLVideoContext *context;CSLVideoStream *stream;AVRational fps;
    int64_t decode_lead;Uint32 pcm_limit;
    SyncVideoPacket queue[SYNC_VIDEO_PACKETS];int head,count,bytes,recover;
    unsigned epoch,underflows,frames,slow,dropped,resets,hardware_ms;int empty,busy;int64_t last_video,maximum_late,pause_at,pause_position;
} av_sync;
static unsigned sync_epoch;
static int64_t sync_position_locked(void){
    if(av_sync.paused)return av_sync.pause_position;
    Uint32 queued_bytes=SDL_GetQueuedAudioSize(av_sync.device);
    int64_t queued=queued_bytes/4;
    int64_t position=audio_master_read_frames_at(&av_sync.clock,queued,48000,av_gettime_relative());
    if(av_sync.started&&!av_sync.eof&&!queued&&position>=av_sync.clock.tail){
        if(!av_sync.empty)av_sync.underflows++;
        av_sync.empty=1;
    }else if(queued)av_sync.empty=0;
    return position;
}
static int64_t sync_position(void){
    if(!av_sync.initialized)return 0;
    pthread_mutex_lock(&av_sync.mutex);int64_t p=sync_position_locked();pthread_mutex_unlock(&av_sync.mutex);
    return p;
}
static void sync_pause(int pause){
    if(!av_sync.initialized)return;
    pthread_mutex_lock(&av_sync.mutex);
    int64_t now=av_gettime_relative();
    if(pause&&!av_sync.paused){
        av_sync.pause_position=sync_position_locked();av_sync.pause_at=now;
    }else if(!pause&&av_sync.paused&&av_sync.clock.observing){
        av_sync.clock.observed_at+=now-av_sync.pause_at;
    }
    av_sync.paused=pause;
    SDL_PauseAudioDevice(av_sync.device,pause||!av_sync.started);
    pthread_mutex_unlock(&av_sync.mutex);
}
static void sync_clear_video_locked(void){
    while(av_sync.count){
        SyncVideoPacket *p=&av_sync.queue[av_sync.head];
        av_sync.bytes-=p->packet->size;av_packet_free(&p->packet);
        av_sync.head=(av_sync.head+1)%SYNC_VIDEO_PACKETS;av_sync.count--;av_sync.dropped++;
    }
}
static void *sync_video_worker(void *unused){
    (void)unused;
    for(;;){
        pthread_mutex_lock(&av_sync.mutex);
        int quit=av_sync.quit,ready=!av_sync.quit&&av_sync.count&&!av_sync.paused;
        SyncVideoPacket item={0};
        if(ready){item=av_sync.queue[av_sync.head];av_sync.head=(av_sync.head+1)%SYNC_VIDEO_PACKETS;
            av_sync.count--;av_sync.bytes-=item.packet->size;av_sync.busy=1;}
        pthread_mutex_unlock(&av_sync.mutex);
        if(quit)break;
        if(!ready){SDL_Delay(2);continue;}
        int discard=0;
        for(;;){
            pthread_mutex_lock(&av_sync.mutex);
            int64_t master=sync_position_locked();quit=av_sync.quit;
            int pause=av_sync.paused||!av_sync.clock.ready,ending=av_sync.eof;
            int64_t audio_end=av_sync.clock.tail;
            pthread_mutex_unlock(&av_sync.mutex);
            if(quit)break;
            /* Feed decode-order DTS with the configured decode lead. PTS remains diagnostic;
             * B-frame references must keep their encoded ordering. */
            int64_t late=master-item.dts;
            if(late>300000&&!item.reset){
                /* Catch up only when a newer independent decode point already
                 * exists. Otherwise keep the reference chain intact and let
                 * fast submissions recover rather than freezing for a GOP. */
                pthread_mutex_lock(&av_sync.mutex);
                int skip=-1;
                for(int i=0;i<av_sync.count;i++){
                    SyncVideoPacket *p=&av_sync.queue[(av_sync.head+i)%SYNC_VIDEO_PACKETS];
                    if((p->packet->flags&AV_PKT_FLAG_KEY)&&p->dts<=master+100000&&p->dts>item.dts)skip=i;
                }
                if(skip>=0){
                    for(int i=0;i<skip;i++){
                        SyncVideoPacket *p=&av_sync.queue[av_sync.head];
                        av_sync.bytes-=p->packet->size;av_packet_free(&p->packet);
                        av_sync.head=(av_sync.head+1)%SYNC_VIDEO_PACKETS;av_sync.count--;av_sync.dropped++;
                    }
                    av_sync.queue[av_sync.head].reset=1;av_sync.dropped++;discard=1;
                }
                pthread_mutex_unlock(&av_sync.mutex);
                if(discard)break;
            }
            if(ending&&!pause&&item.dts>audio_end+500000){discard=1;break;}
            if(!pause&&item.dts<=master+av_sync.decode_lead)break;
            SDL_Delay(2);
        }
        if(quit||discard){av_packet_free(&item.packet);pthread_mutex_lock(&av_sync.mutex);av_sync.busy=0;pthread_mutex_unlock(&av_sync.mutex);if(quit)break;continue;}
        if(item.reset){
            SLVideo_FreeStream(av_sync.stream);
            av_sync.stream=SLVideo_CreateStream(av_sync.context,k_ESLVideoFormatH264,0);
            if(av_sync.stream&&av_sync.fps.num>0&&av_sync.fps.den>0)
                SLVideo_SetStreamTargetFramerate(av_sync.stream,av_sync.fps.num,av_sync.fps.den);
            pthread_mutex_lock(&av_sync.mutex);av_sync.resets++;pthread_mutex_unlock(&av_sync.mutex);
        }
        int64_t began=av_gettime_relative();
        int error=!av_sync.stream||SLVideo_BeginFrame(av_sync.stream,item.packet->size)<0||
            SLVideo_WriteFrameData(av_sync.stream,item.packet->data,item.packet->size)<0||
            SLVideo_SubmitFrame(av_sync.stream)<0;
        int64_t elapsed=av_gettime_relative()-began;
        int64_t threshold=250000;
        if(av_sync.fps.num>0&&av_sync.fps.den>0){
            int64_t triple=3000000LL*av_sync.fps.den/av_sync.fps.num;
            if(triple>threshold)threshold=triple;
        }
        unsigned hardware_ms=av_sync.stream?SLVideo_GetQueuedVideoMS(av_sync.stream):0;
        pthread_mutex_lock(&av_sync.mutex);
        av_sync.hardware_ms=hardware_ms;av_sync.busy=0;
        if(error)av_sync.error=1;
        else{
            av_sync.frames++;av_sync.last_video=item.pts;
            int64_t late=sync_position_locked()-item.pts;
            if(late>av_sync.maximum_late)av_sync.maximum_late=late;
            if(elapsed>=threshold)av_sync.slow++;
        }
        pthread_mutex_unlock(&av_sync.mutex);
        av_packet_free(&item.packet);
        if(error)break;
    }
    return NULL;
}
static int sync_open(SDL_AudioDeviceID device,int samples,CSLVideoContext *context,CSLVideoStream *stream,
                     AVRational fps,int64_t position){
    memset(&av_sync,0,sizeof(av_sync));pthread_mutex_init(&av_sync.mutex,NULL);
    av_sync.initialized=1;av_sync.epoch=++sync_epoch;av_sync.device=device;av_sync.context=context;av_sync.stream=stream;av_sync.fps=fps;
    int lead_ms=100,requested=0;char extra;
    FILE *timing=fopen("video-decode-lead-ms","r");
    if(timing){
        if(fscanf(timing,"%d %c",&requested,&extra)==1&&requested>=100&&requested<=2000)lead_ms=requested;
        fclose(timing);
    }
    av_sync.decode_lead=(int64_t)lead_ms*1000;
    int reserve_ms=lead_ms+250;if(reserve_ms<500)reserve_ms=500;
    av_sync.pcm_limit=(Uint32)reserve_ms*48000*4/1000;
    fprintf(stderr,"Video decode lead: %dms; PCM reserve limit: %dms; audio start threshold unchanged at 200ms\n",lead_ms,reserve_ms);
    audio_master_reset(&av_sync.clock,position,(int64_t)samples*1000000/48000);
    if(pthread_create(&av_sync.thread,NULL,sync_video_worker,NULL))return -1;
    av_sync.threaded=1;return 0;
}
static int sync_video_queue(AVPacket *packet,AVRational time_base,int64_t offset){
    if(packet->size<1||packet->size>SYNC_VIDEO_BYTES)return -1;
    AVPacket *copy=av_packet_clone(packet);if(!copy)return -1;
    /* Hardware consumes Annex B bytes only; do not retain arbitrary demux side
     * data in every queued reference (it is not covered by the payload cap). */
    av_packet_free_side_data(copy);
    int64_t dts=packet->dts!=AV_NOPTS_VALUE?packet->dts:packet->pts;
    int64_t pts=packet->pts!=AV_NOPTS_VALUE?packet->pts:dts;
    pthread_mutex_lock(&av_sync.mutex);
    if(dts==AV_NOPTS_VALUE){
        int64_t duration=av_sync.fps.num>0?1000000LL*av_sync.fps.den/av_sync.fps.num:40000;
        dts=av_sync.count?av_sync.queue[(av_sync.head+av_sync.count-1)%SYNC_VIDEO_PACKETS].dts+duration:sync_position_locked();
        pts=dts;
    }else{
        dts=av_rescale_q(dts,time_base,AV_TIME_BASE_Q)-offset;
        pts=av_rescale_q(pts,time_base,AV_TIME_BASE_Q)-offset;
    }
    if(av_sync.count==SYNC_VIDEO_PACKETS||av_sync.bytes+packet->size>SYNC_VIDEO_BYTES){
        sync_clear_video_locked();av_sync.recover=1;
    }
    int reset=av_sync.recover;
    if(reset&&(!(packet->flags&AV_PKT_FLAG_KEY)||dts<sync_position_locked()-100000)){
        av_sync.dropped++;pthread_mutex_unlock(&av_sync.mutex);av_packet_free(&copy);return 0;
    }
    av_sync.recover=0;
    SyncVideoPacket *p=&av_sync.queue[(av_sync.head+av_sync.count)%SYNC_VIDEO_PACKETS];
    *p=(SyncVideoPacket){copy,dts,pts,reset};av_sync.count++;av_sync.bytes+=packet->size;
    pthread_mutex_unlock(&av_sync.mutex);return 0;
}
static void sync_audio_start(int force){
    pthread_mutex_lock(&av_sync.mutex);
    if(!av_sync.started&&(force||SDL_GetQueuedAudioSize(av_sync.device)>=48000*4/5)){
        av_sync.started=1;
        if(!av_sync.paused)SDL_PauseAudioDevice(av_sync.device,0);
    }
    pthread_mutex_unlock(&av_sync.mutex);
}
static int sync_audio_queue(const uint8_t *pcm,int samples,int64_t timestamp){
    pthread_mutex_lock(&av_sync.mutex);
    int error=SDL_QueueAudio(av_sync.device,pcm,(Uint32)samples*4);
    if(!error){
        int first=!av_sync.clock.ready;
        audio_master_append(&av_sync.clock,timestamp,samples,48000);
        if(first&&av_sync.paused)av_sync.pause_position=av_sync.clock.position;
    }
    pthread_mutex_unlock(&av_sync.mutex);
    if(!error)sync_audio_start(0);
    return error;
}
static CSLVideoStream *sync_close(void){
    if(!av_sync.initialized)return NULL;
    pthread_mutex_lock(&av_sync.mutex);av_sync.quit=1;pthread_mutex_unlock(&av_sync.mutex);
    if(av_sync.threaded)pthread_join(av_sync.thread,NULL);
    sync_clear_video_locked();CSLVideoStream *stream=av_sync.stream;
    pthread_mutex_destroy(&av_sync.mutex);av_sync.initialized=0;return stream;
}
#endif
