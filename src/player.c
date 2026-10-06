/* Direct H.264 player. Video stays compressed until Valve SLVideo decodes it.
 * Prototype: sequential playback/stop only; seeking/subtitles are not implemented.
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <SLVideo.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "hardware_layout.h"
#include "font.h"

static volatile sig_atomic_t stopped;
static volatile sig_atomic_t change_view;
static int viewing=VIEW_FIT,screen_w,screen_h,source_w,source_h,sar_n=1,sar_d=1;
static CSLVideoContext *view_context;
static CSLVideoOverlay *view_overlay;
static Uint32 overlay_until;
static int64_t deadline;
static const char *ca_file;
static void stop(int sig) { (void)sig;stopped=1; }
static void next_view(int sig) { (void)sig;change_view=1; }
static void show_view(const char *label) {
    if(!view_overlay)view_overlay=SLVideo_CreateOverlay(view_context,480,64);
    if(!view_overlay)return;
    uint32_t *pixels=NULL;int pitch=0;
    SLVideo_HideOverlay(view_overlay);
    SLVideo_GetOverlayPixels(view_overlay,&pixels,&pitch);
    if(!pixels||pitch<480*4)return;
    for(int y=0;y<64;y++){
        uint32_t *row=(uint32_t*)((char*)pixels+y*pitch);
        for(int x=0;x<480;x++)row[x]=0xe8000000;
    }
    for(int n=0;label[n]&&n<38;n++){
        size_t i;
        for(i=0;i<sizeof(glyphs)/sizeof(glyphs[0]);i++)if(glyphs[i].c==label[n])break;
        if(i==sizeof(glyphs)/sizeof(glyphs[0]))continue;
        for(int y=0;y<7;y++)for(int x=0;x<5;x++)if(glyphs[i].row[y]&(16>>x))
            for(int dy=0;dy<2;dy++)for(int dx=0;dx<2;dx++)
                ((uint32_t*)((char*)pixels+(24+y*2+dy)*pitch))[12+n*12+x*2+dx]=0xff74ff84;
    }
    SLVideo_SetOverlayDisplayArea(view_overlay,0.05f,0.83f,0.5f,0.12f);
    SLVideo_ShowOverlay(view_overlay);overlay_until=SDL_GetTicks()+2500;
}
static void apply_view(int notify) {
    VideoRect rect;
    int rc=video_rect(viewing,source_w,source_h,sar_n,sar_d,screen_w,screen_h,&rect);
    if(!rc)rc=hardware_viewport(&rect);
    if(rc){fprintf(stderr,"Viewing mode unavailable: %s\n",view_names[viewing]);if(notify)show_view("VIEW MODE UNAVAILABLE");}
    else {
        fprintf(stderr,"Viewport %s: %d,%d %dx%d (readback verified)\n",view_names[viewing],rect.x,rect.y,rect.w,rect.h);
        if(notify)show_view(view_names[viewing]);
    }
}
static void playback_controls(void) {
    if(change_view&&view_context){change_view=0;viewing=(viewing+1)%VIEW_COUNT;apply_view(1);}
    if(overlay_until&&(Sint32)(SDL_GetTicks()-overlay_until)>=0){SLVideo_HideOverlay(view_overlay);overlay_until=0;}
}
static int interrupt_io(void *opaque) { (void)opaque;return stopped||(deadline&&av_gettime_relative()>deadline); }
/* Apply TLS verification to every HLS playlist, key and segment, not only the
 * top-level URL. Never permit a remote playlist to read local files. */
