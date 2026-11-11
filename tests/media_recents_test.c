#define main flxtr_shell_test_main
#include "../src/shell.c"
#undef main
#include <assert.h>
int main(void){
    char root[]="/tmp/flxtr-media-recents-XXXXXX";
    assert(mkdtemp(root));
    assert(!chdir(root));
    assert(!SDL_Init(SDL_INIT_TIMER));
    Title movie={0},anime={0},rom={0},pc={0},series={0};
    strcpy(movie.kind,"movie");strcpy(movie.title,"A Movie");movie.id=10;
    strcpy(series.kind,"tv");strcpy(series.title,"A Series");series.id=12;
    strcpy(anime.kind,"kiss-episode");strcpy(anime.title,"Anime Episode");
    anime.id=30;anime.season=1;anime.episode=4;
    strcpy(rom.kind,"game");strcpy(rom.title,"Retro Game");
    strcpy(rom.url,"romid:0123456789abcdef");rom.id=42;
    strcpy(pc.kind,"moonlight-game");strcpy(pc.title,"PC Game");
    strcpy(pc.url,"gamez:0123456789abcdef0123456789abcdef");
    media_recents_record(&movie);
    media_recents_record(&series);
    media_recents_record(&anime);
    media_recents_record(&rom);
    media_recents_record(&pc);
    media_recents_record(&movie);
    media_recents_load();
    assert(media_recent_count==5&&!strcmp(media_recent_entries[0].title,"A Movie"));
    Browse screen={MEDIA_RECENTS_MODE,1,0,0,0,0,"",""};
    media_recents_open(screen);
    assert(browse.mode==MEDIA_RECENTS_MODE&&total==5);
    assert(!strcmp(titles[visible[0]].title,"A Movie"));
    int seen_pc=0,seen_rom=0,seen_anime=0;
    for(int i=0;i<total;i++){
        Title *t=&titles[visible[i]];
        if(!strcmp(t->kind,"moonlight-game"))seen_pc=1;
        if(!strcmp(t->kind,"game"))seen_rom=1;
        if(!strcmp(t->kind,"kiss-episode"))seen_anime=1;
    }
    assert(seen_pc&&seen_rom&&seen_anime);
    assert(access("music-cache/recents.tsv",F_OK));
    Title music={0};strcpy(music.kind,"source");
    strcpy(music.title,"A Song");strcpy(music.url,"https://music.example/track");
    media_recents_record(&music);
    media_recents_load();assert(media_recent_count==5);
    SDL_Quit();
    puts("PASS: persistent mixed-media Recents, deduplication and independent music history");
    return 0;
}
