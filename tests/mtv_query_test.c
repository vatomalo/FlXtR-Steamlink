#define FLXTR_DESKTOP
#define main source_main
#include "../src/shell.c"
#undef main
#include <assert.h>
static void only_style(int genre){
    FILE *f=fopen("mtv-weights.tsv","w");assert(f);
    for(int i=1;i<MTV_GENRES;i++)fprintf(f,"%d %d\n",i,i==genre?100:0);
    assert(!fclose(f));
}
int main(void){
    char dir[]="/tmp/flxtr-mtv-query-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    settings_on=1;settings_tab=SETTINGS_MUSIC;settings_row=2;assert(settings_id()==29);
    for(int i=1;i<=MTV_GENRES;i++){
        action(SDLK_RIGHT);assert(mtv_genre==i%MTV_GENRES);
        save_settings();mtv_genre=-1;load_settings();assert(mtv_genre==i%MTV_GENRES);
    }
    action(SDLK_LEFT);assert(mtv_genre==MTV_GENRES-1);
    const char *styles[]={"enka","city pop","latin"};
    char query[221];
    for(int i=13;i<MTV_GENRES;i++){
        only_style(i);
        mtv_genre=0;mtv_query(query,sizeof(query));assert(!strcmp(query,mtv_genre_queries[i]));
        mtv_genre=i;mtv_query(query,sizeof(query));assert(!strcmp(query,mtv_genre_queries[i]));
        mtv_genre=11;mtv_query(query,sizeof(query));assert(strstr(query,styles[i-13])&&strstr(query,"remix music video"));
        mtv_genre=12;mtv_query(query,sizeof(query));assert(strstr(query,styles[i-13])&&strstr(query,"karaoke instrumental backing track"));
    }
    only_style(0);mtv_genre=11;mtv_query(query,sizeof(query));assert(strstr(query,"city pop"));
    FILE *f=fopen("mtv-query.txt","w");assert(f);fputs("User custom query\n",f);fclose(f);
    mtv_genre=0;mtv_query(query,sizeof(query));assert(!strcmp(query,"User custom query"));
    mtv_genre=13;mtv_query(query,sizeof(query));assert(!strcmp(query,mtv_genre_queries[13]));
    puts("PASS: all 16 settings cycle/persist; Enka, City Pop, Latin select directly and fuse into VGA/Karaoke; custom query precedence");
}
