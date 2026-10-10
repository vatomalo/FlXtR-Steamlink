#define main catalog_main
#include "../src/catalog.c"
#undef main
#include <assert.h>
int main(void){
    char dir[]="/tmp/flxtr-roms-XXXXXX";assert(mkdtemp(dir));assert(!chdir(dir));
    char target[400],other[400],path[2048],url[2048];
    assert(game_rom_target("https://archive.org/download/item/lastblad.zip",6,target,sizeof(target)));
    assert(!strcmp(strrchr(target,'/')+1,"lastblad.zip"));
    char longname[360];memset(longname,'A',350);strcpy(longname+350,".sfc");
    snprintf(url,sizeof(url),"https://archive.org/download/long-collection-name/%s",longname);
    assert(game_rom_target(url,2,target,sizeof(target)));assert(strlen(strrchr(target,'/')+1)<192);assert(strstr(target,".sfc"));
    longname[300]='B';snprintf(url,sizeof(url),"https://archive.org/download/long-collection-name/%s",longname);
    assert(game_rom_target(url,2,other,sizeof(other)));assert(strcmp(target,other));
    assert(game_rom_target("https://archive.org/download/disc/pack.zip/game%2Ftrack.bin",4,target,sizeof(target)));
    assert(game_rom_target("https://archive.org/download/disc/pack.zip/game%2Fdisc.cue",4,other,sizeof(other)));
    *strrchr(target,'/')=0;*strrchr(other,'/')=0;assert(!strcmp(target,other));
    assert(!game_archive_path("https://archive.org/download/item/%2e%2e/evil.zip",path,sizeof(path)));
    assert(!game_archive_path("https://archive.org.evil/download/item/a.zip",path,sizeof(path)));
    assert(!game_archive_path("https://archive.org/download/item/a%00.zip",path,sizeof(path)));
    const char *html="<a href=\"//archive.org/download/item/pack.zip/Nested%2FGame.sfc\">Game</a>"
      "<a href=\"https://evil.org/Game.sfc\">bad</a>"
      "<a href=\"//archive.org/download/item/pack.zip/readme.txt\">readme</a>"
      "<a href=\"//archive.org/download/item/pack.zip/%2e%2e%2Fa.sfc\">bad</a>";
    used=total=0;assert(!games_zip_parse(html,"https://archive.org/download/item/pack.zip",1,2));
    assert(total==2&&used==2&&!strcmp(entries[0].title,"DOWNLOAD WHOLE ZIP"));
    assert(!strcmp(entries[1].title,"Game.sfc")&&strstr(entries[1].url,"Nested%2FGame.sfc"));
    puts("PASS: long names, collision suffix, arcade basename, disc siblings, ZIP member links and traversal rejection");
    return 0;
}
