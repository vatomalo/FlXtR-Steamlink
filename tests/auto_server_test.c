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
    close_ui();SDL_Quit();
    unlink("greenlink-catalog");unlink("greenlink-player");unlink("attempts");unlink("player.log");unlink("catalog.log");
    unlink("catalog-cache/result.tsv");rmdir("catalog-cache");assert(!chdir("/tmp"));rmdir(temp);
    puts("PASS: paged/deduplicated servers, lookup and player failure fallback, exhaustion, cancellation, timeout and library preservation");
    return 0;
}
