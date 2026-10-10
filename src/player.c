/* Direct H.264 player. Video stays compressed until Valve SLVideo decodes it.
 * Hardware playback with disk prefill, restart-based seeking and controller overlay.
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
#include <ctype.h>
#include <math.h>
#include <time.h>
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
static int megaplay;
static uint64_t audio_samples;
static int64_t origin=AV_NOPTS_VALUE,clock_start;
/* Disk prefill and rebuffering must not advance subtitle presentation. */
static int subtitle_buffering,disk_read_wait_count;
static void playback_controls(void);
#define DISK_POLL() playback_controls()
#include "disk_buffer.h"
#include "subtitles.h"
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
#include "playback_menu.h"
static void playback_controls(void) {
    menu_poll();
    /* Refresh the playback timeline once per second while the menu is open. */
    static Uint32 menu_next_refresh;
    Uint32 menu_now=SDL_GetTicks();
    if(menu_open&&(Sint32)(menu_now-menu_next_refresh)>=0){
        menu_next_refresh=menu_now+1000;
        menu_draw();
    }
    if(!menu_open&&!overlay_until)subtitle_draw();
    if(change_view&&view_context){change_view=0;viewing=(viewing+1)%VIEW_COUNT;apply_view(1);}
    if(overlay_until&&(Sint32)(SDL_GetTicks()-overlay_until)>=0){SLVideo_HideOverlay(view_overlay);overlay_until=0;subtitle_visible=-2;}
}
static int interrupt_io(void *opaque) { (void)opaque;return stopped||(deadline&&av_gettime_relative()>deadline); }
/* Apply TLS verification to every HLS playlist, key and segment, not only the
 * top-level URL. Never permit a remote playlist to read local files. */
static int open_io(AVFormatContext *fmt,AVIOContext **pb,const char *url,int flags,AVDictionary **opts) {
    if(megaplay){av_dict_set(opts,"referer","https://megaplay.buzz/",0);av_dict_set(opts,"user_agent","FlXtR-Steamlink/0.2",0);}
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
            /* Keep enough audio headroom for brief demux / H.264 submission stalls.
             * 250 ms caused the multiplexed video loop to block frequently. */
            while(!stopped&&SDL_GetQueuedAudioSize(device)>48000*4/2){playback_controls();SDL_Delay(2);}
            audio_samples+=(unsigned)samples;
            if(!stopped&&SDL_QueueAudio(device,pcm,samples*4)<0){av_freep(&pcm);av_frame_unref(frame);return AVERROR(EIO);}
        }
        av_freep(&pcm);av_frame_unref(frame);
        if(samples<0)return samples;
        if(stopped)break;
    }
    return rc==AVERROR(EAGAIN)||rc==AVERROR_EOF?0:rc;
}
/* Load a bounded HTTPS text subtitle track before playback. FFmpeg handles
 * SubRip/WebVTT timestamp parsing; compressed video remains hardware-decoded.
 * Never allow file:, http:, or nested local protocols from provider metadata. */
