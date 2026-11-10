#define FLXTR_DESKTOP
#define main source_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void file(const char *path,const char *value){
    FILE *f=fopen(path,"w");assert(f);fputs(value,f);assert(!fclose(f));
}
static void reap(void){int code;assert(player_pid>0);assert(waitpid(player_pid,&code,0)==player_pid);}
static void resolve(void){
    Uint32 start=SDL_GetTicks();
    while(youtube_pid){youtube_poll();assert(SDL_GetTicks()-start<5000);SDL_Delay(2);}
}
int main(void){
    char dir[]="/tmp/flxtr-youtube-recovery-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_TIMER));assert(!open_ui());
    assert(!mkdir("scripts",0700));
    file("scripts/music-runtime.sh","#!/bin/sh\nprintf '%s' \"$3\" >resolved-query\nmkdir -p music-cache\nprintf 'Recovered clip\\nhttps://example.test/new.mp4\\n\\nFixture agent\\nhttps://www.youtube.com/watch?v=abcdefghijk\\n' >music-cache/video.txt\n");
    file("greenlink-player","#!/bin/sh\nprintf '%s\\n' \"$@\" >args.txt\nexit 0\n");assert(!chmod("greenlink-player",0700));
    Title title={0};strcpy(title.kind,"youtube");strcpy(title.title,"Fixture");
    strcpy(title.url,"https://example.test/expired.mp4");strcpy(title.meta,"https://www.youtube.com/watch?v=abcdefghijk");
    launch_player(&title);reap();
    for(unsigned attempt=1;attempt<=3;attempt++){
        file("youtube-playback-outcome","24.500 180.000 47\n");finish_player(47<<8);
        assert(youtube_pid>0&&youtube_recovery_attempts==attempt&&youtube_resume==23.5);
        resolve();assert(player_pid>0&&youtube_recovery_attempts==attempt);reap();
        char args[4096]={0};FILE *f=fopen("args.txt","r");assert(f);assert(fread(args,1,sizeof(args)-1,f)>0);fclose(f);
        assert(strstr(args,"--start\n23.500"));
        char query[128]={0};f=fopen("resolved-query","r");assert(f);assert(fread(query,1,sizeof(query)-1,f)>0);fclose(f);
        assert(!strcmp(query,title.meta));
    }
    finish_player(47<<8);assert(!youtube_pid&&!player_pid&&youtube_recovery_attempts==3);
    assert(!youtube_spawn(title.meta,0)&&!youtube_recovery_attempts);youtube_stop();
    launch_player(&title);reap();stopping_player=1;finish_player(47<<8);assert(!youtube_pid);
    launch_player(&title);reap();finish_player(0);assert(!youtube_pid);
    launch_player(&title);reap();file("youtube-playback-outcome","nan 180 47\n");finish_player(47<<8);
    assert(youtube_pid>0&&youtube_resume==0);youtube_stop();
    close_ui();SDL_Quit();
    puts("PASS: canonical URL recovery, position rewind, three-attempt limit, fresh-request reset, stop, EOF and invalid position");
}
