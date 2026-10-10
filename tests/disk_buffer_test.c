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
static unsigned disk_read_wait_count;
static int generated,fail_at_end;
static int fake_read(AVFormatContext *fmt,AVPacket *p){
    (void)fmt;
    if(generated==2200)return fail_at_end?AVERROR(EIO):AVERROR_EOF;
    assert(!av_new_packet(p,65536));memset(p->data,generated%251,65536);
    p->stream_index=generated%2;p->pts=p->dts=generated*20;p->duration=20;p->pos=generated*65536;
    uint8_t *side=av_packet_new_side_data(p,AV_PKT_DATA_STRINGS_METADATA,8);assert(side);memcpy(side,"key\0val\0",8);
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
        struct stat st;assert(!fstat(q->fd,&st)&&st.st_size<=64*1024*1024);
        disk_close(q);av_packet_free(&p);
    }
    stopped=0;generated=0;DiskBuffer *q=disk_start(fmt,0,1,-1,30,64);assert(q);
    av_usleep(100000);disk_close(q); /* also wakes a producer blocked on a full ring */
    DIR *d=opendir("playback-cache");assert(d);struct dirent *entry;int files=0;
    while((entry=readdir(d)))if(entry->d_name[0]!='.')files++;
    closedir(d);assert(!files);
    avformat_free_context(fmt);rmdir("playback-cache");chdir("/tmp");rmdir(dir);
    puts("PASS: disk FIFO wraparound, packet/side-data integrity, bounded file, EOF/error draining, cancellation and cleanup");
    return 0;
}
