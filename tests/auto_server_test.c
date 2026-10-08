#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>

static void script(const char *name,const char *body){
    FILE *f=fopen(name,"w");assert(f);fputs(body,f);fclose(f);assert(!chmod(name,0700));
}
static void pump(void){
    Uint32 start=SDL_GetTicks();
    while(catalog_pid||player_pid){
        assert(SDL_GetTicks()-start<5000);
        if(catalog_pid)finish_catalog();
        if(player_pid){int code;if(waitpid(player_pid,&code,WNOHANG)==player_pid)finish_player(code);}
        SDL_Delay(1);
    }
}
int main(void){
    char temp[]="/tmp/flxtr-auto-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    script("greenlink-catalog",
        "#!/bin/sh\n"
        "[ \"$4:$5:$6\" = '1396:1:2' ] || exit 9\n"
        "[ \"$TEST_SLOW\" != 1 ] || exec sleep 10\n"
        "if [ \"$1\" = source ] || [ \"$1\" = kiss-source ]; then\n"
        " echo '# pages=2 total=3'\n"
        " if [ \"$2\" = 1 ]; then names='alpha beta alpha'; else names=gamma; fi\n"
        " for n in $names; do printf '%s\\tSERVER\\t\\t\\tserver\\t1396\\t1\\t2\\t0\\n' \"$n\"; done\n"
        "else\n"
        " echo \"$7\" >> attempts\n"
        " [ \"$7\" != alpha ] || exit 1\n"
        " printf 'AUTO\\tBEST\\t\\thttps://example.org/%s\\tsource\\t1396\\t1\\t2\\t0\\n' \"$7\"\n"
        "fi\n");
    script("greenlink-player",
        "#!/bin/sh\n"
        "[ \"$1\" = https://example.org/gamma ] && [ \"$5\" = 720 ] && [ \"$TEST_FAIL\" != 1 ]\n");
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_TIMER));assert(!open_ui());
    count=total=1;visible[0]=selection=0;browse.mode=4;history_size=2;
    titles[0]=(Title){.title="EPISODE TWO",.kind="episode",.id=1396,.season=1,.episode=2};
    play();assert(auto_active&&catalog_pid&&browse.mode==4);pump();
    assert(!auto_active&&auto_count==3&&auto_index==2);
    assert(!strcmp(status,"PLAYBACK FINISHED"));
    assert(browse.mode==4&&history_size==2&&selection==0&&!strcmp(titles[0].title,"EPISODE TWO"));
    FILE *f=fopen("attempts","r");char buf[80]={0};assert(f);fread(buf,1,sizeof(buf)-1,f);fclose(f);
    assert(!strcmp(buf,"alpha\nbeta\ngamma\n"));
    strcpy(titles[0].kind,"kiss-episode");play();assert(!strcmp(auto_title.kind,"kiss-episode"));pump();assert(!strcmp(status,"PLAYBACK FINISHED"));
    SDL_setenv("TEST_FAIL","1",1);play();pump();assert(!auto_active&&strstr(status,"NO WORKING SERVER"));
    SDL_setenv("TEST_SLOW","1",1);play();action(SDLK_ESCAPE);assert(!catalog_pid&&!auto_active&&strstr(status,"CANCELLED"));
    /* A timeout advances/fails cleanly; it cannot strand the input lock. */
    play();catalog_started=SDL_GetTicks()-70001;pump();assert(!auto_active&&strstr(status,"LOOKUP FAILED"));
    /* Explicit stop never triggers another server, even on a nonzero exit. */
    auto_active=1;auto_index=0;stopping_player=1;close_ui();finish_player(256);
    assert(!auto_active&&!catalog_pid&&!strcmp(status,"PLAYBACK STOPPED"));
    /* Matching player receives a control pipe and restarts at the requested time. */
    script("player-menu-v1","1\n");
    script("greenlink-player",
        "#!/bin/sh\n"
        "printf '%s\\n' \"$@\" >> launch-args\n"
        "if [ ! -f requested ]; then touch requested; echo '1 12.5 0 1 3 2' > playback-request; exit 40; fi\n"
        "exit 0\n");
    browse.mode=0;auto_active=0;restart_position=0;
    Title direct={.title="TEST",.url="https://example.org/video.mp4"};
    launch_player(&direct);assert(player_menu&&player_control>=0);pump();
    assert(!strcmp(status,"PLAYBACK FINISHED")&&subtitle_setting==1&&subtitle_scale==3&&subtitle_delay==2);
    f=fopen("launch-args","r");assert(f);char args[2048]={0};fread(args,1,sizeof(args)-1,f);fclose(f);
    assert(strstr(args,"--control-fd\n")&&strstr(args,"--start\n12.500\n"));
    /* Expiring anime URLs must be resolved again before a seek restart. */
    unsetenv("TEST_SLOW");unsetenv("TEST_FAIL");
    playing_title=auto_title;auto_active=1;auto_index=0;auto_count=1;strcpy(auto_servers[0],"gamma");
    f=fopen("playback-request","w");assert(f);fputs("1 75 0 0 2 0\n",f);fclose(f);
    close_ui();finish_player(40<<8);assert(catalog_pid&&!player_pid&&auto_resume==75);
    pump();assert(!strcmp(status,"PLAYBACK FINISHED"));
    f=fopen("launch-args","r");assert(f);memset(args,0,sizeof(args));fread(args,1,sizeof(args)-1,f);fclose(f);
    assert(strstr(args,"--start\n75.000\n"));
    /* A requested stop must not fall through to automatic server retry. */
    f=fopen("playback-request","w");assert(f);fputs("7 12 0 0 2 0\n",f);fclose(f);
    auto_active=1;close_ui();finish_player(40<<8);assert(!auto_active&&!catalog_pid&&!strcmp(status,"PLAYBACK STOPPED"));
    script("greenlink-catalog","#!/bin/sh\necho '# pages=2 total=7'\nprintf 'EPISODE SEVEN\\tTEST\\t\\thttps://example.org/seven.mp4\\tsource\\t1396\\t1\\t7\\t0\\n'\n");
    browse.mode=4;browse.page=1;selection=0;total=1;listing_pages=2;
    f=fopen("playback-request","w");assert(f);fputs("6 0 0 0 2 0\n",f);fclose(f);
    close_ui();finish_player(40<<8);assert(catalog_pid&&play_after_load);pump();
    assert(browse.page==2&&!play_after_load&&!strcmp(playing_title.title,"EPISODE SEVEN"));
    unlink("player-menu-v1");unlink("requested");unlink("launch-args");unlink("settings.cfg");
    close_ui();SDL_Quit();
    unlink("greenlink-catalog");unlink("greenlink-player");unlink("attempts");unlink("player.log");unlink("catalog.log");
    unlink("catalog-cache/result.tsv");rmdir("catalog-cache");assert(!chdir("/tmp"));rmdir(temp);
    puts("PASS: paged/deduplicated servers, lookup and player failure fallback, exhaustion, cancellation, timeout and library preservation");
    return 0;
}
