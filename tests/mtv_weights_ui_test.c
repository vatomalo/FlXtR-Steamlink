#define FLXTR_DESKTOP
#define main source_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void file(const char *path,const char *value){FILE *f=fopen(path,"w");assert(f);fputs(value,f);assert(!fclose(f));}
int main(void){
    char dir[]="/tmp/flxtr-mtv-weight-ui-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_TIMER));assert(!open_ui());
    settings_on=1;settings_tab=SETTINGS_MUSIC;settings_row=3;assert(settings_id()==31);
    action(SDLK_RETURN);assert(mtv_weights_on&&mtv_edit_weights[1]==1);
    action(SDLK_LEFT);assert(!mtv_edit_weights[1]);action(SDLK_LEFT);assert(!mtv_edit_weights[1]);
    action(SDLK_m);assert(mtv_edit_weights[1]==10);action(SDLK_v);assert(!mtv_edit_weights[1]);
    action(SDLK_RETURN);assert(mtv_edit_weights[1]==1);action(SDLK_RETURN);assert(!mtv_edit_weights[1]);
    for(int i=0;i<20;i++)action(SDLK_m);
    assert(mtv_edit_weights[1]==100);action(SDLK_F2);assert(mtv_edit_weights[1]==1);
    for(int i=1;i<MTV_GENRES;i++)mtv_edit_weights[i]=0;
    mtv_weights_row=13;action(SDLK_m);action(SDLK_m);action(SDLK_m);
    action(SDLK_DOWN);for(int i=0;i<7;i++)action(SDLK_m);
    assert(mtv_weights_row==14&&mtv_weights_first>1);draw(0);
    action(SDLK_ESCAPE);assert(!mtv_weights_on);
    int weights[MTV_GENRES];mtv_weights_load(weights);assert(weights[13]==30&&weights[14]==70&&weights[15]==0);
    for(unsigned i=0;i<100;i++)assert(mtv_weighted_genre(i)==(i<30?13:14));
    mtv_weights_open();assert(!mkdir("mtv-weights.tsv.next",0700));action(SDLK_ESCAPE);
    assert(mtv_weights_on&&strstr(mtv_weights_message,"SAVE FAILED"));mtv_weights_load(weights);assert(weights[14]==70);
    assert(!rmdir("mtv-weights.tsv.next"));action(SDLK_ESCAPE);assert(!mtv_weights_on);
    settings_row=4;assert(settings_id()==32);mtv_fusion_genre=0;
    int seen_custom=0;
    for(int i=0;i<15;i++){action(SDLK_RIGHT);assert(mtv_fusion_valid(mtv_fusion_genre));seen_custom|=mtv_fusion_genre==MTV_FUSION_CUSTOM;}
    assert(mtv_fusion_genre==0&&seen_custom);
    mtv_fusion_genre=14;save_settings();mtv_fusion_genre=0;load_settings();assert(mtv_fusion_genre==14);
    file("mtv-fusion-style.txt","metal\n");
    char query[221];mtv_genre=11;mtv_query(query,sizeof(query));assert(strstr(query,"city pop")&&!strstr(query,"metal"));
    mtv_genre=12;mtv_fusion_genre=13;mtv_query(query,sizeof(query));assert(strstr(query,"enka")&&strstr(query,"karaoke"));
    mtv_fusion_genre=MTV_FUSION_CUSTOM;mtv_query(query,sizeof(query));assert(strstr(query,"metal"));
    mtv_fusion_genre=0;mtv_query(query,sizeof(query));assert(!strstr(query,"metal"));
    file("settings.cfg","mtv_genre=11\n");load_settings();assert(mtv_fusion_genre==MTV_FUSION_CUSTOM);
    close_ui();SDL_Quit();
    puts("PASS: Music weight editor, range/mute/reset/scroll, durable weighted selection, save failure, fusion picker persistence and custom-file compatibility");
}
