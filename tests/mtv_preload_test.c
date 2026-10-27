#define FLXTR_DESKTOP
#define main source_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void file(const char *name,const char *content,int executable){
    FILE *f=fopen(name,"w");assert(f);fputs(content,f);assert(!fclose(f));
    if(executable)assert(!chmod(name,0700));
}
static void wait_file(const char *name){
    Uint32 start=SDL_GetTicks();
    while(access(name,F_OK)){assert(SDL_GetTicks()-start<5000);SDL_Delay(5);}
}
int main(void){
    char dir[]="/tmp/flxtr-mtv-preload-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_TIMER));assert(!open_ui());
    assert(!mkdir("scripts",0700));
    file("scripts/music-runtime.sh","#!/bin/sh\nmkdir -p music-cache\nprintf 'Next clip\\nhttps://example.test/next.mp4\\nhttps://example.test/next.m4a\\nFixture agent\\nhttps://www.youtube.com/watch?v=abcdefghijk\\n' >music-cache/video.txt\n",1);
    file("greenlink-player","#!/usr/bin/python3\nimport os,sys,time\nfrom pathlib import Path\na=sys.argv\nif '--preload-fd' in a:\n Path('next-preloaded').touch()\n while True:\n  command=os.read(int(a[a.index('--preload-fd')+1]),1)\n  if command==b'B': Path('next-expanded').touch()\n  elif command==b'G': break\n  else: sys.exit(1)\n Path('next-started').touch()\n while not Path('finish-next').exists(): time.sleep(.01)\nelse:\n Path('current-started').touch()\n while not Path('finish-current').exists(): time.sleep(.01)\n",1);
    tv_active=mtv_override=1;
    Title title={0};strcpy(title.kind,"youtube");strcpy(title.title,"Current clip");strcpy(title.url,"https://example.test/current.mp4");
    strcpy(youtube_audio,"https://example.test/current.m4a");strcpy(youtube_agent,"Current agent");
    launch_player(&title);assert(player_pid>0);wait_file("current-started");
    pid_t current=player_pid;PlayerPipe control=player_control;
    youtube_prefetch();assert(youtube_pid>0&&youtube_preparing);
    Uint32 began=SDL_GetTicks();
    while(youtube_pid){youtube_poll();assert(SDL_GetTicks()-began<5000);SDL_Delay(2);}
    wait_file("next-preloaded");
    assert(tv_prepared&&tv_buffer_pid>0&&player_pid==current&&player_control==control);
    assert(!strcmp(playing_title.title,"Current clip"));
    assert(!strcmp(youtube_audio,"https://example.test/current.m4a"));
    assert(access("next-started",F_OK));
    /* Missing duration and distant EOF must leave the early buffer small. */
    file("playback-position","10 200\n",0);youtube_prefetch();
    assert(access("next-expanded",F_OK));
    SDL_Delay(1100);
    file("playback-position","175 200\n",0);youtube_prefetch();
    wait_file("next-expanded");assert(access("next-started",F_OK));
    pid_t next=tv_buffer_pid;
    file("finish-current","",0);int code;assert(waitpid(current,&code,0)==current&&!code);
    file("playback-request","6 12 0 1 2 0\n",0);
    finish_player(40<<8);wait_file("next-started");
    assert(player_pid==next&&!tv_prepared&&tv_active&&!auto_active);
    assert(!strcmp(playing_title.title,"Next clip"));
    assert(!strcmp(youtube_audio,"https://example.test/next.m4a"));
    assert(!strcmp(playing_title.meta,"https://www.youtube.com/watch?v=abcdefghijk"));
    file("playback-request","2 12 0 1 2 0\n",0);file("finish-next","",0);
    assert(waitpid(next,&code,0)==next);
    finish_player(40<<8);
    assert(youtube_pid>0&&!youtube_preparing&&youtube_height==1080&&youtube_resume==12&&tv_active);
    began=SDL_GetTicks();
    while(youtube_pid){youtube_poll();assert(SDL_GetTicks()-began<5000);SDL_Delay(2);}
    assert(player_pid>0&&playing_title.height==1080&&!strcmp(playing_title.title,"Next clip"));
    assert(waitpid(player_pid,&code,0)==player_pid);
    file("playback-request","7 0 0 1 2 0\n",0);
    finish_player(40<<8);assert(!tv_active&&!tv_buffer_pid&&!youtube_pid);
    /* Stop must also cancel a resolver running behind the active player. */
    tv_active=1;player_pid=123;strcpy(playing_title.kind,"youtube");
    youtube_retry_at=0;youtube_prefetch();assert(youtube_preparing&&youtube_pid>0);
    pid_t resolving=youtube_pid;
    file("playback-request","6 0 0 1 2 0\n",0);
    finish_player(40<<8);
    assert(!player_pid&&youtube_pid==resolving&&youtube_preparing&&tv_active);
    tv_cancel_prepared();assert(!youtube_pid&&!youtube_preparing&&tv_gate<0);
    player_pid=0;close_ui();SDL_Quit();
    puts("PASS: MTV resolves and gates next clip, expands near EOF, promotes prepared worker on Next and cancels on Stop");
}
