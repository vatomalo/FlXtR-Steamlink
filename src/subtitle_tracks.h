/* The player's subtitle menu: OFF, AUTO, then every text track in the video
 * and every track the resolver found, in any language. A new choice is
 * applied when the menu closes, with the video running: an embedded track is
 * picked up from the packets already read, a downloaded one is fetched by a
 * thread and swapped in when it is complete. */
#include "subtitle_languages.h"
#define SUBTITLE_TRACKS 96
enum{SUBTITLE_CHOICE_OFF,SUBTITLE_CHOICE_AUTO,SUBTITLE_CHOICE_FIRST};
typedef struct {int language,stream;char name[40],url[1024];} SubtitleTrack;   /* stream < 0: url */
static SubtitleTrack subtitle_tracks[SUBTITLE_TRACKS];
static int subtitle_track_count;
static int subtitle_choice=SUBTITLE_CHOICE_OFF;       /* what is showing */
static int subtitle_wanted=SUBTITLE_CHOICE_OFF;       /* asked for (menu closed) */
static int subtitle_stream=-1;                        /* the embedded track decoded now */
static AVFormatContext *subtitle_format;
static AVCodecContext *subtitle_decoder;
static int subtitle_text_codec(enum AVCodecID id){
    return id==AV_CODEC_ID_SUBRIP||id==AV_CODEC_ID_WEBVTT||id==AV_CODEC_ID_MOV_TEXT||id==AV_CODEC_ID_ASS||id==AV_CODEC_ID_SSA;
}
static void subtitle_track_add(int language,const char *name,int stream,const char *url){
    if(subtitle_track_count>=SUBTITLE_TRACKS||(stream<0&&(!url||!*url||strlen(url)>=sizeof(subtitle_tracks[0].url))))return;
    int same=0;
    for(int i=0;i<subtitle_track_count;i++){
        if(stream<0&&!strcmp(subtitle_tracks[i].url,url))return;
        same+=!strncmp(subtitle_tracks[i].name,name,strlen(name));
    }
    SubtitleTrack *t=&subtitle_tracks[subtitle_track_count++];
    memset(t,0,sizeof(*t));t->language=language;t->stream=stream;
    if(url)snprintf(t->url,sizeof(t->url),"%s",url);
    if(same)snprintf(t->name,sizeof(t->name),"%.26s %d",name,same+1);else snprintf(t->name,sizeof(t->name),"%.39s",name);
}
static void subtitle_tracks_find(AVFormatContext *fmt){
    subtitle_track_count=0;subtitle_format=fmt;
    if(fmt)for(unsigned i=0;i<fmt->nb_streams;i++){
        if(fmt->streams[i]->codecpar->codec_type!=AVMEDIA_TYPE_SUBTITLE||!subtitle_text_codec(fmt->streams[i]->codecpar->codec_id))continue;
        AVDictionaryEntry *language=av_dict_get(fmt->streams[i]->metadata,"language",NULL,0);
        AVDictionaryEntry *title=av_dict_get(fmt->streams[i]->metadata,"title",NULL,0);
        int index=language?subtitle_language_index(language->value):-1;
        if(index<0&&title)index=subtitle_language_index(title->value);
        subtitle_track_add(index,index>=0?subtitle_language_table[index].name:"UNLABELLED",(int)i,NULL);
    }
    const char *path=getenv("FLXTR_SUBTITLE_TRACKS");
    FILE *f=path&&*path?fopen(path,"r"):NULL;
    if(f){
        char line[1200];
        while(fgets(line,sizeof(line),f)){
            line[strcspn(line,"\r\n")]=0;
            char *name=strchr(line,'\t');if(!name)continue;
            *name++=0;
            char *url=strchr(name,'\t');if(!url)continue;
            *url++=0;
            if(strncmp(url,"https://",8)||!*name)continue;
            subtitle_track_add(subtitle_language_index(line),name,-1,url);
        }
        fclose(f);
    }
    const struct {const char *key;int index;} known[]={{"FLXTR_SUBTITLE_ENGLISH",2},{"FLXTR_SUBTITLE_NORWEGIAN",3},{"FLXTR_SUBTITLE_SPANISH",4}};
    for(int k=0;k<3;k++){
        const char *url=getenv(known[k].key);
        if(url&&*url)subtitle_track_add(known[k].index,subtitle_language_table[known[k].index].name,-1,url);
    }
}
/* The track a setting (table position) means for this video, or -1: in the
 * video first, its unlabelled track next, then a download. AUTO: any track
 * in the video, else English, Norwegian, Spanish, else the first. */
