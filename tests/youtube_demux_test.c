#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include "../src/youtube_demux.h"
int main(int argc,char **argv){
    assert(argc==3);
    AVFormatContext *video=NULL,*audio=NULL;
    assert(!avformat_open_input(&video,argv[1],NULL,NULL));assert(!avformat_find_stream_info(video,NULL));
    assert(!avformat_open_input(&audio,argv[2],NULL,NULL));assert(!avformat_find_stream_info(audio,NULL));
    assert(youtube_demux_open(&video,"file:///etc/passwd","","")==AVERROR_INVALIDDATA);
    unsigned original=video->nb_streams;
    assert(!youtube_demux_combine(&video,audio));
    assert(video->nb_streams==original+1&&youtube_demux.video->nb_streams==original);
    assert(av_find_best_stream(video,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0)==(int)original);
    assert(!youtube_demux_seek(video,0));
    AVPacket *packet=av_packet_alloc();assert(packet);
    int counts[2]={0,0},rc;int64_t last=AV_NOPTS_VALUE;
    while((rc=youtube_demux_read(video,packet))>=0){
        int group=packet->stream_index==(int)original;
        counts[group]++;
        int64_t timestamp=packet->dts!=AV_NOPTS_VALUE?packet->dts:packet->pts;
        if(timestamp!=AV_NOPTS_VALUE){
            timestamp=av_rescale_q(timestamp,video->streams[packet->stream_index]->time_base,AV_TIME_BASE_Q);
            assert(last==AV_NOPTS_VALUE||timestamp>=last);last=timestamp;
        }
        av_packet_unref(packet);
    }
    assert(rc==AVERROR_EOF&&counts[0]>10&&counts[1]>10);
    assert(!youtube_demux_seek(video,0));assert(!youtube_demux_read(video,packet));
    av_packet_free(&packet);youtube_demux_close();avformat_close_input(&video);
    puts("PASS: separate compressed audio/video, unchanged input demux streams, timestamp ordering, EOF and seek");
    return 0;
}
