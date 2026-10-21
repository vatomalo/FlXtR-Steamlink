/* Direct H.264 player. Video stays compressed until Valve SLVideo decodes it.
 * Hardware playback with bounded RAM prefill, cached forward seeking and controller overlay.
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
#include <sys/stat.h>
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
/* RAM prefill and rebuffering must not advance subtitle presentation. */
static int subtitle_buffering,disk_read_wait_count;
static void playback_controls(void);
static void playback_buffer_wait(int64_t waited);
static int playback_seek(double position);
static int64_t ram_seek=-1,adaptive_resume_at;
static int adaptive_on,adaptive_started,adaptive_slow,adaptive_audio;
static int64_t adaptive_last_audio,adaptive_decoding_since;
#define PLAYBACK_ADAPTIVE adaptive_on
#define PLAYBACK_SEEK(position) playback_seek(position)
#define DISK_POLL() playback_controls()
#define DISK_WAITED(waited) playback_buffer_wait(waited)
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
    if(!rc)rc=hardware_viewport(&rect,screen_w,screen_h,source_w,source_h);
    if(rc){fprintf(stderr,"Viewing mode unavailable: %s\n",view_names[viewing]);if(notify)show_view("VIEW MODE UNAVAILABLE");}
    else {
        fprintf(stderr,"Viewport %s: %d,%d %dx%d (readback verified)\n",view_names[viewing],rect.x,rect.y,rect.w,rect.h);
        if(notify)show_view(view_names[viewing]);
    }
}
#include "playback_menu.h"
#include "playback_started.h"
static void playback_controls(void) {
    int64_t controls_began=av_gettime_relative();
    menu_poll();
    if(av_gettime_relative()-controls_began>250000)adaptive_resume_at=av_gettime_relative();
    /* Atomic, rate-limited position checkpoint for TV resume. */
    static Uint32 last_checkpoint;
    Uint32 tick=SDL_GetTicks();
    if(!stopped&&origin!=AV_NOPTS_VALUE&&clock_start&&
       (Uint32)(tick-last_checkpoint)>=10000){
        last_checkpoint=tick;
        double position=playback_position();
        if(position>=0&&position<86400){
            FILE *f=fopen("playback-position.next","w");
            if(f){
                fprintf(f,"%.3f\n",position);
                if(!fclose(f))rename("playback-position.next","playback-position");
                else unlink("playback-position.next");
            }
        }
    }
    /* Refresh the playback timeline once per second while the menu is open. */
    static Uint32 menu_next_refresh;
    Uint32 menu_now=SDL_GetTicks();
    if(menu_open&&menu_overlay)SLVideo_ShowOverlay(menu_overlay);
    if(menu_open&&(Sint32)(menu_now-menu_next_refresh)>=0){
        menu_next_refresh=menu_now+1000;
        menu_draw();
    }
    if(!menu_open&&!overlay_until)subtitle_draw();
    if(change_view&&view_context){change_view=0;viewing=(viewing+1)%VIEW_COUNT;apply_view(1);}
    if(overlay_until&&(Sint32)(SDL_GetTicks()-overlay_until)>=0){if(!menu_open)SLVideo_HideOverlay(view_overlay);overlay_until=0;subtitle_visible=-2;}
}
static int interrupt_io(void *opaque) { (void)opaque;return stopped||(deadline&&av_gettime_relative()>deadline); }
/* Apply TLS verification to every HLS playlist, key and segment, not only the
 * top-level URL. Never permit a remote playlist to read local files. */
