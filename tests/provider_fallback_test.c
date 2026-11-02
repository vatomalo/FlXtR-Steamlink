#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void executable(const char *name,const char *body){FILE *f=fopen(name,"w");assert(f);fputs(body,f);fclose(f);assert(!chmod(name,0700));}
int main(void){
    char tmp[]="/tmp/flxtr-providers-XXXXXX";assert(mkdtemp(tmp));assert(!chdir(tmp));
    executable("greenlink-catalog","#!/bin/sh\nif [ \"$1\" = source ]; then\nfor n in alpha fmovies sflix; do printf '%s\\tSERVER\\t\\t\\tserver\\t61222\\t1\\t1\\t0\\n' \"$n\"; done\nelse\necho \"$7\" >> attempts\n[ \"$7\" = sflix ] || exit 1\nprintf 'AUTO\\tSFLIX / BEST COMPATIBLE\\t\\thttps://media.example/master.m3u8\\tsource\\t61222\\t1\\t1\\t0\\n'\nfi\n");
    executable("greenlink-player","#!/bin/sh\nprintf '%s' \"$FLXTR_MEDIA_PROVIDER\" > provider\nexit 0\n");
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_TIMER));assert(!open_ui());
    browse.mode=4;count=total=1;selection=visible[0]=0;
    titles[0]=(Title){.title="EPISODE",.kind="episode",.id=61222,.season=1,.episode=1};
    assert(server_preference_save("episode","sflix")); /* Cannot promote provider 3. */
    play();Uint32 start=SDL_GetTicks();
    while(catalog_pid||player_pid){assert(SDL_GetTicks()-start<5000);if(catalog_pid)finish_catalog();if(player_pid){int status;if(waitpid(player_pid,&status,WNOHANG)==player_pid)finish_player(status);}SDL_Delay(1);}
    char attempts[80]={0},provider[16]={0};FILE *f=fopen("attempts","r");assert(f);assert(fread(attempts,1,sizeof(attempts)-1,f)>0);fclose(f);
    f=fopen("provider","r");assert(f);assert(fread(provider,1,sizeof(provider)-1,f)>0);fclose(f);
    assert(!strcmp(attempts,"alpha\nfmovies\nsflix\n")&&!strcmp(provider,"sflix"));
    assert(!auto_active&&!strcmp(status,"PLAYBACK FINISHED")&&browse.mode==4);
    close_ui();SDL_Quit();
    puts("PASS: Flixer -> FMovies -> SFlix, failed providers advance, saved preferences preserve order and playback receives provider headers");return 0;
}
