/* Audio-only playback: bounded PCM queue, no video decoder or Python process. */
#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static volatile sig_atomic_t stopped,toggle;
static int paused;
static uint64_t samples_queued;
static int64_t deadline;
static void stop(int n){(void)n;stopped=1;}
static void pause_request(int n){(void)n;toggle=1;}
static int interrupt(void *p){(void)p;return stopped||(deadline&&av_gettime_relative()>deadline);}
static void controls(SDL_AudioDeviceID device){
    if(toggle){toggle=0;paused=!paused;SDL_PauseAudioDevice(device,paused);fprintf(stderr,"Audio %s\n",paused?"paused":"resumed");}
    while(paused&&!stopped){SDL_Delay(20);if(toggle){toggle=0;paused=0;SDL_PauseAudioDevice(device,0);fprintf(stderr,"Audio resumed\n");}}
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    char url[16002],agent[502];FILE *f=fopen(argv[1],"r");if(!f)return 2;
    if(!fgets(url,sizeof(url),f)||!strchr(url,'\n')||!fgets(agent,sizeof(agent),f)){fclose(f);return 2;}
    fclose(f);url[strcspn(url,"\r\n")]=0;agent[strcspn(agent,"\r\n")]=0;
    if(strncmp(url,"https://",8))return 2;
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGUSR1,pause_request);
    av_log_set_level(AV_LOG_ERROR);avformat_network_init();
    AVFormatContext *fmt=avformat_alloc_context();AVCodecContext *decoder=NULL;
    AVPacket *packet=NULL;AVFrame *frame=NULL;SwrContext *swr=NULL;
    SDL_AudioDeviceID device=0;uint8_t *pcm=NULL;int result=1,index=-1,rc,capacity=0;
    if(!fmt)goto done;
    fmt->interrupt_callback=(AVIOInterruptCB){interrupt,NULL};
    AVDictionary *options=NULL;
    av_dict_set(&options,"protocol_whitelist","https,tls,tcp,crypto",0);
    av_dict_set(&options,"tls_verify","1",0);
    av_dict_set(&options,"ca_file",access("certs/cacert.pem",R_OK)?"/etc/ssl/certs/ca-certificates.crt":"certs/cacert.pem",0);
    av_dict_set(&options,"user_agent",agent,0);av_dict_set(&options,"rw_timeout","15000000",0);
    deadline=av_gettime_relative()+20000000;
    rc=avformat_open_input(&fmt,url,NULL,&options);av_dict_free(&options);if(rc<0)goto done;
    if(avformat_find_stream_info(fmt,NULL)<0)goto done;
    AVCodec *codec=NULL;index=av_find_best_stream(fmt,AVMEDIA_TYPE_AUDIO,-1,-1,&codec,0);
    if(index<0||!codec)goto done;
    for(unsigned i=0;i<fmt->nb_streams;i++)fmt->streams[i]->discard=(int)i==index?AVDISCARD_DEFAULT:AVDISCARD_ALL;
    decoder=avcodec_alloc_context3(codec);if(!decoder)goto done;
    if(avcodec_parameters_to_context(decoder,fmt->streams[index]->codecpar)<0||avcodec_open2(decoder,codec,NULL)<0)goto done;
    if(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER)){fprintf(stderr,"Audio init: %s\n",SDL_GetError());goto done;}
    SDL_AudioSpec want={0},got={0};want.freq=48000;want.format=AUDIO_S16SYS;want.channels=2;want.samples=2048;
    device=SDL_OpenAudioDevice(NULL,0,&want,&got,0);if(!device){fprintf(stderr,"Audio output: %s\n",SDL_GetError());goto done;}
    int64_t layout=decoder->channel_layout?(int64_t)decoder->channel_layout:av_get_default_channel_layout(decoder->channels);
    swr=swr_alloc_set_opts(NULL,AV_CH_LAYOUT_STEREO,AV_SAMPLE_FMT_S16,48000,layout,decoder->sample_fmt,decoder->sample_rate,0,NULL);
    if(!swr||swr_init(swr)<0){fprintf(stderr,"Audio converter unavailable\n");goto done;}
    packet=av_packet_alloc();frame=av_frame_alloc();if(!packet||!frame)goto done;
    fprintf(stderr,"Audio-only playback: %s -> stereo 48 kHz\n",codec->name);
    SDL_PauseAudioDevice(device,0);
    while(!stopped){
        controls(device);if(stopped)break;
        while(SDL_GetQueuedAudioSize(device)>48000*4*3&&!stopped){controls(device);SDL_Delay(20);}
        deadline=av_gettime_relative()+20000000;
        rc=av_read_frame(fmt,packet);int eof=rc==AVERROR_EOF;
        if(rc<0&&!eof)break;
        if(eof||packet->stream_index==index){
            rc=avcodec_send_packet(decoder,eof?NULL:packet);
            if(rc<0&&rc!=AVERROR_EOF)break;
            while(avcodec_receive_frame(decoder,frame)>=0){
                int samples=(int)av_rescale_rnd(swr_get_delay(swr,decoder->sample_rate)+frame->nb_samples,48000,decoder->sample_rate,AV_ROUND_UP);
                if(samples<1||samples>192000)goto done;
                if(samples>capacity){av_freep(&pcm);pcm=av_malloc((size_t)samples*4);if(!pcm)goto done;capacity=samples;}
                int n=swr_convert(swr,&pcm,samples,(const uint8_t**)frame->extended_data,frame->nb_samples);
                if(n<0||SDL_QueueAudio(device,pcm,(Uint32)n*4))goto done;
                samples_queued+=(unsigned)n;
                av_frame_unref(frame);
            }
        }
        av_packet_unref(packet);
        if(eof){
            while(SDL_GetQueuedAudioSize(device)&&!stopped){controls(device);SDL_Delay(20);}
            result=0;break;
        }
    }
    if(stopped)result=0;
done:
    fprintf(stderr,"Audio samples queued: %llu\n",(unsigned long long)samples_queued);
    if(result)fprintf(stderr,"Audio playback failed; see music lookup/playback log\n");
    if(device)SDL_CloseAudioDevice(device);
    av_freep(&pcm);swr_free(&swr);av_frame_free(&frame);av_packet_free(&packet);
    avcodec_free_context(&decoder);avformat_close_input(&fmt);SDL_Quit();return result;
}