static const char *local_media;
#include "phone_media.h"
static int open_io(AVFormatContext *fmt,AVIOContext **pb,const char *url,int flags,AVDictionary **opts) {
    if(megaplay){av_dict_set(opts,"referer","https://megaplay.buzz/",0);av_dict_set(opts,"user_agent","FlXtR-Steamlink/0.2",0);}
    const char *agent=getenv("FLXTR_VIDEO_USER_AGENT");if(agent&&strlen(agent)<=500&&!strpbrk(agent,"\r\n"))av_dict_set(opts,"user_agent",agent,0);
    av_dict_set(opts,"tls_verify","1",0);
    av_dict_set(opts,"ca_file",ca_file,0);
    av_dict_set(opts,"rw_timeout","15000000",0);
    av_dict_set(opts,"protocol_whitelist",local_media&&!strcmp(local_media,url)?"file":"http,https,tcp,tls,crypto",0);
    return avio_open2(pb,url,flags,&fmt->interrupt_callback,opts);
}
/* Avoid holding the single demux/decoder thread behind a distant timestamp.
 * A stalled packet can otherwise prevent queued audio from being fed.
 * Keep the cap short and log hits for device-side performance diagnosis. */
static void wait_until(int64_t when) {
    const int64_t max_wait_us=40000;
    int64_t began=av_gettime_relative();
    while(!stopped&&av_gettime_relative()<when){
        playback_controls();
        if(av_gettime_relative()-began>=max_wait_us){
            static unsigned pace_caps;
            if(pace_caps++<30)
                fprintf(stderr,"Playback pacing capped: future=%.1f ms\n",
                    (when-av_gettime_relative())/1000.0);
            break;
        }
        SDL_Delay(2);
    }
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
            /* Bound SDL audio latency near 250 ms instead of 500 ms.
             * Excess queued audio otherwise trails the video clock. */
            while(!stopped&&SDL_GetQueuedAudioSize(device)>48000*4/4){playback_controls();SDL_Delay(2);}
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
#include "youtube_demux.h"
#define DISK_RAM
#include "disk_buffer.h"
#include "video_quality.h"
#include "adaptive_video.h"
static AdaptiveVideo adaptive_policy;
static DiskBuffer *active_buffer;
static void adaptive_trace(const char *line){
    fputs(line,stderr);
    struct stat st;
    if(!stat("adaptive-quality.log",&st)&&st.st_size>1048576)
        rename("adaptive-quality.log","adaptive-quality.previous.log");
    FILE *log=fopen("adaptive-quality.log","a");
    if(log){fprintf(log,"%lld %s",(long long)time(NULL),line);fclose(log);}
}
static void adaptive_begin(int actual,int target,int available){
    int blocked=0;const char *mask=getenv("FLXTR_ADAPTIVE_BLOCKED");
    if(mask&&strlen(mask)==1&&mask[0]>='0'&&mask[0]<='7')blocked=mask[0]-'0';
    adaptive_init(&adaptive_policy,actual,target,available,blocked,av_gettime_relative());
    adaptive_policy.report=av_gettime_relative();adaptive_started=1;
}
static void adaptive_evaluate(int64_t starvation){
    if(!adaptive_on||!adaptive_started||stopped)return;
    int64_t now=av_gettime_relative();
    if(adaptive_resume_at){adaptive_hold(&adaptive_policy,now);adaptive_resume_at=0;adaptive_slow=adaptive_audio=0;return;}
    if(adaptive_decoding_since&&now-adaptive_decoding_since>=10000000&&now>=adaptive_policy.armed&&access("adaptive-ready",F_OK)){
        FILE *ready=fopen("adaptive-ready","w");if(ready){fputs("1\n",ready);fclose(ready);}
    }
    pthread_mutex_lock(&active_buffer->mutex);
    int64_t newest=active_buffer->video_newest;
    pthread_mutex_unlock(&active_buffer->mutex);
    double position=playback_position();
    double ahead=newest==AV_NOPTS_VALUE?0:newest/(double)AV_TIME_BASE-position;
    if(ahead<0)ahead=0;
    int reason=0;
    int target=adaptive_sample(&adaptive_policy,now,starvation,adaptive_slow,adaptive_audio,ahead,
                              media_duration<=0||media_duration-position>=30,&reason);
    adaptive_slow=adaptive_audio=0;
    if(now-adaptive_policy.report>=10000000||reason){
        adaptive_policy.report=now;
        char line[256];snprintf(line,sizeof(line),"Auto health: actual=%dp target=%dp ahead=%.1fs starvation=%.0fms decoder=%d audio=%d blocked=%d%s\n",
            adaptive_policy.actual,adaptive_policy.target,ahead,((reason>0&&reason<4)?adaptive_policy.last_starved:adaptive_policy.starved)/1000.0,
            (reason>0&&reason<4)?adaptive_policy.last_slow:adaptive_policy.slow,(reason>0&&reason<4)?adaptive_policy.last_underruns:adaptive_policy.underruns,adaptive_policy.blocked,
            reason&&!target?" / no lower usable rendition":"");
        adaptive_trace(line);
    }
    if(!target)return;
    FILE *f=fopen("adaptive-request.next","w");
    if(!f)return;
    fprintf(f,"%d %.3f %d %d %d\n",target,position,adaptive_policy.blocked,adaptive_policy.actual,reason);
    if(fclose(f)||rename("adaptive-request.next","adaptive-request"))return;
    char message[64];snprintf(message,sizeof(message),"AUTO QUALITY: %dP",target);show_view(message);
    char line[160];snprintf(line,sizeof(line),"Auto decision: %dp -> %dp reason=%d at %.3f sec\n",adaptive_policy.actual,target,reason,position);adaptive_trace(line);
    requested_action=46;stopped=1;
}
static void playback_buffer_wait(int64_t waited){
    /* Called with the FIFO unlocked, even while the network demux is stalled.
     * React before the socket's long timeout; exclude tiny scheduling waits. */
    if(waited>=25000)adaptive_evaluate(waited);
}
static int playback_seek(double position){
    int64_t actual;
    if(active_buffer&&position>playback_position()&&
       disk_seek_forward(active_buffer,(int64_t)(position*AV_TIME_BASE),&actual)){
        ram_seek=actual;paused=0;seek_scrubbing=0;menu_open=0;
        if(menu_overlay)SLVideo_HideOverlay(menu_overlay);
        fprintf(stderr,"RAM seek: %.3f -> %.3f seconds; no network restart\n",position,actual/(double)AV_TIME_BASE);
        return 1;
    }
    menu_restart(1,position);return 0;
}
static int load_external_subtitles(const char *url){
    int local=phone_local_media(url);const char *ext=url?strrchr(url,'.'):NULL;
    if(!url||(!local&&strncmp(url,"https://",8))||strlen(url)>1023||strpbrk(url,"\r\n\t"))return -1;
    if(local&&(!ext||(strcasecmp(ext,".srt")&&strcasecmp(ext,".vtt"))))return -1;
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
    av_dict_set(&options,"protocol_whitelist",local?"file":"https,tls,tcp,crypto",0);
    av_dict_set(&options,"allowed_extensions","vtt,webvtt,m3u8",0);
    av_dict_set(&options,"rw_timeout","12000000",0);
    fprintf(stderr,"External subtitle: attempting %s track\n",local?"uploaded":"HTTPS");
    int rc=avformat_open_input(&subfmt,url,local?av_find_input_format(!strcasecmp(ext,".srt")?"srt":"webvtt"):NULL,&options);
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
    unsigned frames=0;double limit=0;int height_limit=720,buffer_secs=15,buffer_mb=32,si=-1;const char *sub_language="auto";
    if(argc<2){fprintf(stderr,"Usage: greenlink-player URL [test-seconds] [--view fit|stretch|pixel] | --probe URL\n");return 2;}
    int preload_fd=-1;
    int buffer_probe=!strcmp(argv[1],"--probe-buffer");
    int probe=!strcmp(argv[1],"--probe")||buffer_probe;
    if(probe&&argc<3)return 2;
    const char *url=probe?argv[2]:argv[1];
    local_media=phone_local_media(url)?url:NULL;
    ca_file=access("certs/cacert.pem",R_OK)?"/etc/ssl/certs/ca-certificates.crt":"certs/cacert.pem";
    for(int i=probe?3:2;i<argc;i++){
        if(!strcmp(argv[i],"--adaptive")){adaptive_on=1;continue;}
        if(!strcmp(argv[i],"--view")&&i+1<argc){
            const char *v=argv[++i];
            if(!strcmp(v,"fit"))viewing=VIEW_FIT;
            else if(!strcmp(v,"stretch"))viewing=VIEW_STRETCH;
            else if(!strcmp(v,"pixel"))viewing=VIEW_PIXEL;
            else return 2;
        }else if(!strcmp(argv[i],"--height")&&i+1<argc){char *end;long h=strtol(argv[++i],&end,10);if(*end||h<0||h>1080)return 2;height_limit=h?(int)h:720;}
        else if(!strcmp(argv[i],"--control-fd")&&i+1<argc){control_fd=atoi(argv[++i]);if(control_fd<3||fcntl(control_fd,F_SETFL,O_NONBLOCK)<0)return 2;}
        else if(!strcmp(argv[i],"--preload-fd")&&i+1<argc){preload_fd=atoi(argv[++i]);if(preload_fd<3||fcntl(preload_fd,F_SETFL,O_NONBLOCK)<0)return 2;}
        else if(!strcmp(argv[i],"--episodes")){menu_episodes=1;}
        else if(!strcmp(argv[i],"--servers")){menu_servers=1;}
        else if(!strcmp(argv[i],"--start")&&i+1<argc){char *end;playback_start=strtod(argv[++i],&end);if(*end||!isfinite(playback_start)||playback_start<0||playback_start>86400)return 2;}
        else if(!strcmp(argv[i],"--buffer-seconds")&&i+1<argc){buffer_secs=atoi(argv[++i]);if(buffer_secs!=5&&buffer_secs!=15&&buffer_secs!=20&&buffer_secs!=25&&buffer_secs!=30&&buffer_secs!=45&&buffer_secs!=60&&buffer_secs!=80&&buffer_secs!=128)return 2;}
        else if(!strcmp(argv[i],"--buffer-mb")&&i+1<argc){buffer_mb=atoi(argv[++i]);if(buffer_mb!=16&&buffer_mb!=32&&buffer_mb!=64&&buffer_mb!=128&&buffer_mb!=256)return 2;}
        else if(!strcmp(argv[i],"--subtitles")&&i+1<argc){sub_language=argv[++i];if(strcmp(sub_language,"off")&&strcmp(sub_language,"auto")&&strcmp(sub_language,"eng")&&strcmp(sub_language,"nor")&&strcmp(sub_language,"spa"))return 2;}
        else if(!strcmp(argv[i],"--subtitle-size")&&i+1<argc){subtitle_size=atoi(argv[++i]);if(subtitle_size<2||subtitle_size>3)return 2;}
        else if(!strcmp(argv[i],"--subtitle-delay")&&i+1<argc){subtitle_delay=atoi(argv[++i]);if(subtitle_delay< -5||subtitle_delay>5)return 2;}
        else {char *end;limit=strtod(argv[i],&end);if(*end||limit<=0)return 2;}
    }
    if(buffer_secs<15)buffer_secs=15;
    if(buffer_mb>64)buffer_mb=64;
    megaplay=getenv("FLXTR_MEDIA_PROVIDER")&&!strcmp(getenv("FLXTR_MEDIA_PROVIDER"),"megaplay");
    /* All providers use at least 15 seconds of bounded compressed RAM prefill. */
    if(megaplay)fprintf(stderr,"MegaPlay: honoring configured prebuffer of %d seconds (RAM cap %d MiB)\n",buffer_secs,buffer_mb);
    menu_height=height_limit;for(int i=0;i<5;i++)if(!strcmp(sub_language,(const char*[]){"off","auto","eng","nor","spa"}[i]))menu_subtitles=i;
    signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGUSR1,next_view);av_log_set_level(AV_LOG_ERROR);avformat_network_init();
    if(SDL_Init(SDL_INIT_TIMER)){fprintf(stderr,"SDL timer: %s\n",SDL_GetError());goto done;}
    fmt=avformat_alloc_context();if(!fmt)goto done;
    fmt->interrupt_callback.callback=interrupt_io;fmt->io_open=open_io;
    const char *agent=getenv("FLXTR_VIDEO_USER_AGENT");if(agent&&strlen(agent)<=500&&!strpbrk(agent,"\r\n"))av_dict_set(&opts,"user_agent",agent,0);
    av_dict_set(&opts,"rw_timeout","15000000",0);
    av_dict_set(&opts,"tls_verify","1",0);
    av_dict_set(&opts,"ca_file",ca_file,0);
    av_dict_set(&opts,"protocol_whitelist",local_media?"file,http,https,tcp,tls,crypto":"http,https,tcp,tls,crypto",0);
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
    int best_w=0,best_h=0,available=0;
    for(unsigned i=0;i<fmt->nb_streams;i++){
        AVCodecParameters *v=fmt->streams[i]->codecpar;const AVPixFmtDescriptor *p=av_pix_fmt_desc_get(v->format);
        if(v->codec_type!=AVMEDIA_TYPE_VIDEO||v->codec_id!=AV_CODEC_ID_H264||v->width<1||v->height<1||v->width>1920||v->height>1080)continue;
        if(p&&(p->comp[0].depth>8||p->log2_chroma_w!=1||p->log2_chroma_h!=1))continue;
        available|=adaptive_bit(v->height);
        if(video_quality_better(v->width,v->height,height_limit,best_w,best_h)){best_w=v->width;best_h=v->height;vi=(int)i;}
    }
    if(vi>=0&&best_h>height_limit)fprintf(stderr,"No %dp H.264 rendition; using available %dp without transcoding\n",height_limit,best_h);
    const char *separate=getenv("FLXTR_EXTERNAL_AUDIO");
    if(separate&&*separate){
        deadline=av_gettime_relative()+20000000;
        rc=youtube_demux_open(&fmt,separate,ca_file,getenv("FLXTR_VIDEO_USER_AGENT"));deadline=0;
        if(rc<0){fprintf(stderr,"Could not open separate YouTube audio (%d)\n",rc);goto done;}
    }
    AVCodec *audio_decoder=NULL;
    ai=av_find_best_stream(fmt,AVMEDIA_TYPE_AUDIO,-1,vi,&audio_decoder,0);
    if(vi<0){fprintf(stderr,"No compatible H.264 track at selected quality (%dp); requesting higher-resolution retry\n",height_limit);result=45;goto done;}
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
    /* Prefer a compatible embedded text track, then external captions. */
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
    if(si<0&&external&&*external){
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
    if(adaptive_on){
        const char *can_resolve=getenv("FLXTR_ADAPTIVE_RESOLVE");
        if(can_resolve&&!strcmp(can_resolve,"1"))available=7;
        menu_height=vp->height;
        fprintf(stderr,"Auto quality enabled: actual=%dp requested=%dp renditions=%d\n",vp->height,height_limit,available);
    }
    media_duration=fmt->duration>0?fmt->duration/(double)AV_TIME_BASE:0;
    if(playback_start>0){
        deadline=av_gettime_relative()+20000000;
        rc=youtube_demux_seek(fmt,(int64_t)(playback_start*AV_TIME_BASE));
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
    if(preload_fd>=0){
        deadline=0;buffer=disk_start(fmt,vi,ai,si,buffer_secs,buffer_mb);if(!buffer)goto done;
        fprintf(stderr,"TV preloading compressed packets; hardware decoder remains closed\n");
        int ready=0;int64_t until=av_gettime_relative()+1800000000LL;
        while(!stopped&&av_gettime_relative()<until){
            char go;ssize_t n=read(preload_fd,&go,1);
            if(n==1){ready=go=='G';break;}
            if(n==0||(n<0&&errno!=EAGAIN&&errno!=EINTR))break;
            SDL_Delay(50);
        }
        close(preload_fd);preload_fd=-1;if(!ready)goto done;
        pthread_mutex_lock(&buffer->mutex);
        fprintf(stderr,"TV preload released: %d packets / %llu bytes; opening hardware decoder\n",buffer->count,(unsigned long long)buffer->write_bytes);
        pthread_mutex_unlock(&buffer->mutex);
    }
    context=SLVideo_CreateContext();if(!context){fprintf(stderr,"SLVideo context unavailable\n");goto done;}
    /* Normal mode supports B-frames, unlike Moonlight's low-latency mode. */
    video=SLVideo_CreateStream(context,k_ESLVideoFormatH264,0);
    if(!video){fprintf(stderr,"SLVideo stream unavailable\n");goto done;}
    view_context=context;source_w=vp->width;source_h=vp->height;
    AVRational sar=av_guess_sample_aspect_ratio(fmt,vs,NULL);sar_n=sar.num;sar_d=sar.den;
    SLVideo_GetDisplayResolution(context,&screen_w,&screen_h);
    /* The hardware viewport does not exist until the first decoded frame. */
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
    if(!buffer)buffer=disk_start(fmt,vi,ai,si,buffer_secs,buffer_mb);
    if(!buffer){fprintf(stderr,"RAM packet buffer allocation failed\n");goto done;}
    fprintf(stderr,"RAM buffer: %d MiB / %d seconds prefill\n",buffer_mb,buffer_secs);
    active_buffer=buffer;
    show_view("BUFFERING...");
    while(!stopped) {
        playback_controls();
        if(ram_seek>=0){
            int64_t target=ram_seek;ram_seek=-1;
            SDL_ClearQueuedAudio(device);avcodec_flush_buffers(audio);
            swr_close(swr);if(swr_init(swr)<0)goto done;
            av_bsf_flush(bsf);subtitle_seek_us=target;
            if(sub_decoder)avcodec_flush_buffers(sub_decoder);
            SLVideo_FreeStream(video);video=SLVideo_CreateStream(context,k_ESLVideoFormatH264,0);
            if(!video)goto done;
            if(fps.num>0&&fps.den>0)SLVideo_SetStreamTargetFramerate(video,fps.num,fps.den);
            origin=target;clock_start=av_gettime_relative();playback_start=target/(double)AV_TIME_BASE;
            frames=0;subtitle_visible=-2;adaptive_decoding_since=0;adaptive_started=0;adaptive_slow=adaptive_audio=0;
        }
        disk_read_wait_count=0;
        subtitle_buffering=1;
        int64_t wait_start=av_gettime_relative();rc=disk_packet(buffer,packet);
        int64_t waited=av_gettime_relative()-wait_start;
        /* Only genuine FIFO starvation pauses the presentation clock.
         * Short disk reads must not accumulate arbitrary subtitle drift. */
        if(clock_start&&disk_read_wait_count>0)clock_start+=waited;
        subtitle_buffering=0;
        if(ram_seek>=0){av_packet_unref(packet);continue;}
        if(overlay_until&&frames==0){if(!menu_open)SLVideo_HideOverlay(view_overlay);overlay_until=0;subtitle_visible=-2;}
        if(rc<0){
            char message[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(rc,message,sizeof(message));
            fprintf(stderr,"RAM packet read stopped: %d (%s), video frames=%u\n",rc,message,frames);
            break;
        }
        int track=packet->stream_index;
        if(track==si&&sub_decoder){subtitle_decode(sub_decoder,fmt->streams[si],packet);av_packet_unref(packet);if(!menu_open)subtitle_draw();continue;}
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
            const int64_t audio_lead_us=180000;
            const int64_t video_lead_us=100000;
            int64_t lead=track==ai?audio_lead_us:video_lead_us;
            if(elapsed>=0 && elapsed<86400LL*AV_TIME_BASE)
                wait_until(clock_start+elapsed-lead);
            if(limit>0&&elapsed>limit*AV_TIME_BASE){av_packet_unref(packet);rc=AVERROR_EOF;break;}
        }
        if(stopped){av_packet_unref(packet);break;}
        if(ram_seek>=0){av_packet_unref(packet);continue;}
        if(track==vi){
            rc=av_bsf_send_packet(bsf,packet);if(rc<0)break;
            while((rc=av_bsf_receive_packet(bsf,filtered))>=0){
                int64_t handoff_start=av_gettime_relative();
                if(filtered->size>8*1024*1024||SLVideo_BeginFrame(video,filtered->size)<0||
                   SLVideo_WriteFrameData(video,filtered->data,filtered->size)<0||SLVideo_SubmitFrame(video)<0){
                    fprintf(stderr,"Hardware decoder rejected frame %u\n",frames);
                    if(adaptive_on){
                        if(!adaptive_started)adaptive_begin(vp->height,height_limit,available);
                        /* A fatal decoder rejection is stronger evidence than
                         * warmup jitter. Try a lower offered rendition now. */
                        adaptive_policy.armed=0;adaptive_resume_at=0;adaptive_slow+=3;
                        adaptive_evaluate(0);
                    }
                    av_packet_unref(filtered);goto done;
                }
                static unsigned slow_submissions;
                /* Time the hardware handoff, not just CPU utilization. */
                int64_t submitted_at=av_gettime_relative();
                int64_t slow_threshold=250000;
                if(fps.num>0&&fps.den>0){
                    int64_t triple=3000000LL*fps.den/fps.num;
                    if(triple>slow_threshold)slow_threshold=triple;
                }
                if(submitted_at-handoff_start>=slow_threshold)adaptive_slow++;
                if(submitted_at-handoff_start>20000&&slow_submissions++<30)
                    fprintf(stderr,"Hardware frame submission delay: %.1f ms frame=%u\n",
                        (submitted_at-handoff_start)/1000.0,frames);
                frames++;
                if(frames==1){
                    playback_mark_started(playback_position());
                    apply_view(0);
                    if(adaptive_on){adaptive_decoding_since=av_gettime_relative();adaptive_begin(vp->height,height_limit,available);}
                }
                av_packet_unref(filtered);
            }
            if(rc==AVERROR(EAGAIN)||rc==AVERROR_EOF)rc=0;
        }else {
            int64_t audio_now=av_gettime_relative();
            if(adaptive_on&&adaptive_started&&audio_samples&&device&&!paused&&
               !SDL_GetQueuedAudioSize(device)&&audio_now-adaptive_last_audio>=500000){
                adaptive_audio++;adaptive_last_audio=audio_now;
            }
            rc=queue_audio(audio,swr,packet,frame,device);
            if(device)SDL_PauseAudioDevice(device,0);
            /* Compare the queued audio tail with its expected media position.
             * Apply small bounded corrections rather than allowing seconds of
             * audio latency to silently accumulate after stalls. */
            if(device&&ts!=AV_NOPTS_VALUE&&origin!=AV_NOPTS_VALUE&&clock_start){
                static int64_t last_sync;
                int64_t now=av_gettime_relative();
                if(now-last_sync>500000){
                    last_sync=now;
                    int64_t queued_us=(int64_t)SDL_GetQueuedAudioSize(device)*1000000LL/(48000*4);
                    int64_t media_us=ts-origin-queued_us;
                    int64_t wall_us=now-clock_start;
                    int64_t error=media_us-wall_us;
                    if(error>80000||error< -80000){
                        if(error>20000)error=20000;
                        if(error< -20000)error=-20000;
                        clock_start-=error;
                    }
                }
            }
        }
        av_packet_unref(packet);if(rc<0)break;
        adaptive_evaluate(0);
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
    if(preload_fd>=0)close(preload_fd);
    active_buffer=NULL;disk_close(buffer);youtube_demux_close();
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