static int open_io(AVFormatContext *fmt,AVIOContext **pb,const char *url,int flags,AVDictionary **opts) {
    av_dict_set(opts,"tls_verify","1",0);
    av_dict_set(opts,"ca_file",ca_file,0);
    av_dict_set(opts,"rw_timeout","15000000",0);
    av_dict_set(opts,"protocol_whitelist","http,https,tcp,tls,crypto",0);
    return avio_open2(pb,url,flags,&fmt->interrupt_callback,opts);
}
static void wait_until(int64_t when) {
    while(!stopped&&av_gettime_relative()<when){playback_controls();SDL_Delay(2);}
}
static int queue_audio(AVCodecContext *codec,SwrContext *swr,AVPacket *pkt,AVFrame *frame,SDL_AudioDeviceID device) {
    int rc=avcodec_send_packet(codec,pkt);
    if(rc<0&&rc!=AVERROR_EOF)return rc;
    while((rc=avcodec_receive_frame(codec,frame))>=0) {
        int out_count=av_rescale_rnd(swr_get_delay(swr,codec->sample_rate)+frame->nb_samples,48000,codec->sample_rate,AV_ROUND_UP);
        if(out_count<0||out_count>48000){av_frame_unref(frame);return AVERROR(EINVAL);}
        uint8_t *pcm=NULL;
        if(av_samples_alloc(&pcm,NULL,2,out_count,AV_SAMPLE_FMT_S16,0)<0){av_frame_unref(frame);return AVERROR(ENOMEM);}
        int samples=swr_convert(swr,&pcm,out_count,(const uint8_t**)frame->extended_data,frame->nb_samples);
        if(samples>0) {
            while(!stopped&&SDL_GetQueuedAudioSize(device)>48000*4/2){playback_controls();SDL_Delay(5);}
            if(!stopped&&SDL_QueueAudio(device,pcm,samples*4)<0){av_freep(&pcm);av_frame_unref(frame);return AVERROR(EIO);}
        }
        av_freep(&pcm);av_frame_unref(frame);
        if(samples<0)return samples;
        if(stopped)break;
    }
    return rc==AVERROR(EAGAIN)||rc==AVERROR_EOF?0:rc;
}
int main(int argc,char **argv) {
    AVFormatContext *fmt=NULL;AVCodecContext *audio=NULL;AVBSFContext *bsf=NULL;
    AVPacket *packet=NULL,*filtered=NULL;AVFrame *frame=NULL;SwrContext *swr=NULL;
    CSLVideoContext *context=NULL;CSLVideoStream *video=NULL;
    SDL_AudioDeviceID device=0;AVDictionary *opts=NULL;int result=1,vi=-1,ai=-1,rc=0;
    int64_t origin=AV_NOPTS_VALUE,clock_start=0;unsigned frames=0;double limit=0;
    if(argc<2){fprintf(stderr,"Usage: greenlink-player URL [test-seconds] [--view fit|stretch|pixel] | --probe URL\n");return 2;}
    int probe=!strcmp(argv[1],"--probe");
    if(probe&&argc!=3)return 2;
    const char *url=probe?argv[2]:argv[1];
    ca_file=access("certs/cacert.pem",R_OK)?"/etc/ssl/certs/ca-certificates.crt":"certs/cacert.pem";
    if(!probe)for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--view")&&i+1<argc){
            const char *v=argv[++i];
            if(!strcmp(v,"fit"))viewing=VIEW_FIT;
            else if(!strcmp(v,"stretch"))viewing=VIEW_STRETCH;
            else if(!strcmp(v,"pixel"))viewing=VIEW_PIXEL;
            else return 2;
        }else {char *end;limit=strtod(argv[i],&end);if(*end||limit<=0)return 2;}
    }
    signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGUSR1,next_view);av_log_set_level(AV_LOG_ERROR);avformat_network_init();
    if(SDL_Init(SDL_INIT_TIMER)){fprintf(stderr,"SDL timer: %s\n",SDL_GetError());goto done;}
    fmt=avformat_alloc_context();if(!fmt)goto done;
    fmt->interrupt_callback.callback=interrupt_io;fmt->io_open=open_io;
    av_dict_set(&opts,"rw_timeout","15000000",0);
    av_dict_set(&opts,"tls_verify","1",0);
    av_dict_set(&opts,"ca_file",ca_file,0);
    av_dict_set(&opts,"protocol_whitelist","http,https,tcp,tls,crypto",0);
    av_dict_set(&opts,"probesize","2097152",0);av_dict_set(&opts,"analyzeduration","3000000",0);
    deadline=av_gettime_relative()+20000000;
    rc=avformat_open_input(&fmt,url,NULL,&opts);av_dict_free(&opts);
    if(rc<0){fprintf(stderr,"Could not open stream (%d)\n",rc);goto done;}
    rc=avformat_find_stream_info(fmt,NULL);if(rc<0){fprintf(stderr,"Could not inspect stream (%d)\n",rc);goto done;}
    vi=av_find_best_stream(fmt,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);
    ai=av_find_best_stream(fmt,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);
    if(vi<0){fprintf(stderr,"No video track\n");goto done;}
    AVStream *vs=fmt->streams[vi];AVCodecParameters *vp=vs->codecpar;
    const AVPixFmtDescriptor *pix=av_pix_fmt_desc_get(vp->format);
    if(vp->codec_id!=AV_CODEC_ID_H264||vp->width>1920||vp->height>1080||
       (pix&&(pix->comp[0].depth>8||pix->log2_chroma_w!=1||pix->log2_chroma_h!=1))){
        fprintf(stderr,"Requires 8-bit 4:2:0 H.264, at most 1920x1080\n");goto done;
    }
    fprintf(stderr,"Video: H.264 %dx%d; audio track %d\n",vp->width,vp->height,ai);
    if(probe){result=0;goto done;}
    context=SLVideo_CreateContext();if(!context){fprintf(stderr,"SLVideo context unavailable\n");goto done;}
    /* Normal mode supports B-frames, unlike Moonlight's low-latency mode. */
    video=SLVideo_CreateStream(context,k_ESLVideoFormatH264,0);
    if(!video){fprintf(stderr,"SLVideo stream unavailable\n");goto done;}
    view_context=context;source_w=vp->width;source_h=vp->height;
    AVRational sar=av_guess_sample_aspect_ratio(fmt,vs,NULL);sar_n=sar.num;sar_d=sar.den;
    SLVideo_GetDisplayResolution(context,&screen_w,&screen_h);apply_view(0);
    AVRational fps=av_guess_frame_rate(fmt,vs,NULL);
    if(fps.num>0&&fps.den>0)SLVideo_SetStreamTargetFramerate(video,fps.num,fps.den);
    rc=av_bsf_alloc(av_bsf_get_by_name("h264_mp4toannexb"),&bsf);if(rc<0)goto done;
    rc=avcodec_parameters_copy(bsf->par_in,vp);if(rc<0)goto done;
    bsf->time_base_in=vs->time_base;rc=av_bsf_init(bsf);if(rc<0)goto done;
    if(ai>=0) {
        if(SDL_InitSubSystem(SDL_INIT_AUDIO)){fprintf(stderr,"SDL audio: %s\n",SDL_GetError());goto done;}
        AVCodecParameters *ap=fmt->streams[ai]->codecpar;
        const AVCodec *codec=avcodec_find_decoder(ap->codec_id);
        if(!codec){fprintf(stderr,"Unsupported audio codec\n");goto done;}
        audio=avcodec_alloc_context3(codec);if(!audio)goto done;
        if(avcodec_parameters_to_context(audio,ap)<0||avcodec_open2(audio,codec,NULL)<0)goto done;
        int64_t layout=audio->channel_layout?(int64_t)audio->channel_layout:av_get_default_channel_layout(audio->channels);
        swr=swr_alloc_set_opts(NULL,AV_CH_LAYOUT_STEREO,AV_SAMPLE_FMT_S16,48000,layout,audio->sample_fmt,audio->sample_rate,0,NULL);
        if(!swr||swr_init(swr)<0)goto done;
        SDL_AudioSpec wanted;SDL_zero(wanted);wanted.freq=48000;wanted.format=AUDIO_S16SYS;wanted.channels=2;wanted.samples=1024;
        device=SDL_OpenAudioDevice(NULL,0,&wanted,NULL,0);
        if(!device){fprintf(stderr,"Audio output unavailable: %s\n",SDL_GetError());goto done;}
    }
    packet=av_packet_alloc();filtered=av_packet_alloc();frame=av_frame_alloc();
    if(!packet||!filtered||!frame)goto done;
    deadline=0;
    while(!stopped) {
        playback_controls();
        deadline=av_gettime_relative()+15000000;rc=av_read_frame(fmt,packet);deadline=0;
        if(rc<0)break;
        int track=packet->stream_index;
        if(track!=vi&&track!=ai){av_packet_unref(packet);continue;}
        int64_t ts=packet->dts!=AV_NOPTS_VALUE?packet->dts:packet->pts;
        if(ts!=AV_NOPTS_VALUE){
            ts=av_rescale_q(ts,fmt->streams[track]->time_base,AV_TIME_BASE_Q);
            if(origin==AV_NOPTS_VALUE){origin=ts;clock_start=av_gettime_relative();}
            int64_t elapsed=ts-origin;
            if(elapsed>0 && elapsed<86400LL*AV_TIME_BASE)wait_until(clock_start+elapsed);
            if(limit>0&&elapsed>limit*AV_TIME_BASE){av_packet_unref(packet);rc=AVERROR_EOF;break;}
        }
        if(stopped){av_packet_unref(packet);break;}
        if(track==vi){
            rc=av_bsf_send_packet(bsf,packet);if(rc<0)break;
            while((rc=av_bsf_receive_packet(bsf,filtered))>=0){
                if(filtered->size>8*1024*1024||SLVideo_BeginFrame(video,filtered->size)<0||
                   SLVideo_WriteFrameData(video,filtered->data,filtered->size)<0||SLVideo_SubmitFrame(video)<0){
                    fprintf(stderr,"Hardware decoder rejected frame %u\n",frames);av_packet_unref(filtered);goto done;
                }
                frames++;av_packet_unref(filtered);
            }
            if(rc==AVERROR(EAGAIN)||rc==AVERROR_EOF)rc=0;
        }else {
            rc=queue_audio(audio,swr,packet,frame,device);
            if(device)SDL_PauseAudioDevice(device,0);
        }
        av_packet_unref(packet);if(rc<0)break;
    }
    if(rc==AVERROR_EOF&&!stopped){
        if(audio&&queue_audio(audio,swr,NULL,frame,device)<0)goto done;
        while(device&&SDL_GetQueuedAudioSize(device)&&!stopped)SDL_Delay(5);
        SDL_Delay(150);
    }
    result=(stopped||rc==AVERROR_EOF)?0:1;
    fprintf(stderr,"Hardware frames submitted: %u; result: %d\n",frames,result);
done:
    if(view_overlay){SLVideo_HideOverlay(view_overlay);SLVideo_FreeOverlay(view_overlay);view_overlay=NULL;}
    restore_viewport();view_context=NULL;
    if(device)SDL_CloseAudioDevice(device);
    if(video)SLVideo_FreeStream(video);
    if(context)SLVideo_FreeContext(context);
    av_packet_free(&packet);av_packet_free(&filtered);av_frame_free(&frame);av_bsf_free(&bsf);
    swr_free(&swr);avcodec_free_context(&audio);avformat_close_input(&fmt);av_dict_free(&opts);
    avformat_network_deinit();SDL_Quit();return result;
}
