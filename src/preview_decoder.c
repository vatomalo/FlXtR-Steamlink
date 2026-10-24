/* Independent muted thumbnail decoder. Never creates a second hardware video plane.
 * Reads a verified HTTPS URL and atomically publishes a capped-rate RGB preview.
 * Intended only for small YouTube H.264 previews; no audio output. */
#define _POSIX_C_SOURCE 200809L
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
#include <SDL.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static volatile sig_atomic_t quit;
static void stop_preview(int signum){(void)signum;quit=1;}
static int write_preview(AVFrame *f){
    if(f->width<1||f->height<1||f->width>1280||f->height>720)return -1;
    if(f->format!=AV_PIX_FMT_YUV420P&&f->format!=AV_PIX_FMT_YUVJ420P&&f->format!=AV_PIX_FMT_NV12)return -1;
    int width=f->width,height=f->height;
    if(width>320){height=height*320/width;width=320;}
    if(height>180){width=width*180/height;height=180;}
    if(width<1||height<1)return -1;
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);
    if(!surface)return -1;
    for(int y=0;y<height;y++){
        int sy=y*f->height/height;
        uint32_t *dst=(uint32_t*)((unsigned char*)surface->pixels+y*surface->pitch);
        for(int x=0;x<width;x++){
            int sx=x*f->width/width;
            int Y=f->data[0][sy*f->linesize[0]+sx];
            int U=f->format==AV_PIX_FMT_NV12?f->data[1][(sy/2)*f->linesize[1]+(sx/2)*2]:f->data[1][(sy/2)*f->linesize[1]+sx/2];
            int V=f->format==AV_PIX_FMT_NV12?f->data[1][(sy/2)*f->linesize[1]+(sx/2)*2+1]:f->data[2][(sy/2)*f->linesize[2]+sx/2];
            int c=Y-16,d=U-128,e=V-128;
            int r=(298*c+409*e+128)>>8,g=(298*c-100*d-208*e+128)>>8,b=(298*c+516*d+128)>>8;
            if(r<0)r=0;
            if(r>255)r=255;
            if(g<0)g=0;
            if(g>255)g=255;
            if(b<0)b=0;
            if(b>255)b=255;
            dst[x]=0xff000000u|((uint32_t)r<<16)|((uint32_t)g<<8)|(uint32_t)b;
        }
    }
    int result=SDL_SaveBMP(surface,"music-cache/preview-frame.next.bmp");
    SDL_FreeSurface(surface);
    if(result<0)return -1;
    return rename("music-cache/preview-frame.next.bmp","music-cache/preview-frame.bmp");
}
int main(int argc,char **argv){
    if(argc!=2||strncmp(argv[1],"https://",8)||strlen(argv[1])>2046)return 2;
    signal(SIGTERM,stop_preview);signal(SIGINT,stop_preview);
    av_log_set_level(AV_LOG_ERROR);avformat_network_init();
    AVFormatContext *format=NULL;AVDictionary *opts=NULL;
    av_dict_set(&opts,"rw_timeout","10000000",0);
    av_dict_set(&opts,"protocol_whitelist","http,https,tcp,tls,crypto",0);
    av_dict_set(&opts,"probesize","524288",0);
    av_dict_set(&opts,"analyzeduration","1000000",0);
    const char *cert=access("certs/cacert.pem",R_OK)?"": "certs/cacert.pem";
    if(*cert)av_dict_set(&opts,"ca_file",cert,0);
    const char *agent=getenv("FLXTR_VIDEO_USER_AGENT");
    if(agent&&strlen(agent)<500&&!strpbrk(agent,"\r\n"))av_dict_set(&opts,"user_agent",agent,0);
    int result=1,track=-1;
    AVCodecContext *decoder=NULL;AVPacket *pkt=NULL;AVFrame *frame=NULL;
    if(avformat_open_input(&format,argv[1],NULL,&opts)<0)goto done;
    if(avformat_find_stream_info(format,NULL)<0)goto done;
    track=av_find_best_stream(format,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);
    if(track<0)goto done;
    AVCodecParameters *params=format->streams[track]->codecpar;
    if(params->codec_id!=AV_CODEC_ID_H264||params->width>640||params->height>360)goto done;
    const AVCodec *codec=avcodec_find_decoder(params->codec_id);
    if(!codec)goto done;
    decoder=avcodec_alloc_context3(codec);
    if(!decoder||avcodec_parameters_to_context(decoder,params)<0)goto done;
    decoder->thread_count=2;
    if(avcodec_open2(decoder,codec,NULL)<0)goto done;
    pkt=av_packet_alloc();frame=av_frame_alloc();if(!pkt||!frame)goto done;
    int64_t began=av_gettime_relative(),last=0,first_pts=AV_NOPTS_VALUE,preview_clock=0;
    unsigned decoded=0,published=0;int64_t max_late=0;
    while(!quit&&av_gettime_relative()-began<45000000&&av_read_frame(format,pkt)>=0){
        if(pkt->stream_index==track&&avcodec_send_packet(decoder,pkt)>=0){
            while(avcodec_receive_frame(decoder,frame)>=0){
                decoded++;
                int64_t now=av_gettime_relative();
                if(frame->best_effort_timestamp!=AV_NOPTS_VALUE){
                    int64_t pts=av_rescale_q(frame->best_effort_timestamp,format->streams[track]->time_base,AV_TIME_BASE_Q);
                    if(first_pts==AV_NOPTS_VALUE){first_pts=pts;preview_clock=now;}
                    int64_t target=preview_clock+pts-first_pts;
                    if(now-target>max_late)max_late=now-target;
                    if(target-began>=45000000){av_frame_unref(frame);result=0;goto done;}
                    while(!quit&&target>now){
                        int64_t wait=target-now;if(wait>10000)wait=10000;
                        SDL_Delay((Uint32)((wait+999)/1000));now=av_gettime_relative();
                    }
                }
                if(quit){av_frame_unref(frame);result=0;goto done;}
                if(now-last>=30000){
                    if(write_preview(frame)<0)goto done;
                    published++;
                    last=now;
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }
    fprintf(stderr,"Preview: decoded=%u published=%u elapsed_ms=%lld max_late_ms=%lld source=%dx%d\n",decoded,published,(long long)((av_gettime_relative()-began)/1000),(long long)(max_late/1000),params->width,params->height);
    result=0;
done:
    av_frame_free(&frame);av_packet_free(&pkt);avcodec_free_context(&decoder);
    avformat_close_input(&format);av_dict_free(&opts);return result;
}
