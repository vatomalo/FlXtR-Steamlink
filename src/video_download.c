#define _POSIX_C_SOURCE 200809L
#include <libavformat/avformat.h>
#include <libavutil/time.h>
#include <libavutil/pixdesc.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include "video_quality.h"

static volatile sig_atomic_t cancelled;
static int64_t deadline;
static void save_error(const char *message){
    FILE *file=fopen("video-downloads/error","w");if(file){fprintf(file,"%s\n",message);fclose(file);}
}
static void stop_download(int signal_number){(void)signal_number;cancelled=1;}
static int interrupted(void *unused){(void)unused;return cancelled||(deadline&&av_gettime_relative()>deadline);}
static const char *certificate;
static int network_open(AVFormatContext *context,AVIOContext **io,const char *url,int flags,AVDictionary **options){
    if(strncmp(url,"http://",7)&&strncmp(url,"https://",8))return AVERROR(EINVAL);
    av_dict_set(options,"tls_verify","1",0);av_dict_set(options,"ca_file",certificate,0);
    av_dict_set(options,"protocol_whitelist","http,https,tcp,tls,crypto",0);
    return avio_open2(io,url,flags,&context->interrupt_callback,options);
}
static int available_space(void){
    struct statvfs disk;
    return !statvfs("phone-files",&disk)&&(uint64_t)disk.f_bavail*disk.f_frsize>64ULL*1024*1024;
}
static void progress(AVFormatContext *output,double seconds,double duration){
    FILE *file=fopen("video-downloads/progress.next","w");if(!file)return;
    fprintf(file,"%lld %.3f %.3f\n",(long long)avio_tell(output->pb),seconds,duration);
    if(!fclose(file))rename("video-downloads/progress.next","video-downloads/progress");
}
int main(int argc,char **argv){
    if(argc!=4||strlen(argv[2])!=16)return 2;
    for(const char *p=argv[2];*p;p++)if(!strchr("0123456789abcdef",*p))return 2;
    if(strncmp(argv[1],"https://",8)&&strncmp(argv[1],"http://",7))return 2;
    int height=atoi(argv[3]);if(height<1||height>1080)return 2;
    char final[96],partial[96];
    snprintf(final,sizeof(final),"phone-files/offline-%s.mkv",argv[2]);
    snprintf(partial,sizeof(partial),"phone-files/.offline-%s.part",argv[2]);
    if(!access(final,F_OK)){fprintf(stderr,"Download already exists\n");return 3;}
    mkdir("phone-files",0700);mkdir("video-downloads",0700);
    if(!available_space()){save_error("NOT ENOUGH LOCAL STORAGE / KEEP 64 MB FREE");fprintf(stderr,"Not enough local storage: keep 64 MB free\n");return 4;}
    signal(SIGINT,stop_download);signal(SIGTERM,stop_download);
    av_log_set_level(AV_LOG_ERROR);avformat_network_init();
    AVFormatContext *input=avformat_alloc_context(),*output=NULL;
    AVPacket *packet=NULL;AVDictionary *options=NULL;int result=1,rc=0,written=0,video=-1,audio=-1;
    int64_t last_progress=0;double seconds=0,duration=0;int tracks_written[2]={0,0};
    const char *failure="DOWNLOAD FAILED / CHECK VIDEO-DOWNLOAD.LOG";
    if(!input)goto done;
    input->interrupt_callback.callback=interrupted;input->io_open=network_open;
    certificate=access("certs/cacert.pem",R_OK)?"/etc/ssl/certs/ca-certificates.crt":"certs/cacert.pem";
    av_dict_set(&options,"tls_verify","1",0);av_dict_set(&options,"ca_file",certificate,0);
    av_dict_set(&options,"protocol_whitelist","http,https,tcp,tls,crypto",0);
    av_dict_set(&options,"rw_timeout","15000000",0);
    if(getenv("FLXTR_MEDIA_PROVIDER")&&!strcmp(getenv("FLXTR_MEDIA_PROVIDER"),"megaplay")){
        av_dict_set(&options,"referer","https://megaplay.buzz/",0);
        av_dict_set(&options,"user_agent","FlXtR-Steamlink/0.2",0);
        av_dict_set(&options,"allowed_extensions","m3u8,ts,m4s,mp4,aac,key,jpg",0);
    }
    deadline=av_gettime_relative()+30000000;
    rc=avformat_open_input(&input,argv[1],NULL,&options);av_dict_free(&options);if(rc<0)goto done;
    rc=avformat_find_stream_info(input,NULL);if(rc<0)goto done;
    if(input->duration<=0){failure="LIVE OR UNKNOWN-LENGTH STREAM / CHOOSE A FINISHED VIDEO";fprintf(stderr,"Live or unknown-length streams cannot be saved offline\n");goto done;}
    duration=input->duration/(double)AV_TIME_BASE;
    int width=0,best_height=0;
    for(unsigned i=0;i<input->nb_streams;i++){
        AVCodecParameters *codec=input->streams[i]->codecpar;
        const AVPixFmtDescriptor *pixels=av_pix_fmt_desc_get(codec->format);
        if(pixels&&(pixels->comp[0].depth>8||pixels->log2_chroma_w!=1||pixels->log2_chroma_h!=1))continue;
        if(codec->codec_type==AVMEDIA_TYPE_VIDEO&&codec->codec_id==AV_CODEC_ID_H264&&
           codec->width<=1920&&codec->height<=1080&&
           video_quality_better(codec->width,codec->height,height,width,best_height)){
            video=(int)i;width=codec->width;best_height=codec->height;
        }
    }
    audio=av_find_best_stream(input,AVMEDIA_TYPE_AUDIO,-1,video,NULL,0);
    if(video<0||audio<0){failure="SOURCE NEEDS COMPATIBLE H.264 VIDEO AND AUDIO";fprintf(stderr,"Source needs compatible H.264 video and audio\n");goto done;}
    if(avformat_alloc_output_context2(&output,NULL,"matroska",partial)<0||!output)goto done;
    int selected[2]={video,audio};
    for(int i=0;i<2;i++){
        AVStream *out=avformat_new_stream(output,NULL);if(!out)goto done;
        if(avcodec_parameters_copy(out->codecpar,input->streams[selected[i]]->codecpar)<0)goto done;
        out->codecpar->codec_tag=0;out->time_base=input->streams[selected[i]]->time_base;
    }
    if(avio_open(&output->pb,partial,AVIO_FLAG_WRITE)<0)goto done;
    if(avformat_write_header(output,NULL)<0)goto done;
    packet=av_packet_alloc();if(!packet)goto done;
    int64_t first[2]={AV_NOPTS_VALUE,AV_NOPTS_VALUE};
    while(!cancelled){
        deadline=av_gettime_relative()+30000000;rc=av_read_frame(input,packet);
        if(rc==AVERROR_EOF){result=0;break;}if(rc<0)break;
        int index=packet->stream_index==video?0:packet->stream_index==audio?1:-1;
        if(index>=0){
            AVStream *source=input->streams[packet->stream_index],*target=output->streams[index];
            if(packet->dts!=AV_NOPTS_VALUE){
                if(first[index]==AV_NOPTS_VALUE)first[index]=packet->dts;
                seconds=(packet->dts-first[index])*av_q2d(source->time_base);
            }
            // HLS transport streams commonly start at 1.4 s or a wall-clock PTS.
            // Shift both tracks by the same origin to preserve A/V sync offline.
            int64_t origin=input->start_time==AV_NOPTS_VALUE?0:
                av_rescale_q(input->start_time,AV_TIME_BASE_Q,source->time_base);
            if(packet->pts!=AV_NOPTS_VALUE)packet->pts-=origin;
            if(packet->dts!=AV_NOPTS_VALUE)packet->dts-=origin;
            av_packet_rescale_ts(packet,source->time_base,target->time_base);
            packet->stream_index=index;packet->pos=-1;
            rc=av_interleaved_write_frame(output,packet);if(rc<0)break;
            written++;tracks_written[index]++;
        }
        av_packet_unref(packet);
        if(av_gettime_relative()-last_progress>250000){
            if(!available_space()){failure="LOCAL STORAGE FULL / INCOMPLETE VIDEO REMOVED";fprintf(stderr,"Local storage is full (64 MB reserve)\n");break;}
            progress(output,seconds,duration);last_progress=av_gettime_relative();
        }
    }
    if(cancelled||!written||!tracks_written[0]||!tracks_written[1]||seconds<duration-2)result=1;
    if(!result&&av_write_trailer(output)<0)result=1;
done:
    if(rc<0&&rc!=AVERROR_EOF){char error[128];av_strerror(rc,error,sizeof(error));fprintf(stderr,"Download failed: %s\n",error);}
    av_packet_free(&packet);av_dict_free(&options);
    if(output){if(output->pb&&avio_closep(&output->pb)<0)result=1;avformat_free_context(output);}
    avformat_close_input(&input);
    if(!result&&rename(partial,final))result=1;
    if(result){unlink(partial);save_error(cancelled?"DOWNLOAD CANCELLED / INCOMPLETE VIDEO REMOVED":failure);}
    fprintf(stderr,"%s\n",result?"Download failed or cancelled; incomplete file removed":"Download complete; available offline");
    return result;
}
