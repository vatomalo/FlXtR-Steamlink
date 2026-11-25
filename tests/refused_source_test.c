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
    char temp[]="/tmp/flxtr-refused-XXXXXX",buf[80];FILE *f;assert(mkdtemp(temp));assert(!chdir(temp));
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_TIMER));assert(!open_ui());
    history_size=2;
    /* Refused links (exit 48): three mirrors, then only the other providers. */
    script("greenlink-catalog",
        "#!/bin/sh\n"
        "if [ \"$1\" = source ]; then echo '# pages=1 total=6'\n"
        " for n in alpha beta gamma delta foxtrot fmovies; do printf '%s\\tSERVER\\t\\t\\tserver\\t1396\\t1\\t2\\t0\\n' \"$n\"; done\n"
        "else echo \"$7\" >> refused\n"
        " printf 'AUTO\\tBEST\\t\\thttps://example.org/%s\\tsource\\t1396\\t1\\t2\\t0\\n' \"$7\"\n"
        "fi\n");
    script("greenlink-player","#!/bin/sh\nexit 48\n");
    browse.mode=4;browse.page=1;selection=0;count=total=1;visible[0]=0;
    titles[0]=(Title){.title="EPISODE TWO",.kind="episode",.id=1396,.season=1,.episode=2};
    play();pump();
    assert(!auto_active&&strstr(status,"VIDEO HOST REFUSED"));
    f=fopen("refused","r");assert(f);memset(buf,0,sizeof(buf));assert(fread(buf,1,sizeof(buf)-1,f));fclose(f);
    assert(!strcmp(buf,"alpha\nbeta\ngamma\nfmovies\n"));
    /* A later ordinary failure reports as one again. */
    script("greenlink-player","#!/bin/sh\nexit 1\n");
    play();pump();assert(!auto_active&&strstr(status,"ALL SOURCES FAILED"));
    unlink("refused");
    close_ui();SDL_Quit();
    puts("PASS: refused links stop after three mirrors, still try other providers and are reported");
    return 0;
}
