#define FLXTR_DESKTOP
#define main source_main
#include "../src/shell.c"
#undef main
#include "../src/playback_started.h"
#include <assert.h>
static int rows(const char *event){
    FILE *f=fopen("tv-play-history.tsv","r");if(!f)return 0;
    char line[512];TvPlayRecord row;int n=0;
    while(fgets(line,sizeof(line),f))if(tv_play_parse(line,&row)&&!strcmp(row.event,event))n++;
    fclose(f);return n;
}
int main(void){
    char dir[]="/tmp/flxtr-history-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    Title t={0};strcpy(t.kind,"episode");strcpy(t.title,"Fixture");t.id=42;t.season=t.episode=1;
    tv_history_prepare(&t);tv_history_finish("failed",0);
    assert(!rows("started")&&!rows("failed")); /* A failed lookup/preloader is not played. */
    tv_history_prepare(&t);
    setenv("FLXTR_BOOKMARK_KEY","00000000",1);playback_mark_started(5);tv_history_poll();assert(!rows("started"));
    char token[16];bookmark_path(&t,token,sizeof(token));setenv("FLXTR_BOOKMARK_KEY",token,1);
    playback_mark_started(10);tv_history_poll();assert(rows("started")==1);
    tv_history_prepare(&t);playback_mark_started(40);tv_history_poll(); /* quality/seek restart */
    assert(rows("started")==1);tv_history_finish("stopped",45);
    assert(rows("stopped")==1&&tv_play_closed(42,1,1));
    tv_history_prepare(&t);playback_mark_started(50);tv_history_finish("completed",60);
    assert(rows("started")==2&&rows("completed")==1);
    t.episode=2;tv_history_prepare(&t);bookmark_path(&t,token,sizeof(token));
    setenv("FLXTR_BOOKMARK_KEY",token,1);playback_mark_started(0);tv_history_poll();
    tv_history_finish("interrupted",30);assert(rows("interrupted")==1);
    strcpy(t.kind,"youtube");strcpy(t.meta,"https://www.youtube.com/watch?v=abcdefghijk");
    tv_history_prepare(&t);assert(!strcmp(tv_history_key,"youtube:abcdefghijk"));
    bookmark_path(&t,token,sizeof(token));setenv("FLXTR_BOOKMARK_KEY",token,1);
    playback_mark_started(0);tv_history_finish("skipped",20);assert(rows("skipped")==1);
    unlink("tv-play-history.tsv");unlink("playback-started");chdir("/tmp");rmdir(dir);
    puts("PASS: confirmed playback only, stale marker rejection, seek deduplication, outcomes and MTV identity");
}
