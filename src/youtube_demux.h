/* Interleave two compressed inputs into the existing bounded disk FIFO.
 * The original demux contexts stay intact: no synthetic streams are appended
 * to an input demuxer (MOV demuxers keep private state for each real stream).
 */
#ifndef FLXTR_YOUTUBE_DEMUX_H
#define FLXTR_YOUTUBE_DEMUX_H
#include <libavutil/mathematics.h>
static struct {
    AVFormatContext *video,*audio;
    AVPacket *pending[2];
    int ready[2],ended[2],audio_track,output_audio;
} youtube_demux;
static void youtube_demux_close(void){
    av_packet_free(&youtube_demux.pending[0]);av_packet_free(&youtube_demux.pending[1]);
    avformat_close_input(&youtube_demux.video);avformat_close_input(&youtube_demux.audio);
    memset(&youtube_demux,0,sizeof(youtube_demux));
}
static AVStream *youtube_copy_stream(AVFormatContext *dest,AVStream *source){
    AVStream *stream=avformat_new_stream(dest,NULL);if(!stream)return NULL;
    if(avcodec_parameters_copy(stream->codecpar,source->codecpar)<0)return NULL;
    stream->time_base=source->time_base;stream->avg_frame_rate=source->avg_frame_rate;
    stream->r_frame_rate=source->r_frame_rate;stream->sample_aspect_ratio=source->sample_aspect_ratio;
    stream->start_time=source->start_time;stream->duration=source->duration;
    stream->disposition=source->disposition;av_dict_copy(&stream->metadata,source->metadata,0);
    return stream;
}
/* Success takes ownership of both original input contexts. */
static int youtube_demux_combine(AVFormatContext **video,AVFormatContext *audio){
    int track=av_find_best_stream(audio,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);
    if(track<0)return track;
    AVFormatContext *merged=avformat_alloc_context();if(!merged)return AVERROR(ENOMEM);
    for(unsigned i=0;i<(*video)->nb_streams;i++)
        if(!youtube_copy_stream(merged,(*video)->streams[i])){avformat_free_context(merged);return AVERROR(ENOMEM);}
    AVStream *sound=youtube_copy_stream(merged,audio->streams[track]);
    if(!sound){avformat_free_context(merged);return AVERROR(ENOMEM);}
    AVPacket *one=av_packet_alloc(),*two=av_packet_alloc();
    if(!one||!two){av_packet_free(&one);av_packet_free(&two);avformat_free_context(merged);return AVERROR(ENOMEM);}
    merged->duration=(*video)->duration;merged->start_time=(*video)->start_time;
    merged->interrupt_callback=(*video)->interrupt_callback;
    youtube_demux.video=*video;youtube_demux.audio=audio;
    youtube_demux.audio_track=track;youtube_demux.output_audio=sound->index;
    youtube_demux.pending[0]=one;youtube_demux.pending[1]=two;
    *video=merged;return 0;
}
static int youtube_demux_open(AVFormatContext **video,const char *url,const char *ca,const char *agent){
    if(!url||!*url)return 0;
    if(strncmp(url,"https://",8)||strlen(url)>16000||strpbrk(url,"\r\n"))return AVERROR_INVALIDDATA;
    AVFormatContext *audio=avformat_alloc_context();if(!audio)return AVERROR(ENOMEM);
    audio->interrupt_callback=(*video)->interrupt_callback;
    AVDictionary *opts=NULL;
    av_dict_set(&opts,"protocol_whitelist","https,tls,tcp,crypto",0);
    av_dict_set(&opts,"tls_verify","1",0);av_dict_set(&opts,"ca_file",ca,0);
    av_dict_set(&opts,"rw_timeout","15000000",0);
    if(agent&&strlen(agent)<=500&&!strpbrk(agent,"\r\n"))av_dict_set(&opts,"user_agent",agent,0);
    int rc=avformat_open_input(&audio,url,NULL,&opts);av_dict_free(&opts);
    if(rc>=0)rc=avformat_find_stream_info(audio,NULL);
    if(rc>=0)rc=youtube_demux_combine(video,audio);
    if(rc<0)avformat_close_input(&audio);
    return rc;
}
static int youtube_demux_read(AVFormatContext *format,AVPacket *packet){
    if(!youtube_demux.video)return av_read_frame(format,packet);
    for(int i=0;i<2;i++){
        if(youtube_demux.ready[i]||youtube_demux.ended[i])continue;
        AVFormatContext *input=i?youtube_demux.audio:youtube_demux.video;
        int rc;
        for(;;){
            rc=av_read_frame(input,youtube_demux.pending[i]);
            if(rc<0)break;
            if(!i||youtube_demux.pending[i]->stream_index==youtube_demux.audio_track)break;
            av_packet_unref(youtube_demux.pending[i]);
        }
        if(rc==AVERROR_EOF){youtube_demux.ended[i]=1;continue;}
        if(rc<0)return rc;
        if(youtube_demux.pending[i]->size>8*1024*1024)return AVERROR_INVALIDDATA;
        youtube_demux.ready[i]=1;
    }
    if(!youtube_demux.ready[0]&&!youtube_demux.ready[1])return AVERROR_EOF;
    int choice=youtube_demux.ready[0]?0:1;
    if(youtube_demux.ready[0]&&youtube_demux.ready[1]){
        AVPacket *v=youtube_demux.pending[0],*a=youtube_demux.pending[1];
        int64_t vt=v->dts!=AV_NOPTS_VALUE?v->dts:v->pts,at=a->dts!=AV_NOPTS_VALUE?a->dts:a->pts;
        if(vt!=AV_NOPTS_VALUE&&at!=AV_NOPTS_VALUE&&
           av_compare_ts(at,youtube_demux.audio->streams[a->stream_index]->time_base,
                         vt,youtube_demux.video->streams[v->stream_index]->time_base)<0)choice=1;
    }
    av_packet_move_ref(packet,youtube_demux.pending[choice]);youtube_demux.ready[choice]=0;
    if(choice)packet->stream_index=youtube_demux.output_audio;
    return 0;
}
static int youtube_demux_seek(AVFormatContext *format,int64_t time){
    if(!youtube_demux.video)return av_seek_frame(format,-1,time,AVSEEK_FLAG_BACKWARD);
    int rc=av_seek_frame(youtube_demux.video,-1,time,AVSEEK_FLAG_BACKWARD);
    if(rc>=0)rc=av_seek_frame(youtube_demux.audio,-1,time,AVSEEK_FLAG_BACKWARD);
    for(int i=0;i<2;i++){av_packet_unref(youtube_demux.pending[i]);youtube_demux.ready[i]=youtube_demux.ended[i]=0;}
    return rc;
}
#define DISK_READ_PACKET(format,packet) youtube_demux_read(format,packet)
#endif
