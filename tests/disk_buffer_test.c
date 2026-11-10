#define _POSIX_C_SOURCE 200809L
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <dirent.h>
static volatile sig_atomic_t stopped;
static int64_t deadline;
static int generated,fail_at_end,disk_read_wait_count,packet_size=65536;
static int fake_read(AVFormatContext *fmt,AVPacket *p){
    (void)fmt;
    if(generated==2200)return fail_at_end?AVERROR(EIO):AVERROR_EOF;
    assert(!av_new_packet(p,packet_size));memset(p->data,generated%251,(size_t)packet_size);
    p->stream_index=generated%2;p->pts=p->dts=generated*20;p->duration=20;p->pos=generated*65536;
    uint8_t *side=av_packet_new_side_data(p,AV_PKT_DATA_STRINGS_METADATA,8);assert(side);memcpy(side,"key\0val\0",8);
    p->flags=generated%100==0?AV_PKT_FLAG_KEY:0;
    generated++;return 0;
}
#define av_read_frame fake_read
#include "../src/disk_buffer.h"
#undef av_read_frame
int main(void){
    char dir[]="/tmp/flxtr-buffer-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    AVFormatContext *fmt=avformat_alloc_context();assert(fmt);
    for(int i=0;i<2;i++){AVStream *s=avformat_new_stream(fmt,NULL);assert(s);s->time_base=(AVRational){1,1000};}
    for(int pass=0;pass<2;pass++){
        stopped=0;generated=0;fail_at_end=pass;
        DiskBuffer *q=disk_start(fmt,0,1,-1,5,64);assert(q);AVPacket *p=av_packet_alloc();assert(p);
        for(int i=0;i<2200;i++){
            assert(!disk_packet(q,p));assert(p->size==65536&&p->pts==i*20&&p->duration==20&&p->stream_index==i%2);
            for(int j=0;j<p->size;j++)assert(p->data[j]==i%251);
            assert(p->side_data_elems==1&&p->side_data[0].size==8&&!memcmp(p->side_data[0].data,"key\0val\0",8));
            av_packet_unref(p);
        }
        assert(disk_packet(q,p)==(pass?AVERROR(EIO):AVERROR_EOF));
#ifdef DISK_RAM
        assert(q->ram&&q->fd==-1&&q->capacity<=64*1024*1024);
#else
        struct stat st;assert(!fstat(q->fd,&st)&&st.st_size<=64*1024*1024);
#endif
        disk_close(q);av_packet_free(&p);
    }
    stopped=0;generated=0;DiskBuffer *q=disk_start(fmt,0,1,-1,30,64);assert(q);
    av_usleep(100000);disk_close(q); /* also wakes a producer blocked on a full ring */
#ifdef DISK_RAM
    stopped=0;generated=0;fail_at_end=0;packet_size=256;q=disk_start(fmt,0,1,-1,15,16);assert(q);
    for(int n=0;n<100;n++){
        pthread_mutex_lock(&q->mutex);int ready=q->primed;pthread_mutex_unlock(&q->mutex);
        if(ready)break;
        av_usleep(10000);
    }
    pthread_mutex_lock(&q->mutex);assert(q->newest-q->records[q->head].time>=15000000);pthread_mutex_unlock(&q->mutex);
    int64_t actual=-1;assert(disk_seek_forward(q,2100000,&actual)&&actual==4000000);
    AVPacket *packet=av_packet_alloc();assert(!disk_packet(q,packet));
    assert(packet->stream_index==0&&(packet->flags&AV_PKT_FLAG_KEY)&&packet->pts==4000);
    av_packet_unref(packet);int count=q->count;
    assert(!disk_seek_forward(q,900000000,&actual)&&q->count>=count);
    disk_close(q);av_packet_free(&packet);
    assert(access("playback-cache",F_OK));
#else
    DIR *d=opendir("playback-cache");assert(d);struct dirent *entry;int files=0;
    while((entry=readdir(d)))if(entry->d_name[0]!='.')files++;
    closedir(d);assert(!files);
#endif
    avformat_free_context(fmt);rmdir("playback-cache");chdir("/tmp");rmdir(dir);
    #ifdef DISK_RAM
    puts("PASS: RAM FIFO wraparound/integrity, 15s prefill, keyframe seek/miss, memory cap, EOF/error and cancellation without disk files");
#else
    puts("PASS: disk FIFO wraparound/integrity, bounded file, EOF/error draining, cancellation and cleanup");
#endif
    return 0;
}
