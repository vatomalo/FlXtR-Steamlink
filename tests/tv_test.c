#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void script(const char *name,const char *body){FILE *f=fopen(name,"w");assert(f);fputs(body,f);fclose(f);assert(!chmod(name,0700));}
int main(void){
    char temp[]="/tmp/flxtr-tv-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));load_settings();
    assert(tv_valid(tv_blocks));assert(tv_block_at(tv_blocks,14)==2);assert(tv_block_at(tv_blocks,15)==3);assert(tv_block_at(tv_blocks,17)==4);assert(tv_block_at(tv_blocks,19)==5);assert(tv_block_at(tv_blocks,22)==6);
    tv_blocks[3].hour=14;tv_blocks[3].genre=6;save_settings();memset(tv_blocks,0,sizeof(tv_blocks));load_settings();assert(tv_blocks[3].hour==14&&tv_blocks[3].genre==6);
    tv_blocks[0].hour=3;save_settings();load_settings();assert(tv_valid(tv_blocks)&&tv_blocks[0].hour==0);
    tv_progress_save(1396,1,1);tv_progress_save(1396,1,2);TvProgress rows[256];assert(tv_progress_read(rows)==1&&rows[0].episode==2);
    unlink("tv-progress.tsv");
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_TIMER));assert(!open_ui());browse.mode=11;
    settings_on=1;settings_row=7;action(SDLK_RETURN);assert(tv_schedule_on);tv_column=0;tv_row=0;action(SDLK_RIGHT);assert(!tv_blocks[0].hour);action(SDLK_ESCAPE);settings_on=0;
    script("greenlink-catalog","#!/bin/sh\ncase \"$1\" in\ntv-pick) printf 'TEST SHOW\\tTV\\t\\t\\tepisode\\t1396\\t1\\t2\\n';;\nsource) printf 'alpha\\tSERVER\\t\\t\\tserver\\t1396\\t1\\t2\\n';;\nquality) printf 'AUTO\\tBEST\\t\\thttps://example.org/video.mp4\\tsource\\t1396\\t1\\t2\\n';;\nesac\n");
    script("greenlink-player","#!/bin/sh\nexit 0\n");
    tv_active=1;player_pid=123;tv_tick();assert(!catalog_pid);player_pid=0;
    tv_next_at=0;tv_tick();assert(catalog_pid);Uint32 start=SDL_GetTicks();
    while(catalog_pid||player_pid){assert(SDL_GetTicks()-start<5000);if(catalog_pid)finish_catalog();if(player_pid){int code;if(waitpid(player_pid,&code,WNOHANG)==player_pid)finish_player(code);}SDL_Delay(1);}
    assert(tv_active&&!tv_attempts&&tv_progress_read(rows)==1&&rows[0].id==1396&&rows[0].episode==2);
    FILE *f=fopen("playback-request","w");assert(f);fputs("7 10 0 0 2 0\n",f);fclose(f);close_ui();finish_player(40<<8);assert(!tv_active);
    tv_break_due=0;tv_active=1;tv_attempts=12;tv_next_at=0;tv_tick();assert(!tv_active&&!catalog_pid);
    assert(tv_sequence("sequence")==0&&tv_sequence("sequence")==1&&tv_sequence("sequence")==2);
    unlink("tv-recent.tsv");for(int i=1;i<=40;i++)tv_recent_add(i,1,1);
    TvRecent recent[32];assert(tv_recent_read(recent)==32&&recent[0].id==9&&recent[31].id==40);
    assert(!tv_recent_has(8,1,1,0)&&tv_recent_has(9,1,1,0));
    assert(!tv_recent_has(34,1,2,1)&&tv_recent_has(35,1,2,1)&&!tv_recent_has(35,1,2,0));
    tv_active=1;tv_attempts=0;tv_break_due=1;tv_tick();assert(catalog_pid&&pending.mode==13);
    while(catalog_pid){finish_catalog();SDL_Delay(1);}assert(tv_active&&!player_pid&&!tv_break_due);
    script("greenlink-catalog","#!/bin/sh\nprintf 'VINTAGE AD\\tAD\\t\\thttps://example.org/ad.mp4\\ttv-break\\t0\\t0\\t0\\n'\n");
    tv_break_due=1;tv_tick();assert(catalog_pid);start=SDL_GetTicks();
    while(catalog_pid||player_pid){assert(SDL_GetTicks()-start<5000);if(catalog_pid)finish_catalog();if(player_pid){assert(tv_in_break);int code;if(waitpid(player_pid,&code,WNOHANG)==player_pid)finish_player(code);}SDL_Delay(1);}
    assert(tv_active&&!tv_in_break&&!tv_break_due&&tv_progress_read(rows)==1&&rows[0].episode==2);
    tv_in_break=1;close_ui();finish_player(0);assert(tv_active&&!tv_in_break&&tv_progress_read(rows)==1&&rows[0].episode==2);
    tv_active=0;browse=(Browse){7,3,0,0,0,0,"",""};
    action(SDLK_y);assert(filters_on);action(SDLK_RIGHT);assert(browse.id==1&&browse.season==0);
    action(SDLK_DOWN);action(SDLK_RIGHT);assert(browse.id==1&&browse.season==1);
    action(SDLK_ESCAPE);assert(!filters_on&&!browse.id&&!browse.season);
    action(SDLK_y);action(SDLK_RIGHT);action(SDLK_RETURN);assert(pending.mode==7&&pending.id==1&&pending.season==0&&pending.page==1);
    while(catalog_pid){finish_catalog();SDL_Delay(1);}
    close_ui();SDL_Quit();
    puts("PASS: schedule boundaries, editable settings, validation, completion progress, no mid-episode switch, TV stop and bounded retries");return 0;
}
