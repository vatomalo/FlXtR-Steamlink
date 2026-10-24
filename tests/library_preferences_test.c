#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void marker(const char *token){FILE *f=fopen("playback-started","w");assert(f);fprintf(f,"%s 4.000\n",token);fclose(f);}
int main(void){
    char temp[]="/tmp/flxtr-library-prefs-XXXXXX";assert(mkdtemp(temp));assert(!chdir(temp));
    assert(!SDL_Init(SDL_INIT_TIMER));
    int modes[]={1,2,7},genres[]={6,3,8},orders[]={2,1,3};
    for(int i=0;i<3;i++){
        browse=(Browse){.mode=modes[i],.page=1,.id=genres[i],.season=orders[i]};
        filters_on=1;action(SDLK_RETURN);assert(!filters_on);
    }
    memset(library_genre,0,sizeof(library_genre));memset(library_order,0,sizeof(library_order));load_settings();
    for(int i=0;i<3;i++){
        assert(library_genre[i]==genres[i]&&library_order[i]==orders[i]);
        browse.mode=0;
        library_pages[0].valid=1;library_pages[0].count=0;library_pages[0].pages=1;
        library_pages[0].key=(Browse){.mode=modes[i],.page=1,.id=genres[i],.season=orders[i]};
        request_catalog((Browse){.mode=modes[i],.page=1},0,0);
        assert(browse.mode==modes[i]&&browse.id==genres[i]&&browse.season==orders[i]);
    }
    browse=(Browse){.mode=1,.id=2,.season=0};filters_on=1;
    filter_original_genre=6;filter_original_order=2;action(SDLK_ESCAPE);
    assert(browse.id==6&&browse.season==2);load_settings();assert(library_genre[0]==6);
    FILE *f=fopen("settings.cfg","a");assert(f);fputs("series_genre=6\nanime_order=99\nmovie_genre=-1\n",f);fclose(f);
    load_settings();assert(library_genre[1]==3&&library_order[2]==3&&library_genre[0]==6);
    /* Resolver success alone never establishes a known-good server. */
    strcpy(auto_title.kind,"movie");auto_count=3;
    strcpy(auto_servers[0],"alpha");strcpy(auto_servers[1],"echo");strcpy(auto_servers[2],"foxtrot");
    auto_prefer_flixer_server();assert(!strcmp(auto_servers[0],"echo"));
    playing_title=(Title){.kind="movie",.id=123};strcpy(playback_server,"foxtrot");tv_history_active=0;
    char name[32],token[16];server_preference_read("movie",name);assert(!name[0]);
    marker("00000000");tv_history_poll();server_preference_read("movie",name);assert(!name[0]);
    bookmark_path(&playing_title,token,sizeof(token));marker(token);tv_history_poll();
    server_preference_read("episode",name);assert(!strcmp(name,"foxtrot"));
    auto_prefer_flixer_server();assert(!strcmp(auto_servers[0],"foxtrot")&&!strcmp(auto_servers[1],"echo")&&!strcmp(auto_servers[2],"alpha"));
    assert(server_preference_save("kiss-episode","mirror_2"));
    server_preference_read("movie",name);assert(!strcmp(name,"foxtrot"));
    assert(!server_preference_save("movie","bad/name"));
    strcpy(auto_title.kind,"kiss-episode");auto_count=2;strcpy(auto_servers[0],"other");strcpy(auto_servers[1],"mirror_2");
    auto_prefer_flixer_server();assert(!strcmp(auto_servers[0],"mirror_2"));
    strcpy(auto_title.kind,"movie");auto_count=2;strcpy(auto_servers[0],"alpha");strcpy(auto_servers[1],"echo");
    auto_prefer_flixer_server();assert(!strcmp(auto_servers[0],"echo")); /* missing preferred server retains fallback */
    unlink("settings.cfg");unlink("tv-gambits.cfg");unlink("server-preferences.cfg");
    SDL_Quit();assert(!chdir("/tmp"));assert(!rmdir(temp));
    puts("PASS: independent persistent genres/order, cancelled filters, validated settings, confirmed server learning and provider fallbacks");
}
