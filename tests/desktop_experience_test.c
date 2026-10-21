#define FLXTR_DESKTOP
#define main source_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void write_file(const char *name,const char *value,int executable){
    FILE *f=fopen(name,"w");assert(f);fputs(value,f);assert(!fclose(f));
    if(executable)assert(!chmod(name,0700));
}
int main(void){
    char temp[]="/tmp/flxtr-experience-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_TIMER));assert(!open_ui());
    menu_theme=3;load_menu_background();assert(green.b==255&&theme_canvas.b==31);
    menu_theme=4;load_menu_background();assert(green.r==255&&green.g==185);
    menu_theme=5;load_menu_background();assert(!access("themes/custom/theme.ini",R_OK));
    write_file("themes/custom/theme.ini","accent=#123456\ncanvas=#020304\ntext=#bad\n",0);
    load_menu_background();assert(green.r==0x12&&green.g==0x34&&green.b==0x56&&theme_canvas.r==2);
    save_settings();menu_theme=0;load_settings();assert(menu_theme==5);
    desktop_key(SDLK_F1,KMOD_NONE);assert(keyboard_help);
    desktop_key(SDLK_ESCAPE,KMOD_NONE);assert(!keyboard_help&&running);
    desktop_key(SDLK_F10,KMOD_NONE);assert(settings_on);
    desktop_key(SDLK_ESCAPE,KMOD_NONE);assert(!settings_on);
    desktop_key(SDLK_F4,KMOD_NONE);assert(menu_theme==0);
    music_on=music_edit=1;strcpy(music_query,"cafe");desktop_key(SDLK_m,KMOD_NONE);assert(music_edit&&!strcmp(music_query,"cafe"));
    strcpy(music_query,"CAF\xc3\x89");music_backspace();assert(!strcmp(music_query,"CAF"));music_on=music_edit=0;
    memcpy(tv_blocks,tv_defaults,sizeof(tv_blocks));tv_blocks[0].genre=TV_MUSIC_VIDEOS;assert(tv_valid(tv_blocks));
    assert(!mkdir("scripts",0700));
    write_file("scripts/music-runtime.sh","#!/bin/sh\nprintf '%s\\n' \"$@\" >worker-args.txt\nmkdir -p music-cache\nprintf 'Fixture video\\nhttps://example.test/video.mp4\\n\\nMozilla/5.0\\n' >music-cache/video.txt\n",1);
    write_file("greenlink-player","#!/bin/sh\nprintf '%s\\n' \"$@\" >player-args.txt\nexit 0\n",1);
    tv_active=mtv_override=1;tv_attempts=0;tv_next_at=0;
    tv_tick();assert(youtube_pid>0&&!catalog_pid);
    Uint32 began=SDL_GetTicks();
    while(youtube_pid){youtube_poll();assert(SDL_GetTicks()-began<3000);SDL_Delay(1);}
    assert(player_pid>0&&!strcmp(playing_title.kind,"youtube"));
    char args[500]={0};FILE *f=fopen("worker-args.txt","r");assert(f);assert(fread(args,1,sizeof(args)-1,f)>0);fclose(f);
    assert(strstr(args,"tv-video")&&strstr(args,"720"));
    int code;assert(waitpid(player_pid,&code,0)==player_pid);finish_player(code);
    assert(tv_active&&!tv_attempts&&tv_next_at==0);
    assert(access("tv-progress.tsv",R_OK));
    write_file("playback-request","7 0 0 1 2 0\n",0);finish_player(40<<8);assert(!tv_active);
    write_file("playback-request","10 0 0 1 2 0\n",0);finish_player(40<<8);assert(!running);
    close_ui();SDL_Quit();
    puts("PASS: theme palettes/custom RGB/persistence, keyboard help/editing, MTV worker/player handoff, EOF and Stop");
    return 0;
}
