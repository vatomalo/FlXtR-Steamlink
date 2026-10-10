/* Small text-only subtitle renderer on SLVideo's overlay plane. */
typedef struct {int64_t start,end;char text[512];} SubtitleCue;
/* External text subtitle tracks may contain a complete episode of cues. */
#define SUBTITLE_MAX_CUES 4096
static SubtitleCue subtitle_cues[SUBTITLE_MAX_CUES];
static int subtitle_external;
static int64_t subtitle_seek_us;
static CSLVideoOverlay *subtitle_overlay;
static int subtitle_size=2,subtitle_delay,subtitle_visible=-1;
#include "subtitle_text.h"
static void subtitle_decode(AVCodecContext *decoder,AVStream *stream,AVPacket *packet){
    AVSubtitle sub={0};int got=0;
    if(avcodec_decode_subtitle2(decoder,&sub,&got,packet)<0||!got){avsubtitle_free(&sub);return;}
    int64_t start=packet->pts==AV_NOPTS_VALUE?0:av_rescale_q(packet->pts,stream->time_base,AV_TIME_BASE_Q);
    /* WebVTT and SRT demuxers provide the actual cue span in packet.duration.
     * Decoder display times can be rounded or shortened, cutting off lines
     * even when their starting timestamps are perfectly synchronized. */
    int64_t packet_duration=packet->duration>0?
        av_rescale_q(packet->duration,stream->time_base,AV_TIME_BASE_Q):0;
    int64_t decoder_duration=sub.end_display_time>sub.start_display_time?
        (int64_t)(sub.end_display_time-sub.start_display_time)*1000:0;
    int64_t duration=(stream->codecpar->codec_id==AV_CODEC_ID_WEBVTT||
                      stream->codecpar->codec_id==AV_CODEC_ID_SUBRIP)?
        (packet_duration>0?packet_duration:decoder_duration):
        (decoder_duration>0?decoder_duration:packet_duration);
    if(duration<=0)duration=5000000;
    start+=(int64_t)sub.start_display_time*1000;
    for(unsigned i=0;i<sub.num_rects;i++){
        const char *text=sub.rects[i]->text?sub.rects[i]->text:sub.rects[i]->ass;
        if(!text)continue;
        int slot=0;for(int j=1;j<SUBTITLE_MAX_CUES;j++)if(subtitle_cues[j].end<subtitle_cues[slot].end)slot=j;
        subtitle_cues[slot].start=start;subtitle_cues[slot].end=start+duration;
        subtitle_text(subtitle_cues[slot].text,sizeof(subtitle_cues[slot].text),text,!sub.rects[i]->text);
        if(subtitle_visible==slot)subtitle_visible=-2;
    }avsubtitle_free(&sub);
}
static void subtitle_draw(void){
    /* Keep the last visible cue while the decoder waits for more media. */
    if(subtitle_buffering)return;
    static Uint32 next_poll;
    if(!view_context||origin==AV_NOPTS_VALUE||!clock_start)return;
    Uint32 ticks=SDL_GetTicks();
    if((Sint32)(ticks-next_poll)<0)return;
    next_poll=ticks+40;
    int64_t now=(subtitle_external?subtitle_seek_us:origin)+av_gettime_relative()-clock_start-(int64_t)subtitle_delay*AV_TIME_BASE;
    int selected=-1;for(int i=0;i<SUBTITLE_MAX_CUES;i++)if(subtitle_cues[i].text[0]&&now>=subtitle_cues[i].start&&now<subtitle_cues[i].end)selected=i;
    if(selected==subtitle_visible)return;
    if(selected<0){subtitle_visible=-1;if(subtitle_overlay)SLVideo_HideOverlay(subtitle_overlay);return;}
    if(!subtitle_overlay)subtitle_overlay=SLVideo_CreateOverlay(view_context,960,144);
    if(!subtitle_overlay)return;
    uint32_t *pixels=NULL;int pitch=0;SLVideo_HideOverlay(subtitle_overlay);SLVideo_GetOverlayPixels(subtitle_overlay,&pixels,&pitch);
    if(!pixels||pitch<960*4)return;
    subtitle_visible=selected;
    for(int y=0;y<144;y++)for(int x=0;x<960;x++)((uint32_t*)((char*)pixels+y*pitch))[x]=0xa0000000;
    const char *input=subtitle_cues[selected].text;int cols=900/(6*subtitle_size);
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
    SLVideo_SetOverlayDisplayArea(subtitle_overlay,0.08f,0.75f,0.84f,0.20f);SLVideo_ShowOverlay(subtitle_overlay);
}
