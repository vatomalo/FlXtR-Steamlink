/* Permanent TV playback ledger. Recent-selection cooldown and completion
 * cursors remain useful, but neither is a substitute for this append-only log. */
#ifndef FLXTR_TV_PLAY_HISTORY_H
#define FLXTR_TV_PLAY_HISTORY_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#ifdef _WIN32
#include <io.h>
#endif
typedef struct {
    long long at;char event[16],kind[16];int id,season,episode;
    double position;char key[96],title[80];
} TvPlayRecord;
static inline int tv_play_valid_event(const char *event){
    return !strcmp(event,"started")||!strcmp(event,"completed")||!strcmp(event,"stopped")||
        !strcmp(event,"skipped")||!strcmp(event,"failed")||!strcmp(event,"interrupted");
}
static inline int tv_play_parse(const char *line,TvPlayRecord *r){
    char copy[512];size_t n=strlen(line);
    if(n>=sizeof(copy)||!n||line[n-1]!='\n')return 0;
    memcpy(copy,line,n+1);copy[n-1]=0;char *parts[9],*p=copy;
    for(int i=0;i<9;i++){
        parts[i]=p;if(i==8)break;
        char *tab=strchr(p,'\t');if(!tab)return 0;*tab=0;p=tab+1;
    }
    char *end;memset(r,0,sizeof(*r));
    errno=0;r->at=strtoll(parts[0],&end,10);if(errno||end==parts[0]||*end||r->at<0)return 0;
    if(!tv_play_valid_event(parts[1])||strlen(parts[2])<1||strlen(parts[2])>=sizeof(r->kind))return 0;
    snprintf(r->event,sizeof(r->event),"%s",parts[1]);snprintf(r->kind,sizeof(r->kind),"%s",parts[2]);
    int *numbers[]={&r->id,&r->season,&r->episode};
    for(int i=0;i<3;i++){errno=0;long v=strtol(parts[i+3],&end,10);if(errno||end==parts[i+3]||*end||v<0||v>2147483647)return 0;*numbers[i]=(int)v;}
    r->position=strtod(parts[6],&end);if(end==parts[6]||*end||!(r->position>=0&&r->position<=86400))return 0;
    if(!parts[7][0]||strlen(parts[7])>=sizeof(r->key)||strlen(parts[8])>=sizeof(r->title))return 0;
    snprintf(r->key,sizeof(r->key),"%s",parts[7]);snprintf(r->title,sizeof(r->title),"%s",parts[8]);
    if(!strcmp(r->kind,"episode")&&(r->id<1||r->season<1||r->season>=1000||r->episode<1||r->episode>=100000))return 0;
    return 1;
}
static inline int tv_play_append(const char *event,const char *kind,int id,int season,int episode,
                                 double position,const char *key,const char *title){
    if(!tv_play_valid_event(event)||!kind||!*kind||strlen(kind)>=16||!key||!*key||strlen(key)>=96||
       strcspn(kind,"\t\r\n")!=strlen(kind)||strcspn(key,"\t\r\n")!=strlen(key)||
       id<0||season<0||episode<0||!(position>=0&&position<=86400))return 0;
    if(!strcmp(kind,"episode")&&(id<1||season<1||season>=1000||episode<1||episode>=100000))return 0;
    char label[80];snprintf(label,sizeof(label),"%.79s",title?title:"");
    for(char *p=label;*p;p++)if((unsigned char)*p<32||*p==127)*p=' ';
    FILE *f=fopen("tv-play-history.tsv","a+");if(!f)return 0;
    /* Preserve a torn final row, but keep the next valid record separate. */
    if(!fseek(f,0,SEEK_END)&&ftell(f)>0){
        if(!fseek(f,-1,SEEK_END)){int last=fgetc(f);if(last!='\n'){fseek(f,0,SEEK_END);fputc('\n',f);}}
    }
    fseek(f,0,SEEK_END);
    int good=fprintf(f,"%lld\t%s\t%s\t%d\t%d\t%d\t%.3f\t%s\t%s\n",
        (long long)time(NULL),event,kind,id,season,episode,position,key,label)>0;
    if(fflush(f))good=0;
#ifdef _WIN32
    if(_commit(_fileno(f)))good=0;
#else
    if(fsync(fileno(f)))good=0;
#endif
    if(fclose(f))good=0;
    return good;
}
/* One streaming scan, constant memory, no 32/256-show eviction. The next fresh
 * episode follows the furthest episode that actually reached playback. */
static inline int tv_play_cursor(int id,int *season,int *episode){
    *season=*episode=0;FILE *f=fopen("tv-play-history.tsv","r");if(!f)return 0;
    char line[512];TvPlayRecord row;
    while(fgets(line,sizeof(line),f)){
        if(!strchr(line,'\n')){int c;while((c=fgetc(f))!=EOF&&c!='\n'){}continue;}
        if(!tv_play_parse(line,&row)||strcmp(row.event,"started")||strcmp(row.kind,"episode")||row.id!=id)continue;
        if(row.season>*season||(row.season==*season&&row.episode>*episode)){*season=row.season;*episode=row.episode;}
    }fclose(f);return *season>0;
}
static inline int tv_play_closed(int id,int season,int episode){
    FILE *f=fopen("tv-play-history.tsv","r");if(!f)return 0;
    char line[512];TvPlayRecord row;int closed=0;
    while(fgets(line,sizeof(line),f)){
        if(!strchr(line,'\n')){int c;while((c=fgetc(f))!=EOF&&c!='\n'){}continue;}
        if(tv_play_parse(line,&row)&&!strcmp(row.kind,"episode")&&row.id==id&&row.season==season&&row.episode==episode)
            closed=strcmp(row.event,"started")!=0;
    }fclose(f);return closed;
}
#endif
