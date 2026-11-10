#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void finished_audio(int code){
    music_pid=fork();assert(music_pid>=0);
    if(!music_pid)_exit(code);
    music_job=3;
    while(!music_poll())SDL_Delay(1);
}
int main(void){
    char path[]="/tmp/flxtr-music-XXXXXX";assert(mkdtemp(path));assert(!chdir(path));
    assert(!SDL_Init(SDL_INIT_TIMER));
    assert(!mkdir("scripts",0700));
    FILE *f=fopen("scripts/music-runtime.sh","w");assert(f);
    fputs("#!/bin/sh\nsleep 20\n",f);fclose(f);
    music_count=2;music_play_index=0;strcpy(music_rows[1].kind,"video");strcpy(music_rows[1].key,"abcdefghijk");
    finished_audio(0);assert(music_job==2&&music_selected==1);music_stop();
    music_play_index=1;finished_audio(0);assert(music_job==4);music_stop();
    finished_audio(1);assert(!music_pid&&!music_job);
    music_spawn(4);music_stop();assert(!music_poll()&&!music_pid&&!music_job);
    unlink("scripts/music-runtime.sh");rmdir("scripts");unlink("music-background.log");
    unlink("music.log");rmdir("music-cache");assert(!chdir("/tmp"));rmdir(path);
    SDL_Quit();puts("PASS: queued tracks precede recommendations; failures and explicit stop never autoplay");return 0;
}
