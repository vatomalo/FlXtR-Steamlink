#ifndef FLXTR_TV_HISTORY_H
#define FLXTR_TV_HISTORY_H
typedef struct {int id,season,episode;} TvRecent;
static inline int tv_recent_read(TvRecent *rows){
    FILE *f=fopen("tv-recent.tsv","r");if(!f)return 0;
    int n=0;char line[96];while(n<32&&fgets(line,sizeof(line),f)){
        TvRecent r;if(sscanf(line,"%d %d %d",&r.id,&r.season,&r.episode)==3&&r.id>0&&r.season>0&&r.episode>0)rows[n++]=r;
    }fclose(f);return n;
}
static inline int tv_recent_has(int id,int season,int episode,int cooldown){
    TvRecent rows[32];int n=tv_recent_read(rows);
    for(int i=0;i<n;i++)if(rows[i].id==id&&((rows[i].season==season&&rows[i].episode==episode)||(cooldown&&i>=n-6)))return 1;
    return 0;
}
static inline void tv_recent_add(int id,int season,int episode){
    TvRecent rows[32];int n=tv_recent_read(rows);if(n==32){memmove(rows,rows+1,31*sizeof(*rows));n=31;}
    rows[n++]=(TvRecent){id,season,episode};FILE *f=fopen("tv-recent.tsv.next","w");if(!f)return;
    for(int i=0;i<n;i++)fprintf(f,"%d\t%d\t%d\n",rows[i].id,rows[i].season,rows[i].episode);
    if(!fclose(f))rename("tv-recent.tsv.next","tv-recent.tsv");
}
static inline int tv_sequence(const char *path){
    unsigned value=0;FILE *f=fopen(path,"r");if(f){if(fscanf(f,"%u",&value)!=1)value=0;fclose(f);}
    value%=10000000;char tmp[96];snprintf(tmp,sizeof(tmp),"%s.next",path);f=fopen(tmp,"w");
    if(f){fprintf(f,"%u\n",value+1);if(!fclose(f))rename(tmp,path);}
    return (int)value;
}
#endif