static int subtitle_track_for(int language){
    if(language==0)return -1;
    if(language==1){
        for(int i=0;i<subtitle_track_count;i++)if(subtitle_tracks[i].stream>=0)return i;
        for(int want=2;want<=4;want++)for(int i=0;i<subtitle_track_count;i++)if(subtitle_tracks[i].language==want)return i;
        return subtitle_track_count?0:-1;
    }
    for(int pass=0;pass<3;pass++)for(int i=0;i<subtitle_track_count;i++){
        const SubtitleTrack *t=&subtitle_tracks[i];
        if(pass==0&&t->stream>=0&&t->language==language)return i;
        if(pass==1&&t->stream<0&&t->language==language)return i;
        if(pass==2&&t->stream>=0&&t->language<0)return i;
    }
    return -1;
}
static int subtitle_choice_count(void){return SUBTITLE_CHOICE_FIRST+subtitle_track_count;}
static void subtitle_choice_name(int choice,char *out,size_t size){
    if(choice<SUBTITLE_CHOICE_FIRST||choice>=subtitle_choice_count()){
        snprintf(out,size,"%s",choice==SUBTITLE_CHOICE_OFF?"OFF":"AUTO");return;
    }
    const SubtitleTrack *t=&subtitle_tracks[choice-SUBTITLE_CHOICE_FIRST];
    int readable=t->language<0||subtitle_language_table[t->language].latin;
    snprintf(out,size,"%s%s  %d/%d",t->name,readable?"":" - LETTERS NOT SUPPORTED",choice-SUBTITLE_CHOICE_FIRST+1,subtitle_track_count);
}
/* The setting to remember for a choice. */
static int subtitle_choice_language(int choice){
    if(choice==SUBTITLE_CHOICE_OFF)return 0;
    if(choice<SUBTITLE_CHOICE_FIRST||choice>=subtitle_choice_count())return 1;
    const int language=subtitle_tracks[choice-SUBTITLE_CHOICE_FIRST].language;
    return language>=2?language:1;
}
static int subtitle_choice_track(int choice){
    if(choice==SUBTITLE_CHOICE_OFF)return -1;
    return choice==SUBTITLE_CHOICE_AUTO?subtitle_track_for(1):choice-SUBTITLE_CHOICE_FIRST;
}
/* HLS opens a rendition when it is wanted; in a file the tracks are there
 * anyway and tiny, so all text tracks stay readable for a quick switch. */
