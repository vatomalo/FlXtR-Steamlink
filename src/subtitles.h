/* Small text-only subtitle renderer on SLVideo's overlay plane. */
typedef struct {int64_t start,end;char text[512];} SubtitleCue;
static SubtitleCue subtitle_cues[64];
static SubtitleCue *external_cues;
static int external_count,subtitle_loading;
static unsigned subtitle_rendered;
static CSLVideoOverlay *subtitle_overlay;
static int subtitle_size=2,subtitle_delay,subtitle_visible=-1;
#include "subtitle_text.h"
static void subtitle_decode(AVCodecContext *decoder,AVStream *stream,AVPacket *packet){
    AVSubtitle sub={0};int got=0;
    if(avcodec_decode_subtitle2(decoder,&sub,&got,packet)<0||!got){avsubtitle_free(&sub);return;}
    int64_t start=packet->pts==AV_NOPTS_VALUE?0:av_rescale_q(packet->pts,stream->time_base,AV_TIME_BASE_Q);
    int64_t duration=sub.end_display_time>sub.start_display_time?(int64_t)(sub.end_display_time-sub.start_display_time)*1000:
        packet->duration>0?av_rescale_q(packet->duration,stream->time_base,AV_TIME_BASE_Q):5000000;
    start+=(int64_t)sub.start_display_time*1000;
    for(unsigned i=0;i<sub.num_rects;i++){
        const char *text=sub.rects[i]->text?sub.rects[i]->text:sub.rects[i]->ass;
        if(!text)continue;
        int slot=0;SubtitleCue *cues=subtitle_cues;
        if(subtitle_loading){if(external_count>=8192)break;cues=external_cues;slot=external_count++;}
        else for(int j=1;j<64;j++)if(cues[j].end<cues[slot].end)slot=j;
        cues[slot].start=start;cues[slot].end=start+duration;
        subtitle_text(cues[slot].text,sizeof(cues[slot].text),text,!sub.rects[i]->text);
        if(subtitle_visible==slot)subtitle_visible=-2;
    }avsubtitle_free(&sub);
}
static int subtitle_order(const void *a,const void *b){
    const SubtitleCue *x=a,*y=b;return (x->start>y->start)-(x->start<y->start);
}
static void subtitle_load_external(const char *path,int64_t offset){
    AVFormatContext *fmt=avformat_alloc_context();AVCodecContext *decoder=NULL;AVPacket *packet=NULL;AVDictionary *options=NULL;
    if(!fmt)return;
    char temporary[]="/tmp/flxtr-subtitle-XXXXXX";int temporary_fd=-1;
    fmt->interrupt_callback.callback=interrupt_io;
    av_dict_set(&options,"protocol_whitelist",strstr(path,"://")?"http,https,tcp,tls":"file",0);
    av_dict_set(&options,"format_whitelist","srt,webvtt,ass",0);
    av_dict_set(&options,"tls_verify","1",0);av_dict_set(&options,"ca_file",ca_file,0);
    av_dict_set(&options,"rw_timeout","5000000",0);
    deadline=av_gettime_relative()+10000000;
    /* Subtitle demuxers may read the entire file during open: cap input first. */
    if(strstr(path,"://")){
        AVIOContext *input=NULL;
        if(avio_open2(&input,path,AVIO_FLAG_READ,&fmt->interrupt_callback,&options)<0)goto done;
        temporary_fd=mkstemp(temporary);
        int bytes=0,n=0,valid=temporary_fd>=0;unsigned char chunk[8192];
        while(valid&&(n=avio_read(input,chunk,sizeof(chunk)))>0){
            bytes+=n;if(bytes>2*1024*1024){valid=0;break;}
            int at=0;while(at<n){ssize_t w=write(temporary_fd,chunk+at,(size_t)(n-at));if(w<0&&errno==EINTR)continue;if(w<=0){valid=0;break;}at+=(int)w;}
        }
        avio_closep(&input);
        if(!valid||(n<0&&n!=AVERROR_EOF))goto done;
        path=temporary;av_dict_set(&options,"protocol_whitelist","file",0);
    }else {struct stat st;if(stat(path,&st)||st.st_size>2*1024*1024)goto done;}
    if(avformat_open_input(&fmt,path,NULL,&options)<0)goto done;
    if(avformat_find_stream_info(fmt,NULL)<0)goto done;
    int track=av_find_best_stream(fmt,AVMEDIA_TYPE_SUBTITLE,-1,-1,NULL,0);if(track<0)goto done;
    const AVCodec *codec=avcodec_find_decoder(fmt->streams[track]->codecpar->codec_id);if(!codec)goto done;
    decoder=avcodec_alloc_context3(codec);if(!decoder)goto done;
    if(avcodec_parameters_to_context(decoder,fmt->streams[track]->codecpar)<0||avcodec_open2(decoder,codec,NULL)<0)goto done;
    external_cues=calloc(8192,sizeof(*external_cues));packet=av_packet_alloc();if(!external_cues||!packet)goto done;
    subtitle_loading=1;decoder->pkt_timebase=fmt->streams[track]->time_base;
    for(int n=0;n<16384&&external_count<8192&&av_gettime_relative()<deadline&&av_read_frame(fmt,packet)>=0;n++){
        if(packet->stream_index==track)subtitle_decode(decoder,fmt->streams[track],packet);
        av_packet_unref(packet);
    }
    for(int i=0;i<external_count;i++){external_cues[i].start+=offset;external_cues[i].end+=offset;}
    qsort(external_cues,(size_t)external_count,sizeof(*external_cues),subtitle_order);
 done:
    if(temporary_fd>=0){close(temporary_fd);unlink(temporary);}
    subtitle_loading=0;deadline=0;av_packet_free(&packet);avcodec_free_context(&decoder);avformat_close_input(&fmt);av_dict_free(&options);
    fprintf(stderr,"External subtitle cues: %d\n",external_count);
}
static void subtitle_draw(void){
    if(!view_context||origin==AV_NOPTS_VALUE||!clock_start)return;
    int64_t now=origin+av_gettime_relative()-clock_start-(int64_t)subtitle_delay*AV_TIME_BASE;
    int selected=-1;for(int i=0;i<64;i++)if(subtitle_cues[i].text[0]&&now>=subtitle_cues[i].start&&now<subtitle_cues[i].end)selected=i;
    int lo=0,hi=external_count;
    while(lo<hi){int mid=lo+(hi-lo)/2;if(external_cues[mid].start<=now)lo=mid+1;else hi=mid;}
    for(int i=lo-1;i>=0&&i>=lo-32;i--)if(now<external_cues[i].end){selected=64+i;break;}
    if(selected==subtitle_visible)return;
    subtitle_visible=selected;
    if(selected<0){if(subtitle_overlay)SLVideo_HideOverlay(subtitle_overlay);return;}
    if(!subtitle_overlay)subtitle_overlay=SLVideo_CreateOverlay(view_context,960,144);
    if(!subtitle_overlay)return;
    uint32_t *pixels=NULL;int pitch=0;SLVideo_HideOverlay(subtitle_overlay);SLVideo_GetOverlayPixels(subtitle_overlay,&pixels,&pitch);
    if(!pixels||pitch<960*4)return;
    for(int y=0;y<144;y++)for(int x=0;x<960;x++)((uint32_t*)((char*)pixels+y*pitch))[x]=0xa0000000;
    const char *input=selected>=64?external_cues[selected-64].text:subtitle_cues[selected].text;int cols=900/(6*subtitle_size);
    for(int row=0;row<3&&*input;row++){
        char line[80];int n=0;while(input[n]&&input[n]!='\n'&&n<cols)n++;
        if(n==cols&&input[n]&&input[n]!='\n'){int split=n;while(split>0&&input[split]!=' ')split--;if(split>0)n=split;}
        memcpy(line,input,(size_t)n);line[n]=0;input+=n;while(*input==' '||*input=='\n')input++;
        int left=(960-n*6*subtitle_size)/2;
        for(int j=0;j<n;j++){
            unsigned char ch=(unsigned char)toupper((unsigned char)line[j]);size_t g;
            for(g=0;g<sizeof(glyphs)/sizeof(glyphs[0]);g++)if(glyphs[g].c==ch)break;
            if(g==sizeof(glyphs)/sizeof(glyphs[0]))continue;
            for(int y=0;y<7;y++)for(int x=0;x<5;x++)if(glyphs[g].row[y]&(16>>x))
                for(int dy=0;dy<subtitle_size;dy++)for(int dx=0;dx<subtitle_size;dx++)
                    ((uint32_t*)((char*)pixels+(18+row*38+y*subtitle_size+dy)*pitch))[left+j*6*subtitle_size+x*subtitle_size+dx]=0xffffffff;
        }
    }
    SLVideo_SetOverlayDisplayArea(subtitle_overlay,0.08f,0.75f,0.84f,0.20f);SLVideo_ShowOverlay(subtitle_overlay);subtitle_rendered++;
}
