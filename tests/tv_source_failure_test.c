#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>

int main(void){
    char temp[]="/tmp/flxtr-tv-source-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    auto_title=(Title){.title="Unavailable",.kind="episode",.id=215803,.season=1,.episode=1};
    tv_active=tv_joining=auto_active=1;pending.mode=5;
    auto_result(0);
    int season=0,episode=0;
    assert(!auto_active&&!tv_joining&&tv_active&&tv_play_closed(215803,1,1));
    assert(!tv_play_cursor(215803,&season,&episode));
    /* Exhausting all servers also abandons the failed live join. */
    auto_title.episode=2;tv_joining=auto_active=1;auto_count=auto_index=2;
    auto_next();
    assert(!auto_active&&!tv_joining&&tv_active&&tv_play_closed(215803,1,2));
    assert(!tv_play_cursor(215803,&season,&episode));
    /* Normal library lookups never create a TV failure record. */
    tv_active=0;auto_title.episode=3;auto_active=1;pending.mode=5;
    auto_result(0);
    assert(!auto_active&&!tv_play_closed(215803,1,3));
    unlink("tv-play-history.tsv");assert(!chdir("/tmp"));assert(!rmdir(temp));
    puts("PASS: failed TV discovery and exhausted servers abandon rejoin without marking playback or affecting library lookups");
    return 0;
}
