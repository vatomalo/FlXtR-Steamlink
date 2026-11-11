#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    char path[]="/tmp/flxtr-home-XXXXXX";assert(mkdtemp(path));assert(!chdir(path));
    assert(!SDL_Init(SDL_INIT_TIMER));assert(!mkdir("music-cache",0700));assert(!mkdir("scripts",0700));
    FILE *f=fopen("music-cache/recents.tsv","w");assert(f);
    fputs("video\tabcdefghijk\tRecent song\tArtist\ninvalid\tx\tNo\tNo\nlocal\t/music/a.mp3\tOffline song\tOn device\n",f);fclose(f);
    music_open();assert(music_on&&music_tab==MUSIC_RECENTS&&music_count==2&&!music_lookup_pid);
    assert(!strcmp(music_rows[0].title,"Recent song"));
    music_action(SDLK_F7);MusicRow saved[100];assert(music_load_rows("music-cache/saved.tsv",saved)==1);
    assert(!strcmp(saved[0].key,"abcdefghijk"));
    music_action(SDLK_f);assert(music_load_rows("music-cache/saved.tsv",saved)==0);
    music_action(SDLK_f);assert(music_load_rows("music-cache/saved.tsv",saved)==1);
    music_queue_count=1;strcpy(music_queue[0].key,"original");
    music_pid=fork();assert(music_pid>=0);if(!music_pid){sleep(20);_exit(0);}
    music_job=3;pid_t playing=music_pid;
    f=fopen("scripts/music-runtime.sh","w");assert(f);fputs("#!/bin/sh\nsleep 20\n",f);fclose(f);
    music_open_tab(MUSIC_SEARCH);strcpy(music_query,"new music");music_spawn(1);
    assert(music_pid==playing&&music_job==3&&music_lookup_pid>0);
    music_action(SDLK_ESCAPE);assert(!music_lookup_pid&&music_on&&music_pid==playing);
    music_action(SDLK_LEFT);assert(music_tab==MUSIC_LIBRARY&&music_lookup_pid>0&&music_pid==playing);
    music_open_tab(MUSIC_RECENTS);assert(music_count==2&&!music_lookup_pid&&music_pid==playing);
    assert(music_queue_count==1&&!strcmp(music_queue[0].key,"original"));
    music_action(SDLK_ESCAPE);assert(!music_on&&music_pid==playing);
    music_open();assert(music_tab==MUSIC_RECENTS&&music_count==2);
    music_stop();music_lookup_stop();SDL_Quit();
    puts("PASS: persistent Recents home, separate browsing worker, preserved playback and queue");return 0;
}