static void player_subtitle_log(const char *phase,const char *english,const char *norwegian,const char *selected){
    FILE *f=fopen("subtitle-debug.log","a");if(!f)return;
    fprintf(f,"%lu player phase=%s english=%s norwegian=%s selected=%s\n",
        (unsigned long)time(NULL),phase,
        english&&*english?"yes":"no",norwegian&&*norwegian?"yes":"no",
        selected&&*selected?"yes":"no");
    fclose(f);
}
static int load_external_subtitles(const char *url){
    if(!url||strncmp(url,"https://",8)||strlen(url)>1023||strpbrk(url,"\r\n\t"))return -1;
    AVFormatContext *subfmt=avformat_alloc_context();
    if(!subfmt)return -1;
    subfmt->interrupt_callback.callback=interrupt_io;
    AVDictionary *options=NULL;
    /* MegaPlay caption endpoints enforce the same origin headers as video. */
    if(megaplay){
        av_dict_set(&options,"referer","https://megaplay.buzz/",0);
        av_dict_set(&options,"user_agent","FlXtR-Steamlink/0.2",0);
    }
    av_dict_set(&options,"tls_verify","1",0);
    av_dict_set(&options,"ca_file",ca_file,0);
    /* FFmpeg's HLS demuxer opens WebVTT segments through nested HTTPS. */
    av_dict_set(&options,"protocol_whitelist","https,tls,tcp,crypto",0);
    av_dict_set(&options,"allowed_extensions","vtt,webvtt,m3u8",0);
    av_dict_set(&options,"rw_timeout","12000000",0);
    fprintf(stderr,"External subtitle: attempting HTTPS track\n");
    int rc=avformat_open_input(&subfmt,url,NULL,&options);
    av_dict_free(&options);
    if(rc<0){char reason[128];av_strerror(rc,reason,sizeof(reason));fprintf(stderr,"External subtitle URL could not be opened: %s (%d)\n",reason,rc);avformat_free_context(subfmt);return -1;}
    rc=avformat_find_stream_info(subfmt,NULL);
    int stream=-1;
    if(rc>=0)for(unsigned i=0;i<subfmt->nb_streams;i++){
        enum AVCodecID codec=subfmt->streams[i]->codecpar->codec_id;
        if(codec==AV_CODEC_ID_SUBRIP||codec==AV_CODEC_ID_WEBVTT){stream=(int)i;break;}
    }
    if(stream<0){fprintf(stderr,"External subtitle has no SRT/WebVTT stream\n");avformat_close_input(&subfmt);return -1;}
    AVCodecParameters *parameters=subfmt->streams[stream]->codecpar;
    const AVCodec *decoder=avcodec_find_decoder(parameters->codec_id);
    AVCodecContext *context=decoder?avcodec_alloc_context3(decoder):NULL;
    if(!context||avcodec_parameters_to_context(context,parameters)<0||avcodec_open2(context,decoder,NULL)<0){
        avcodec_free_context(&context);avformat_close_input(&subfmt);return -1;
    }
    context->pkt_timebase=subfmt->streams[stream]->time_base;
    AVPacket *packet=av_packet_alloc();int packets=0;
    if(packet){
        /* Cap downloads to avoid untrusted subtitle playlists or endless streams. */
        int64_t start=av_gettime_relative();
        while(packets<4096&&av_gettime_relative()-start<12000000&&!stopped){
            rc=av_read_frame(subfmt,packet);
            if(rc<0)break;
            if(packet->stream_index==stream){subtitle_decode(context,subfmt->streams[stream],packet);packets++;}
            av_packet_unref(packet);
        }
        av_packet_free(&packet);
    }
    avcodec_free_context(&context);avformat_close_input(&subfmt);
    if(!packets)return -1;
    subtitle_external=1;
    fprintf(stderr,"External subtitle loaded: %d packets\n",packets);
    return 0;
}
int main(int argc,char **argv) {
    DiskBuffer *buffer=NULL;
    AVFormatContext *fmt=NULL;AVCodecContext *audio=NULL,*sub_decoder=NULL;AVBSFContext *bsf=NULL;
    AVPacket *packet=NULL,*filtered=NULL;AVFrame *frame=NULL;SwrContext *swr=NULL;
    CSLVideoContext *context=NULL;CSLVideoStream *video=NULL;
    SDL_AudioDeviceID device=0;AVDictionary *opts=NULL;int result=1,vi=-1,ai=-1,rc=0;
    unsigned frames=0;double limit=0;int height_limit=720,buffer_secs=15,buffer_mb=128,si=-1;const char *sub_language="auto";
    if(argc<2){fprintf(stderr,"Usage: greenlink-player URL [test-seconds] [--view fit|stretch|pixel] | --probe URL\n");return 2;}
    int buffer_probe=!strcmp(argv[1],"--probe-buffer");
    int probe=!strcmp(argv[1],"--probe")||buffer_probe;
    if(probe&&argc<3)return 2;
    const char *url=probe?argv[2]:argv[1];
    ca_file=access("certs/cacert.pem",R_OK)?"/etc/ssl/certs/ca-certificates.crt":"certs/cacert.pem";
    for(int i=probe?3:2;i<argc;i++){
        if(!strcmp(argv[i],"--view")&&i+1<argc){
            const char *v=argv[++i];
            if(!strcmp(v,"fit"))viewing=VIEW_FIT;
            else if(!strcmp(v,"stretch"))viewing=VIEW_STRETCH;
            else if(!strcmp(v,"pixel"))viewing=VIEW_PIXEL;
            else return 2;
        }else if(!strcmp(argv[i],"--height")&&i+1<argc){char *end;long h=strtol(argv[++i],&end,10);if(*end||h<0||h>1080)return 2;height_limit=h?(int)h:720;}
        else if(!strcmp(argv[i],"--control-fd")&&i+1<argc){control_fd=atoi(argv[++i]);if(control_fd<3||fcntl(control_fd,F_SETFL,O_NONBLOCK)<0)return 2;}
        else if(!strcmp(argv[i],"--episodes")){menu_episodes=1;}
        else if(!strcmp(argv[i],"--servers")){menu_servers=1;}
        else if(!strcmp(argv[i],"--start")&&i+1<argc){char *end;playback_start=strtod(argv[++i],&end);if(*end||!isfinite(playback_start)||playback_start<0||playback_start>86400)return 2;}
        else if(!strcmp(argv[i],"--buffer-seconds")&&i+1<argc){buffer_secs=atoi(argv[++i]);if(buffer_secs!=5&&buffer_secs!=15&&buffer_secs!=30)return 2;}
        else if(!strcmp(argv[i],"--buffer-mb")&&i+1<argc){buffer_mb=atoi(argv[++i]);if(buffer_mb!=64&&buffer_mb!=128&&buffer_mb!=256)return 2;}
        else if(!strcmp(argv[i],"--subtitles")&&i+1<argc){sub_language=argv[++i];if(strcmp(sub_language,"off")&&strcmp(sub_language,"auto")&&strcmp(sub_language,"eng")&&strcmp(sub_language,"nor")&&strcmp(sub_language,"spa"))return 2;}
        else if(!strcmp(argv[i],"--subtitle-size")&&i+1<argc){subtitle_size=atoi(argv[++i]);if(subtitle_size<2||subtitle_size>3)return 2;}
        else if(!strcmp(argv[i],"--subtitle-delay")&&i+1<argc){subtitle_delay=atoi(argv[++i]);if(subtitle_delay< -5||subtitle_delay>5)return 2;}
        else {char *end;limit=strtod(argv[i],&end);if(*end||limit<=0)return 2;}
    }
    megaplay=getenv("FLXTR_MEDIA_PROVIDER")&&!strcmp(getenv("FLXTR_MEDIA_PROVIDER"),"megaplay");
    /* Faster startup for short-lived anime HLS URLs; retain disk buffering. */
    if(megaplay&&buffer_secs>5){fprintf(stderr,"MegaPlay: reducing initial prebuffer from %d to 5 seconds\n",buffer_secs);buffer_secs=5;}
    menu_height=height_limit;for(int i=0;i<5;i++)if(!strcmp(sub_language,(const char*[]){"off","auto","eng","nor","spa"}[i]))menu_subtitles=i;
    signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGUSR1,next_view);av_log_set_level(AV_LOG_ERROR);avformat_network_init();
    if(SDL_Init(SDL_INIT_TIMER)){fprintf(stderr,"SDL timer: %s\n",SDL_GetError());goto done;}
    fmt=avformat_alloc_context();if(!fmt)goto done;
    fmt->interrupt_callback.callback=interrupt_io;fmt->io_open=open_io;
    av_dict_set(&opts,"rw_timeout","15000000",0);
    av_dict_set(&opts,"tls_verify","1",0);
    av_dict_set(&opts,"ca_file",ca_file,0);
    av_dict_set(&opts,"protocol_whitelist","http,https,tcp,tls,crypto",0);
    if(megaplay){
        av_dict_set(&opts,"referer","https://megaplay.buzz/",0);av_dict_set(&opts,"user_agent","FlXtR-Steamlink/0.2",0);
        /* This host serves MPEG-TS segments with .jpg names. Protocol restrictions
         * still apply to every playlist, key and segment. */
        av_dict_set(&opts,"allowed_extensions","m3u8,ts,m4s,mp4,aac,key,jpg",0);
    }
    av_dict_set(&opts,"probesize","2097152",0);av_dict_set(&opts,"analyzeduration","3000000",0);
    deadline=av_gettime_relative()+20000000;
    rc=avformat_open_input(&fmt,url,NULL,&opts);av_dict_free(&opts);
    if(rc<0){fprintf(stderr,"Could not open stream (%d)\n",rc);goto done;}
    rc=avformat_find_stream_info(fmt,NULL);if(rc<0){fprintf(stderr,"Could not inspect stream (%d)\n",rc);goto done;}
    int best_pixels=0;
    for(unsigned i=0;i<fmt->nb_streams;i++){
        AVCodecParameters *v=fmt->streams[i]->codecpar;const AVPixFmtDescriptor *p=av_pix_fmt_desc_get(v->format);
        if(v->codec_type!=AVMEDIA_TYPE_VIDEO||v->codec_id!=AV_CODEC_ID_H264||v->width<1||v->height<1||v->width>(height_limit<=480?854:height_limit<=720?1280:1920)||v->height>height_limit)continue;
        if(p&&(p->comp[0].depth>8||p->log2_chroma_w!=1||p->log2_chroma_h!=1))continue;
        if(v->width*v->height>best_pixels){best_pixels=v->width*v->height;vi=(int)i;}
    }
    AVCodec *audio_decoder=NULL;
    ai=av_find_best_stream(fmt,AVMEDIA_TYPE_AUDIO,-1,vi,&audio_decoder,0);
    if(vi<0){fprintf(stderr,"No compatible H.264 track at selected quality\n");goto done;}
    if(ai<0){fprintf(stderr,"No decodable audio track; try another server\n");goto done;}
    AVCodecParameters *sound=fmt->streams[ai]->codecpar;
    fprintf(stderr,"Audio: %s; %d channels; %d Hz\n",avcodec_get_name(sound->codec_id),sound->channels,sound->sample_rate);
    /* Prefer requested language, but use unlabelled tracks when hosts omit metadata. */
    if(strcmp(sub_language,"off"))for(int pass=0;pass<2&&si<0;pass++)for(unsigned i=0;i<fmt->nb_streams;i++){
        AVCodecParameters *sp=fmt->streams[i]->codecpar;
        if(sp->codec_type!=AVMEDIA_TYPE_SUBTITLE||!(sp->codec_id==AV_CODEC_ID_SUBRIP||sp->codec_id==AV_CODEC_ID_WEBVTT||sp->codec_id==AV_CODEC_ID_MOV_TEXT||sp->codec_id==AV_CODEC_ID_ASS||sp->codec_id==AV_CODEC_ID_SSA))continue;
        AVDictionaryEntry *language=av_dict_get(fmt->streams[i]->metadata,"language",NULL,0);
        const char *lang=language?language->value:"";
        int matching=!strcmp(sub_language,"auto")||!strcmp(lang,sub_language)||
            (!strcmp(sub_language,"eng")&&(!strcmp(lang,"en")||!strcmp(lang,"en-US")||!strcmp(lang,"en-GB")))||
            (!strcmp(sub_language,"nor")&&(!strcmp(lang,"nb")||!strcmp(lang,"nob")||!strcmp(lang,"no")||!strcmp(lang,"nn")))||
            (!strcmp(sub_language,"spa")&&(!strcmp(lang,"es")||!strcmp(lang,"spa")||!strncmp(lang,"es-",3)));
        if(pass==0&&!matching)continue;
        if(pass==1&&(*lang||!strcmp(sub_language,"auto")))continue;
        const AVCodec *codec=avcodec_find_decoder(sp->codec_id);if(!codec)continue;
        sub_decoder=avcodec_alloc_context3(codec);if(!sub_decoder)break;
        if(avcodec_parameters_to_context(sub_decoder,sp)<0||avcodec_open2(sub_decoder,codec,NULL)<0){avcodec_free_context(&sub_decoder);continue;}
        sub_decoder->pkt_timebase=fmt->streams[i]->time_base;si=(int)i;break;
    }
    fprintf(stderr,"Subtitle mode: %s, selected track: %d%s\n",sub_language,si,si<0?" (no supported embedded text subtitle track)":"");
    /* Prefer a requested external language track, otherwise use embedded text. */
    const char *english=getenv("FLXTR_SUBTITLE_ENGLISH");
    const char *norwegian=getenv("FLXTR_SUBTITLE_NORWEGIAN");
    const char *spanish=getenv("FLXTR_SUBTITLE_SPANISH");
    const char *external=NULL;
    if(strcmp(sub_language,"off")){
        if(!strcmp(sub_language,"nor"))external=norwegian;
        else if(!strcmp(sub_language,"spa"))external=spanish;
        else external=english;
        if((!external||!*external)&&!strcmp(sub_language,"auto"))external=norwegian;
        if((!external||!*external)&&!strcmp(sub_language,"auto"))external=spanish;
    }
    fprintf(stderr,"External subtitle handoff: English %s, Norwegian %s, selected %s\n",
        english&&*english?"available":"absent",norwegian&&*norwegian?"available":"absent",
        external&&*external?"available":"absent");
    player_subtitle_log("handoff",english,norwegian,external);
    if(external&&*external){
        if(load_external_subtitles(external)==0){
            player_subtitle_log("loaded",english,norwegian,external);
            avcodec_free_context(&sub_decoder);si=-1;
            subtitle_seek_us=(int64_t)(playback_start*AV_TIME_BASE);
        }else {player_subtitle_log("download-failed",english,norwegian,external);
            fprintf(stderr,"External subtitles failed, falling back to embedded track\\n");}
    }

    for(unsigned i=0;i<fmt->nb_streams;i++)fmt->streams[i]->discard=((int)i==vi||(int)i==ai||(int)i==si)?AVDISCARD_DEFAULT:AVDISCARD_ALL;
    AVStream *vs=fmt->streams[vi];AVCodecParameters *vp=vs->codecpar;
    const AVPixFmtDescriptor *pix=av_pix_fmt_desc_get(vp->format);
    if(vp->codec_id!=AV_CODEC_ID_H264||vp->width>1920||vp->height>1080||
       (pix&&(pix->comp[0].depth>8||pix->log2_chroma_w!=1||pix->log2_chroma_h!=1))){
        fprintf(stderr,"Requires 8-bit 4:2:0 H.264, at most 1920x1080\n");goto done;
    }
    fprintf(stderr,"Video: H.264 %dx%d; audio track %d\n",vp->width,vp->height,ai);
    media_duration=fmt->duration>0?fmt->duration/(double)AV_TIME_BASE:0;
    if(playback_start>0){
        deadline=av_gettime_relative()+20000000;
        rc=av_seek_frame(fmt,-1,(int64_t)(playback_start*AV_TIME_BASE),AVSEEK_FLAG_BACKWARD);
        deadline=0;
        if(rc<0){fprintf(stderr,"Server does not support seeking (%d)\n",rc);result=44;goto done;}
        fprintf(stderr,"Seek accepted: %.3f seconds (keyframe)\n",playback_start);
    }
    if(probe){
        if(buffer_probe){
            deadline=0;buffer=disk_start(fmt,vi,ai,si,buffer_secs,buffer_mb);if(!buffer)goto done;
            packet=av_packet_alloc();if(!packet)goto done;int video_packets=0,audio_packets=0;
            for(int i=0;i<200;i++){rc=disk_packet(buffer,packet);if(rc<0)break;if(packet->stream_index==vi)video_packets++;if(packet->stream_index==ai)audio_packets++;av_packet_unref(packet);}
            fprintf(stderr,"Buffered packet test: %d video / %d audio\n",video_packets,audio_packets);
            if(!video_packets||!audio_packets)goto done;
        }
        result=0;goto done;
    }
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
    menu_audio=device;
    packet=av_packet_alloc();filtered=av_packet_alloc();frame=av_frame_alloc();
    if(!packet||!filtered||!frame)goto done;
    deadline=0;
    buffer=disk_start(fmt,vi,ai,si,buffer_secs,buffer_mb);
    if(!buffer){fprintf(stderr,"Disk buffer unavailable or insufficient free space\n");goto done;}
    fprintf(stderr,"Disk buffer: %d MiB / %d seconds prefill\n",buffer_mb,buffer_secs);
    show_view("BUFFERING...");
    while(!stopped) {
        playback_controls();
        disk_read_wait_count=0;
        subtitle_buffering=1;
        int64_t wait_start=av_gettime_relative();rc=disk_packet(buffer,packet);
        int64_t waited=av_gettime_relative()-wait_start;
        /* Only genuine FIFO starvation pauses the presentation clock.
         * Short disk reads must not accumulate arbitrary subtitle drift. */
        if(clock_start&&disk_read_wait_count>0)clock_start+=waited;
        subtitle_buffering=0;
        if(overlay_until&&frames==0){SLVideo_HideOverlay(view_overlay);overlay_until=0;subtitle_visible=-2;}
        if(rc<0){
            char message[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(rc,message,sizeof(message));
            fprintf(stderr,"Disk packet read stopped: %d (%s), video frames=%u\n",rc,message,frames);
            break;
        }
        int track=packet->stream_index;
        if(track==si&&sub_decoder){subtitle_decode(sub_decoder,fmt->streams[si],packet);av_packet_unref(packet);subtitle_draw();continue;}
        if(track!=vi&&track!=ai){av_packet_unref(packet);continue;}
        /* Log anomalous decode/presentation timestamp separation without
         * changing decode-order submission to the H.264 hardware. */
        if(track==vi&&packet->dts!=AV_NOPTS_VALUE&&packet->pts!=AV_NOPTS_VALUE){
            int64_t delta=av_rescale_q(packet->pts-packet->dts,
                fmt->streams[vi]->time_base,AV_TIME_BASE_Q);
            static unsigned pts_reports;
            if((delta>250000||delta< -250000)&&pts_reports++<20)
                fprintf(stderr,"Video PTS-DTS difference: %.3f s frame=%u\n",
                    delta/1000000.0,frames);
        }
        int64_t ts=packet->dts!=AV_NOPTS_VALUE?packet->dts:packet->pts;
        if(ts!=AV_NOPTS_VALUE){
            ts=av_rescale_q(ts,fmt->streams[track]->time_base,AV_TIME_BASE_Q);
            if(origin==AV_NOPTS_VALUE){origin=ts;clock_start=av_gettime_relative();}
            int64_t elapsed=ts-origin;
            /* Audio needs time to reach the output device; video needs time
             * to pass through the hardware decoder. Queue both ahead of
             * presentation instead of treating packet submission as display. */
            const int64_t audio_lead_us=120000;
            const int64_t video_lead_us=100000;
            int64_t lead=track==ai?audio_lead_us:video_lead_us;
            if(elapsed>=0 && elapsed<86400LL*AV_TIME_BASE)
                wait_until(clock_start+elapsed-lead);
            if(limit>0&&elapsed>limit*AV_TIME_BASE){av_packet_unref(packet);rc=AVERROR_EOF;break;}
        }
        if(stopped){av_packet_unref(packet);break;}
        if(track==vi){
            rc=av_bsf_send_packet(bsf,packet);if(rc<0)break;
            while((rc=av_bsf_receive_packet(bsf,filtered))>=0){
                int64_t handoff_start=av_gettime_relative();
                if(filtered->size>8*1024*1024||SLVideo_BeginFrame(video,filtered->size)<0||
                   SLVideo_WriteFrameData(video,filtered->data,filtered->size)<0||SLVideo_SubmitFrame(video)<0){
                    fprintf(stderr,"Hardware decoder rejected frame %u\n",frames);av_packet_unref(filtered);goto done;
                }
                static unsigned slow_submissions;
                /* Time the hardware handoff, not just CPU utilization. */
                int64_t submitted_at=av_gettime_relative();
                if(submitted_at-handoff_start>20000&&slow_submissions++<30)
                    fprintf(stderr,"Hardware frame submission delay: %.1f ms frame=%u\n",
                        (submitted_at-handoff_start)/1000.0,frames);
                frames++;av_packet_unref(filtered);
            }
            if(rc==AVERROR(EAGAIN)||rc==AVERROR_EOF)rc=0;
        }else {
            rc=queue_audio(audio,swr,packet,frame,device);
            if(device)SDL_PauseAudioDevice(device,0);
        }
        av_packet_unref(packet);if(rc<0)break;
        if(frames>200&&clock_start&&av_gettime_relative()-clock_start>20000000&&!audio_samples){
            fprintf(stderr,"No audio decoded after 20 seconds; try another server\n");goto done;
        }
    }
    if(rc==AVERROR_EOF&&!stopped){
        if(audio&&queue_audio(audio,swr,NULL,frame,device)<0)goto done;
        while(device&&SDL_GetQueuedAudioSize(device)&&!stopped){playback_controls();SDL_Delay(5);}
        SDL_Delay(150);
    }
    result=(stopped||rc==AVERROR_EOF)?0:1;
    fprintf(stderr,"Hardware frames submitted: %u; result: %d\n",frames,result);
done:
    disk_close(buffer);
    if(menu_overlay){SLVideo_HideOverlay(menu_overlay);SLVideo_FreeOverlay(menu_overlay);}
    if(control_fd>=0)close(control_fd);
    if(requested_action)result=requested_action;
    if(subtitle_overlay){SLVideo_HideOverlay(subtitle_overlay);SLVideo_FreeOverlay(subtitle_overlay);subtitle_overlay=NULL;}
    avcodec_free_context(&sub_decoder);
    fprintf(stderr,"Audio samples queued: %llu\n",(unsigned long long)audio_samples);
    if(view_overlay){SLVideo_HideOverlay(view_overlay);SLVideo_FreeOverlay(view_overlay);view_overlay=NULL;}
    restore_viewport();view_context=NULL;
    if(device)SDL_CloseAudioDevice(device);
    if(video)SLVideo_FreeStream(video);
    if(context)SLVideo_FreeContext(context);
    av_packet_free(&packet);av_packet_free(&filtered);av_frame_free(&frame);av_bsf_free(&bsf);
    swr_free(&swr);avcodec_free_context(&audio);avformat_close_input(&fmt);av_dict_free(&opts);
    avformat_network_deinit();SDL_Quit();return result;
}
