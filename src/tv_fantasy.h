/* Fantasy-specific ranking for TMDB's combined Sci-Fi & Fantasy TV genre.
 * Only series with recognizable fantasy descriptors are eligible. */
#ifndef FLXTR_TV_FANTASY_H
#define FLXTR_TV_FANTASY_H
#include <stddef.h>
#include <string.h>
#include <ctype.h>
static int tv_fantasy_contains(const char *text,const char *term){
    if(!text||!term||!*term)return 0;
    size_t len=strlen(term);
    for(const unsigned char *p=(const unsigned char*)text;*p;p++){
        size_t i=0;
        while(i<len&&p[i]&&tolower(p[i])==(unsigned char)term[i])i++;
        if(i==len)return 1;
    }
    return 0;
}
static int tv_fantasy_score(const char *title,const char *overview){
    static const char *const clues[]={
        "magic","magical","wizard","witch","sorcer","spell","dragon",
        "kingdom","enchant","fairy","fairies","faerie","elf","elves",
        "demon","vampire","werewolf","supernatural","myth","legend",
        "prophe","curse","deity","gods","goddess","fantasy",
        "knight","medieval","realm","immortal","alchemy","witchcraft"
    };
    static const char *const scifi[]={
        "spaceship","spacecraft","alien invasion","extraterrestrial",
        "robot","android","cyborg","interstellar","galactic",
        "space station","astronaut","time travel","futuristic"
    };
    int score=0;
    for(size_t i=0;i<sizeof(clues)/sizeof(clues[0]);i++){
        if(tv_fantasy_contains(title,clues[i]))score+=3;
        if(tv_fantasy_contains(overview,clues[i]))score+=2;
    }
    for(size_t i=0;i<sizeof(scifi)/sizeof(scifi[0]);i++)
        if(tv_fantasy_contains(title,scifi[i])||tv_fantasy_contains(overview,scifi[i]))score-=2;
    return score;
}
#endif
