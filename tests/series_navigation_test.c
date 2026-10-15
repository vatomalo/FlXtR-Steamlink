#define main catalog_main
#include "../src/catalog.c"
#undef main
#include <assert.h>
static void check(int season,int episode,int direction,int outseason,int outep){
 used=total=0;memset(entries,0,sizeof(entries));assert(!series_adjacent(125,season,episode,direction));
 assert(used==(outep?1:0));if(outep){assert(entries[0].season==outseason&&entries[0].episode==outep);assert(!strcmp(entries[0].kind,"episode"));}
}
int main(void){
 char dir[]="/tmp/flxtr-series-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
 response_cache_store(API "/tv/125","{\"seasons\":[{\"season_number\":0},{\"season_number\":1},{\"season_number\":3}]}");
 response_cache_store(API "/tv/125/season/1","{\"episodes\":[{\"episode_number\":2,\"name\":\"Second\",\"air_date\":\"2001-01-01\"},{\"episode_number\":1,\"name\":\"First\",\"air_date\":\"2001-01-01\"}]}");
 response_cache_store(API "/tv/125/season/3","{\"episodes\":[{\"episode_number\":1,\"name\":\"New season\",\"air_date\":\"2001-01-01\"},{\"episode_number\":2,\"air_date\":\"2999-01-01\"}]}");
 check(1,1,1,1,2);check(1,2,1,3,1);check(3,1,-1,1,2);check(3,1,1,0,0);check(1,1,-1,0,0);
 DIR *d=opendir("catalog-cache");struct dirent *e;while((e=readdir(d)))if(e->d_name[0]!='.'){char f[512];snprintf(f,sizeof(f),"catalog-cache/%s",e->d_name);unlink(f);}closedir(d);rmdir("catalog-cache");chdir("/tmp");rmdir(dir);
 puts("Series navigation: ordering, season gaps, previous, future episodes and end passed");return 0;
}
