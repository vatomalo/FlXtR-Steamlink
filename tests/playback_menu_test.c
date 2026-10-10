#define _POSIX_C_SOURCE 200809L
#include <SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include "../src/font.h"
#include "../src/video_layout.h"
#define AV_TIME_BASE 1000000
#define AV_NOPTS_VALUE INT64_MIN
typedef int CSLVideoOverlay;
static int64_t now=20000000,origin=0,clock_start=10000000;
static int64_t av_gettime_relative(void){return now;}
static int stopped,viewing,subtitle_size=2,subtitle_delay,subtitle_visible;
static int *view_context;
static CSLVideoOverlay *view_overlay,*subtitle_overlay;
static Uint32 overlay_until;
static CSLVideoOverlay *SLVideo_CreateOverlay(int *c,int w,int h){(void)c;(void)w;(void)h;return NULL;}
static void SLVideo_HideOverlay(CSLVideoOverlay *p){(void)p;}
static void SLVideo_ShowOverlay(CSLVideoOverlay *p){(void)p;}
static void SLVideo_GetOverlayPixels(CSLVideoOverlay *p,uint32_t **pixels,int *pitch){(void)p;*pixels=NULL;*pitch=0;}
static void SLVideo_SetOverlayDisplayArea(CSLVideoOverlay *p,float a,float b,float c,float d){(void)p;(void)a;(void)b;(void)c;(void)d;}
static void show_view(const char *s){(void)s;}
static void apply_view(int notify){(void)notify;}
#include "../src/playback_menu.h"
static double request(int expected){
    FILE *f=fopen("playback-request","r");assert(f);int action,v,s,z,d;double pos;
    assert(fscanf(f,"%d %lf %d %d %d %d",&action,&pos,&v,&s,&z,&d)==6);fclose(f);
    assert(action==expected&&requested_action==40&&stopped);unlink("playback-request");stopped=requested_action=0;return pos;
}
int main(void){
    char temp[]="/tmp/flxtr-menu-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    VideoRect rect;assert(!video_rect(VIEW_FIT,640,360,1,1,1920,1080,&rect));assert(view_names[0]);
    media_duration=100;assert(playback_position()==10);
    menu_command('l');assert(request(1)==0);
    menu_command('r');assert(request(1)==20);
    media_duration=15;menu_command('r');assert(request(1)==14);media_duration=100;
    menu_command('m');assert(menu_open);menu_command('a');assert(paused);
    now+=5000000;assert(playback_position()==10);menu_command('a');assert(!paused&&playback_position()==10);
    menu_row=7;menu_command('a');assert(!stopped);menu_servers=1;menu_command('a');assert(request(4)==10);
    menu_row=8;menu_command('a');assert(!stopped);menu_episodes=1;menu_command('a');assert(request(5)==0);
    menu_row=9;menu_command('a');assert(request(6)==0);
    menu_row=2;menu_command('l');assert(menu_subtitles==4&&request(1)==10);
    menu_row=4;for(int i=0;i<20;i++)menu_command('l');assert(subtitle_delay==-5);
    menu_command('b');assert(!menu_open&&!stopped);menu_command('b');assert(request(7)==10);
    int fd[2];assert(!pipe(fd));control_fd=fd[0];assert(!fcntl(control_fd,F_SETFL,O_NONBLOCK));
    assert(write(fd[1],"md",2)==2);menu_row=0;menu_poll();assert(menu_open&&menu_row==1);close(fd[0]);close(fd[1]);
    assert(!chdir("/tmp"));rmdir(temp);
    puts("PASS: playback commands, seek bounds, pause clock, disabled actions, subtitle limits and control pipe");return 0;
}
