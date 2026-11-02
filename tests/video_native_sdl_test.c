#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void play_once(void){
    Title t={.title="TEST",.kind="movie",.url="https://media.example/test.m3u8"};
    launch_player(&t);assert(player_pid>0);int code;assert(waitpid(player_pid,&code,0)==player_pid);finish_player(code);
}
static void captured(char *out,size_t cap){FILE *f=fopen("child-library-path","r");assert(f);size_t n=fread(out,1,cap-1,f);out[n]=0;fclose(f);}
int main(void){
    char tmp[]="/tmp/flxtr-video-sdl-XXXXXX";assert(mkdtemp(tmp));assert(!chdir(tmp));
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_TIMER));assert(!open_ui());
    FILE *f=fopen("greenlink-player","w");assert(f);fputs("#!/bin/sh\nprintf '%s' \"$LD_LIBRARY_PATH\" > child-library-path\n",f);fclose(f);chmod("greenlink-player",0700);
    setenv("LD_LIBRARY_PATH","/original/path",1);unsetenv("FLXTR_USE_SYSTEM_SDL");
    char path[PATH_MAX*2];play_once();captured(path,sizeof(path));assert(!strcmp(path,"/original/path"));
    mkdir("retroarch-libs",0700);f=fopen("retroarch-libs/libSDL2-2.0.so.0","w");assert(f);fputs("fixture",f);fclose(f);
    play_once();captured(path,sizeof(path));assert(strstr(path,"/retroarch-libs:/original/path"));
    assert(!strcmp(getenv("LD_LIBRARY_PATH"),"/original/path"));
    setenv("FLXTR_USE_SYSTEM_SDL","1",1);play_once();captured(path,sizeof(path));assert(!strcmp(path,"/original/path"));
    close_ui();SDL_Quit();puts("PASS: native SDL2 is selected only in Steam Link video children; missing library and system override preserve the original path");return 0;
}
