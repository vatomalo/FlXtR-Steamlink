#include <time.h>
#define TV_BLOCKS 10
#define TV_GENRES 17
#define TV_MUSIC_VIDEOS 16
typedef struct {int hour,genre,marathon;} TvBlock;
static const TvBlock tv_defaults[TV_BLOCKS]={{0,6,1},{6,11,0},{9,1,0},{11,9,0},{13,8,0},{15,2,1},{17,5,1},{19,3,1},{21,7,0},{23,4,1}};
static const char *const tv_genres[TV_GENRES]={"MIXED SHOWS","COMEDY","CRIME / DRAMA","SCI-FI","ANIMATION","ACTION / ADVENTURE","MYSTERY","SCI-FI / ADULT ANIMATION","DOCUMENTARY","FAMILY","REALITY","KIDS","SOAP","WAR / POLITICS","WESTERN","TALK SHOW","MTV / MUSIC VIDEOS"};
static inline int tv_valid(const TvBlock *blocks){
    if(blocks[0].hour)return 0;
    for(int i=0;i<TV_BLOCKS;i++)if(blocks[i].hour<0||blocks[i].hour>23||blocks[i].genre<0||blocks[i].genre>=TV_GENRES||blocks[i].marathon<0||blocks[i].marathon>1||(i&&blocks[i].hour<=blocks[i-1].hour))return 0;
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
static inline time_t tv_block_start(const TvBlock *blocks,time_t now){
    (void)tv_hour();struct tm local;localtime_r(&now,&local);
    local.tm_hour=blocks[tv_block_at(blocks,local.tm_hour)].hour;
    local.tm_min=local.tm_sec=0;local.tm_isdst=-1;return mktime(&local);
}
