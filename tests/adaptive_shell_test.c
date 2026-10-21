#ifndef FLXTR_ARM_SHELL_TEST
#define FLXTR_DESKTOP
#endif
#define FLXTR_ADAPTIVE_TEST
#define main source_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void file(const char *name,const char *text){
    FILE *f=fopen(name,"w");assert(f);fputs(text,f);assert(!fclose(f));
}
static void reap(void){int code;assert(player_pid>0);assert(waitpid(player_pid,&code,0)==player_pid);}
int main(void){
    char dir[]="/tmp/flxtr-adaptive-shell-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_TIMER));assert(!open_ui());
    assert(quality_setting==3&&quality_height()==720);
    file("settings.cfg","quality=3\n");load_settings();assert(quality_setting==3);
    file("greenlink-player","#!/bin/sh\nprintf '%s\\n' \"$@\" >args.txt\nexit 0\n");assert(!chmod("greenlink-player",0700));
    Title title={0};strcpy(title.kind,"episode");strcpy(title.title,"Fixture");strcpy(title.url,"https://example.test/master.m3u8");
    launch_player(&title);reap();
    file("adaptive-request","480 24.5 2 720 1\n");finish_player(46<<8);
    assert(adaptive_height==480&&adaptive_blocked==2&&adaptive_pending);
    reap();
    char args[1000]={0};FILE *f=fopen("args.txt","r");assert(f);assert(fread(args,1,sizeof(args)-1,f)>0);fclose(f);
    assert(strstr(args,"--height\n480")&&strstr(args,"--start\n24.500"));
    /* An initialization/seek failure rolls back once to the previous tier. */
    finish_player(44<<8);assert(adaptive_height==720&&!adaptive_pending&&(adaptive_blocked&1));reap();
    file("adaptive-ready","1\n");finish_player(0);
    /* Reject malicious/unbounded adaptation requests instead of restarting. */
    tv_active=1;file("adaptive-request","2160 9 0 720 4\n");finish_player(46<<8);
    assert(!player_pid&&!tv_active);
    /* A failed higher-quality YouTube lookup keeps the prior signed stream. */
    assert(!mkdir("scripts",0700));file("scripts/music-runtime.sh","#!/bin/sh\nexit 1\n");assert(!chmod("scripts/music-runtime.sh",0700));
    quality_setting=3;adaptive_height=720;
    strcpy(title.kind,"youtube");strcpy(title.meta,"https://www.youtube.com/watch?v=abcdefghijk");
    launch_player(&title);reap();
    file("adaptive-request","1080 12 0 720 4\n");finish_player(46<<8);assert(youtube_pid&&adaptive_pending);
    Uint32 began=SDL_GetTicks();
    while(youtube_pid){youtube_poll();assert(SDL_GetTicks()-began<5000);SDL_Delay(2);}
    assert(player_pid>0&&adaptive_height==720&&!adaptive_pending&&(adaptive_blocked&4));reap();
    file("playback-request","7 0 0 1 2 0\n");finish_player(40<<8);
    settings_on=1;
    for(int tab=0;tab<SETTINGS_TABS;tab++){settings_tab=tab;settings_row=0;draw(0);} /* every tab must have labels */
    settings_tab=SETTINGS_MOVIES;
    quality_setting=2;settings_on=1;settings_row=0;action(SDLK_RIGHT);assert(quality_setting==3);
    action(SDLK_RIGHT);assert(quality_setting==0); /* manual selection still available */
    close_ui();SDL_Quit();
    puts("PASS: Auto setting persistence, exact target/resume restart, one rollback and invalid-request rejection");
}