static void subtitle_discard(void){
    if(!subtitle_format)return;
    const char *name=subtitle_format->iformat?subtitle_format->iformat->name:"";
    const int hls=!strncmp(name,"hls",3);
    for(unsigned i=0;i<subtitle_format->nb_streams;i++){
        AVStream *s=subtitle_format->streams[i];
        if(s->codecpar->codec_type!=AVMEDIA_TYPE_SUBTITLE)continue;
        s->discard=(int)i==subtitle_stream||(!hls&&subtitle_text_codec(s->codecpar->codec_id))?AVDISCARD_DEFAULT:AVDISCARD_ALL;
    }
}
static int subtitle_open_stream(int stream){
    AVCodecParameters *p=subtitle_format->streams[stream]->codecpar;
    const AVCodec *codec=avcodec_find_decoder(p->codec_id);
    AVCodecContext *next=codec?avcodec_alloc_context3(codec):NULL;
    if(!next||avcodec_parameters_to_context(next,p)<0||avcodec_open2(next,codec,NULL)<0){avcodec_free_context(&next);return -1;}
    next->pkt_timebase=subtitle_format->streams[stream]->time_base;
    avcodec_free_context(&subtitle_decoder);subtitle_decoder=next;subtitle_stream=stream;subtitle_external=0;
    subtitle_discard();
    return 0;
}
static void subtitle_close_stream(void){avcodec_free_context(&subtitle_decoder);subtitle_stream=-1;subtitle_discard();}
static int subtitle_fetch(const char *url,SubtitleCue *cues);
/* One download at a time; the video never waits for it. */
static struct {pthread_t thread;pthread_mutex_t mutex;int running,done,result,choice;char url[1024];SubtitleCue *cues;} subtitle_job={.mutex=PTHREAD_MUTEX_INITIALIZER};
static void *subtitle_job_run(void *opaque){
    (void)opaque;
    const int result=subtitle_fetch(subtitle_job.url,subtitle_job.cues);
    pthread_mutex_lock(&subtitle_job.mutex);subtitle_job.result=result;subtitle_job.done=1;pthread_mutex_unlock(&subtitle_job.mutex);
    return NULL;
}
static void subtitle_reset_cues(SubtitleCue *next){
    if(subtitle_cues!=subtitle_first_cues)free(subtitle_cues);
    subtitle_cues=next?next:subtitle_first_cues;
    if(!next)memset(subtitle_first_cues,0,sizeof(subtitle_first_cues));
    subtitle_visible=-2;
}
static void subtitle_choose(int choice){
    if(choice>=0&&choice<subtitle_choice_count())subtitle_wanted=choice;
    /* The shell saves it when playback ends, however it ends. */
    FILE *f=fopen("subtitle-choice.next","w");
    if(f){
        fprintf(f,"%d\n",subtitle_choice_language(subtitle_wanted));
        if(fclose(f)||rename("subtitle-choice.next","subtitle-choice"))unlink("subtitle-choice.next");
    }
}
static void subtitle_switch_poll(void){
    if(subtitle_job.running){
        pthread_mutex_lock(&subtitle_job.mutex);const int done=subtitle_job.done;pthread_mutex_unlock(&subtitle_job.mutex);
        if(!done)return;
        pthread_join(subtitle_job.thread,NULL);subtitle_job.running=0;
        if(subtitle_job.choice!=subtitle_wanted){free(subtitle_job.cues);}       /* chosen away meanwhile */
        else if(subtitle_job.result<0){
            free(subtitle_job.cues);subtitle_wanted=subtitle_choice;
            show_view("SUBTITLES NOT AVAILABLE");
        }else{
            subtitle_close_stream();subtitle_reset_cues(subtitle_job.cues);
            subtitle_external=1;subtitle_choice=subtitle_wanted;
            show_view("SUBTITLES ON");
        }
        subtitle_job.cues=NULL;
    }
    if(subtitle_wanted==subtitle_choice)return;
    const int track=subtitle_choice_track(subtitle_wanted);
    if(track<0){
        subtitle_close_stream();subtitle_reset_cues(NULL);subtitle_external=0;
        if(subtitle_overlay)SLVideo_HideOverlay(subtitle_overlay);
        subtitle_choice=subtitle_wanted;
        show_view(subtitle_wanted==SUBTITLE_CHOICE_OFF?"SUBTITLES OFF":"NO SUBTITLES FOUND");
        return;
    }
    if(subtitle_tracks[track].stream>=0){
        if(subtitle_open_stream(subtitle_tracks[track].stream)<0){subtitle_wanted=subtitle_choice;show_view("SUBTITLES NOT AVAILABLE");return;}
        subtitle_reset_cues(NULL);subtitle_choice=subtitle_wanted;
        show_view("SUBTITLES ON");
        return;
    }
    subtitle_job.cues=calloc(SUBTITLE_MAX_CUES,sizeof(SubtitleCue));
    if(!subtitle_job.cues){subtitle_wanted=subtitle_choice;return;}
    snprintf(subtitle_job.url,sizeof(subtitle_job.url),"%s",subtitle_tracks[track].url);
    subtitle_job.choice=subtitle_wanted;subtitle_job.done=0;subtitle_job.result=-1;
    if(pthread_create(&subtitle_job.thread,NULL,subtitle_job_run,NULL)){
        free(subtitle_job.cues);subtitle_job.cues=NULL;subtitle_wanted=subtitle_choice;return;
    }
    subtitle_job.running=1;
    show_view("LOADING SUBTITLES...");
}
static void subtitle_switch_close(void){
    if(subtitle_job.running){pthread_join(subtitle_job.thread,NULL);subtitle_job.running=0;free(subtitle_job.cues);subtitle_job.cues=NULL;}
    avcodec_free_context(&subtitle_decoder);
    if(subtitle_cues!=subtitle_first_cues){free(subtitle_cues);subtitle_cues=subtitle_first_cues;}
}
