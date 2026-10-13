/* One persisted channel position, not a personal playback bookmark. */
typedef struct {long long block,start;int id,season,episode,seconds;} TvAiring;
static inline int tv_airing_read(TvAiring *a){
    FILE *f=fopen("tv-airing.tsv","r");if(!f)return 0;
    int n=fscanf(f,"%lld %lld %d %d %d %d",&a->block,&a->start,&a->id,&a->season,&a->episode,&a->seconds);fclose(f);
    return n==6&&a->block>0&&a->start>=a->block&&a->id>0&&a->season>0&&a->season<1000&&a->episode>0&&a->episode<100000&&a->seconds>=60&&a->seconds<=14400;
}
static inline void tv_airing_save(const TvAiring *a){
    FILE *f=fopen("tv-airing.tsv.next","w");if(!f)return;
    fprintf(f,"%lld\t%lld\t%d\t%d\t%d\t%d\n",a->block,a->start,a->id,a->season,a->episode,a->seconds);
    if(!fclose(f))rename("tv-airing.tsv.next","tv-airing.tsv");
}
static inline int tv_airing_current(const TvAiring *a,long long now){
    return now>=a->start&&now<a->start+a->seconds;
}
