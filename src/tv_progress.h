/* Bounded per-show episode progress. Only successful completion advances it. */
typedef struct {int id,season,episode;} TvProgress;
static inline int tv_progress_read(TvProgress *rows){
    FILE *f=fopen("tv-progress.tsv","r");if(!f)return 0;
    int n=0;char line[96];while(n<256&&fgets(line,sizeof(line),f)){
        TvProgress p;if(sscanf(line,"%d %d %d",&p.id,&p.season,&p.episode)==3&&p.id>0&&p.season>0&&p.season<1000&&p.episode>0&&p.episode<100000)rows[n++]=p;
    }fclose(f);return n;
}
static inline void tv_progress_save(int id,int season,int episode){
    TvProgress rows[256];int n=tv_progress_read(rows),at=-1;
    for(int i=0;i<n;i++)if(rows[i].id==id)at=i;
    if(at<0){if(n==256){memmove(rows,rows+1,255*sizeof(*rows));n=255;}at=n++;}
    rows[at]=(TvProgress){id,season,episode};FILE *f=fopen("tv-progress.tsv.next","w");if(!f)return;
    for(int i=0;i<n;i++)fprintf(f,"%d\t%d\t%d\n",rows[i].id,rows[i].season,rows[i].episode);
    if(!fclose(f))rename("tv-progress.tsv.next","tv-progress.tsv");
}
