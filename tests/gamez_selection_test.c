#define main shell_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    char directory[]="/tmp/flxtr-gamez-selection-XXXXXX";
    assert(mkdtemp(directory));assert(!chdir(directory));
    Title game={.kind="moonlight-game",.title="Approved PC game",.url="gamez:5E4132C860DED717ED0FEE86"};
    FILE *catalog=fopen("catalog.tsv","w");assert(catalog);
    assert(fprintf(catalog,"# pages=1 total=1\nApproved PC game\tAPPROVED\tgame-art-cache/test.bmp\t%s\tmoonlight-game\t0\t0\t0\n"
        "Invalid\tAPPROVED\t\tgamez:bad;command\tmoonlight-game\t0\t0\t0\n"
        "Wrong type\tAPPROVED\t\tgamez:ABC\tmovie\t0\t0\t0\n",game.url)>0);
    assert(!fclose(catalog));assert(read_catalog("catalog.tsv")==1);
    assert(total==1&&!strcmp(titles[visible[0]].url,game.url));
    Browse live={.mode=22,.page=1,.id=11};library_cache_store(&live);
    assert(!library_cache_restore(live,0,0));
    browse=live;action(SDLK_y);assert(gamez_options_on&&!gamez_rename_on&&!search_on&&!filters_on);
    action(SDLK_RETURN);assert(!gamez_options_on&&gamez_rename_on&&search_on);
    assert(!strcmp(gamez_rename_id,"5E4132C860DED717ED0FEE86"));
    action(SDLK_ESCAPE);assert(!gamez_rename_on&&!search_on);
    action(SDLK_F3);assert(gamez_options_on);
    action(SDLK_DOWN);action(SDLK_RETURN);assert(gamez_unapprove_confirm&&!gamez_options_on);
    assert(!strcmp(gamez_unapprove_id,"5E4132C860DED717ED0FEE86"));
    action(SDLK_ESCAPE);assert(!gamez_unapprove_confirm&&!gamez_unapprove_pending&&!catalog_pid);
    action(SDLK_F3);action(SDLK_DOWN);action(SDLK_DOWN);action(SDLK_RETURN);assert(filters_on);
    action(SDLK_RIGHT);assert(browse.id==11&&browse.episode==1);
    action(SDLK_DOWN);action(SDLK_RIGHT);assert(browse.season==1);
    action(SDLK_ESCAPE);assert(!filters_on&&browse.episode==0&&browse.season==0&&browse.id==11);
    action(SDLK_F3);action(SDLK_DOWN);action(SDLK_DOWN);action(SDLK_RETURN);
    action(SDLK_RIGHT);action(SDLK_DOWN);action(SDLK_RIGHT);action(SDLK_RETURN);
    assert(!filters_on&&gamez_genre==1&&gamez_order==1);
    gamez_genre=gamez_order=0;load_settings();assert(gamez_genre==1&&gamez_order==1);
    browse=(Browse){0};
    running=1;moonlight_exit=0;play();
    assert(!running&&moonlight_exit);
    FILE *file=fopen("gamez-request","r");char id[64]="";
    assert(file&&fgets(id,sizeof(id),file));assert(!fclose(file));
    assert(!strcmp(id,"5E4132C860DED717ED0FEE86\n"));assert(!unlink("gamez-request"));
    const char *bad[]={"","gamez:","gamez:bad;command","gamez:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        snprintf(game.url,sizeof(game.url),"%s",bad[i]);
        titles[0]=game;running=1;moonlight_exit=0;play();
        assert(running&&!moonlight_exit&&access("gamez-request",F_OK));
    }
    DIR *files=opendir(".");assert(files);struct dirent *entry;
    while((entry=readdir(files)))if(entry->d_name[0]!='.')assert(!unlink(entry->d_name));
    closedir(files);assert(!chdir("/tmp"));assert(!rmdir(directory));
    puts("PASS: full companion game ID survives UI selection; old and invalid links cannot launch");
    return 0;
}
