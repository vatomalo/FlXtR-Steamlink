#ifndef FLXTR_TV_NETWORK_H
#define FLXTR_TV_NETWORK_H
/* Persisted broadcast timeline. This module deliberately never starts a stream.
 * Player/selector integration must remain separate from the channel clock. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#define TV_NETWORK_CHANNELS 16
#define TV_NETWORK_MAX_AIRINGS 192
#define TV_NETWORK_FORMAT 1
typedef struct {
    long long start;
    int duration,kind,id,season,episode;
    char title[80];
} TvNetworkAiring;
typedef struct {
    int channel,count;
    long long day_start,day_end;
    TvNetworkAiring slots[TV_NETWORK_MAX_AIRINGS];
} TvNetworkDay;
static inline int tv_network_valid(const TvNetworkDay *day){
    if(!day||day->channel<0||day->channel>=TV_NETWORK_CHANNELS||
       day->count<1||day->count>TV_NETWORK_MAX_AIRINGS||
       day->day_start<1||day->day_end<=day->day_start)return 0;
    long long end=day->day_start;
    for(int i=0;i<day->count;i++){
        const TvNetworkAiring *a=&day->slots[i];
        if(a->start!=end||a->duration<1||a->duration>14400||
           a->id<1||a->season<0||a->episode<0||!a->title[0]||
           strchr(a->title,'\t')||strchr(a->title,'\n'))return 0;
        end+=a->duration;
        if(end>day->day_end)return 0;
    }
    return end==day->day_end;
}
/* -1 means outside the scheduled timeline; callers must never regenerate
 * the current day just because the current slot has no playable source. */
static inline int tv_network_at(const TvNetworkDay *day,long long now,int *offset){
    if(!tv_network_valid(day)||now<day->day_start||now>=day->day_end)return -1;
    int lo=0,hi=day->count;
    while(lo<hi){
        int mid=lo+(hi-lo)/2;
        if(day->slots[mid].start<=now)lo=mid+1;else hi=mid;
    }
    int index=lo-1;
    if(index<0||now>=day->slots[index].start+day->slots[index].duration)return -1;
    if(offset)*offset=(int)(now-day->slots[index].start);
    return index;
}
/* The entire guide is written to a temporary file before atomic rename.
 * The naming is per channel and per local broadcast date, never per viewer. */
static inline int tv_network_path(char *out,size_t cap,const char *directory,
                                  int channel,const char *day_key){
    if(!directory||!day_key||strlen(day_key)!=8||strspn(day_key,"0123456789")!=8||
       channel<0||channel>=TV_NETWORK_CHANNELS)return 0;
    int n=snprintf(out,cap,"%s/tv-channel-%02d-%s.tsv",directory,channel+1,day_key);
    return n>0&&(size_t)n<cap;
}
static inline int tv_network_save(const TvNetworkDay *day,const char *path){
    if(!tv_network_valid(day)||!path||strlen(path)>PATH_MAX-12)return 0;
    char next[PATH_MAX];snprintf(next,sizeof(next),"%s.next",path);
    FILE *f=fopen(next,"w");if(!f)return 0;
    int ok=fprintf(f,"FLXTR-TV\t%d\t%d\t%lld\t%lld\t%d\n",TV_NETWORK_FORMAT,
                   day->channel,day->day_start,day->day_end,day->count)>0;
    for(int i=0;ok&&i<day->count;i++){
        const TvNetworkAiring *a=&day->slots[i];
        ok=fprintf(f,"%lld\t%d\t%d\t%d\t%d\t%d\t%s\n",
                   a->start,a->duration,a->kind,a->id,a->season,a->episode,a->title)>0;
    }
    if(fflush(f))ok=0;
    if(fclose(f))ok=0;
    if(ok){if(rename(next,path))ok=0;}
    if(!ok)remove(next);
    return ok;
}
static inline int tv_network_load(TvNetworkDay *day,const char *path){
    if(!day||!path)return 0;
    FILE *f=fopen(path,"r");if(!f)return 0;
    TvNetworkDay *tmp=(TvNetworkDay*)calloc(1,sizeof(*tmp));
    if(!tmp){fclose(f);return 0;}
    char line[256];int version=0,ok=0;
    if(fgets(line,sizeof(line),f)&&
       sscanf(line,"FLXTR-TV\t%d\t%d\t%lld\t%lld\t%d",
              &version,&tmp->channel,&tmp->day_start,&tmp->day_end,&tmp->count)==5&&
       version==TV_NETWORK_FORMAT&&tmp->count>0&&tmp->count<=TV_NETWORK_MAX_AIRINGS){
        ok=1;
        for(int i=0;i<tmp->count;i++){
            TvNetworkAiring *a=&tmp->slots[i];int consumed=0;
            if(!fgets(line,sizeof(line),f)||
               sscanf(line,"%lld\t%d\t%d\t%d\t%d\t%d\t%79[^\n]%n",
                      &a->start,&a->duration,&a->kind,&a->id,&a->season,
                      &a->episode,a->title,&consumed)!=7||
               !consumed||line[consumed]!='\n'){ok=0;break;}
        }
        if(ok&&fgetc(f)!=EOF)ok=0;
    }
    fclose(f);
    if(ok&&tv_network_valid(tmp))*day=*tmp;else ok=0;
    free(tmp);return ok;
}
#endif
