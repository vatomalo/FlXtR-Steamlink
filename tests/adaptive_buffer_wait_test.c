#define _POSIX_C_SOURCE 200809L
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <signal.h>
#include <assert.h>
#include <unistd.h>
#include "../src/adaptive_video.h"
static volatile sig_atomic_t stopped;
static int64_t deadline;
static int disk_read_wait_count,generated,decision;
static AdaptiveVideo policy;
static void starvation_waited(int64_t elapsed){
    int reason;
    decision=adaptive_sample(&policy,av_gettime_relative(),elapsed,0,0,0,0,&reason);
    if(decision){assert(decision==480&&reason==1);stopped=1;}
}
static int network_read(AVFormatContext *format,AVPacket *packet){
    (void)format;
    if(generated>=100){while(!stopped)av_usleep(10000);return AVERROR_EXIT;}
    assert(!av_new_packet(packet,256));packet->stream_index=0;
    packet->dts=packet->pts=generated++*20;packet->duration=20;return 0;
}
#define DISK_RAM
#define DISK_WAITED(elapsed) starvation_waited(elapsed)
#define DISK_READ_PACKET(format,packet) network_read(format,packet)
#include "../src/disk_buffer.h"
int main(void){
    AVFormatContext *fmt=avformat_alloc_context();assert(fmt);
    AVStream *stream=avformat_new_stream(fmt,NULL);assert(stream);stream->time_base=(AVRational){1,1000};
    adaptive_init(&policy,720,720,7,0,av_gettime_relative());policy.armed=0;policy.window=av_gettime_relative();
    DiskBuffer *q=disk_start(fmt,0,-1,-1,1,16);assert(q);
    int64_t actual;assert(!disk_seek_forward(q,900000000,&actual));
    AVPacket *packet=av_packet_alloc();assert(packet);int64_t began=av_gettime_relative();
    while(disk_packet(q,packet)>=0)av_packet_unref(packet);
    assert(decision==480&&av_gettime_relative()-began<2000000);
    disk_close(q);av_packet_free(&packet);avformat_free_context(fmt);
    puts("PASS: FIFO starvation triggers a downgrade while the network read is still blocked, before socket timeout");
}
