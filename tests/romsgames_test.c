#define main catalog_main
#include "../src/catalog.c"
#undef main
#include <assert.h>
int main(void){
    assert(rg_path("/super-nintendo-rom-game/"));
    assert(!rg_path("//evil-rom-game/"));assert(!rg_path("/snes-rom-../"));
    assert(!rg_path("https://elsewhere/nes-rom-game/"));
    char html[]="<a href='/snes-rom-first/'><img src='//cache.downloadroms.io/a.jpg' alt='First &#x27;game&#x27;'></a>"
      "<a href='https://evil.invalid/nes-rom-evil/'><img alt='Bad'></a>"
      "<a href='/snes-rom-second/'><img src='https://evil.invalid/a.jpg' alt='Second'></a>";
    assert(rg_cards(html,0)==2&&used==2);
    assert(!strcmp(entries[0].title,"First 'game'"));
    assert(!strcmp(entries[0].poster,"https://cache.downloadroms.io/a.jpg"));
    assert(!*entries[1].poster);
    used=0;memset(entries,0,sizeof(entries));assert(rg_cards(html,1)==2&&used==1);
    assert(!strcmp(entries[0].title,"Second"));
    used=0;assert(!rg_platforms(2)&&used==4&&total==10);
    puts("RomsGames parser and URL checks passed");return 0;
}
