#include <time.h>
#define TV_BLOCKS 7
#define TV_GENRES 8
typedef struct {int hour,genre;} TvBlock;
static const TvBlock tv_defaults[TV_BLOCKS]={{0,3},{6,4},{10,1},{15,2},{17,5},{19,7},{22,6}};
static const char *const tv_genres[TV_GENRES]={"MIXED SHOWS","COMEDY","CRIME / DRAMA","SCI-FI","ANIMATION","ACTION / ADVENTURE","MYSTERY","SCI-FI / ADULT ANIMATION"};
static inline int tv_valid(const TvBlock *blocks){
    if(blocks[0].hour)return 0;
    for(int i=0;i<TV_BLOCKS;i++)if(blocks[i].hour<0||blocks[i].hour>23||blocks[i].genre<0||blocks[i].genre>=TV_GENRES||(i&&blocks[i].hour<=blocks[i-1].hour))return 0;
    return 1;
}
static inline int tv_block_at(const TvBlock *blocks,int hour){
    int n=0;for(int i=1;i<TV_BLOCKS;i++)if(hour>=blocks[i].hour)n=i;return n;
}
static inline int tv_hour(void){
    /* Explicit Oslo DST rule; the device need not ship the zoneinfo database. */
    setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1);tzset();
    time_t now=time(NULL);struct tm local;localtime_r(&now,&local);return local.tm_hour;
}
