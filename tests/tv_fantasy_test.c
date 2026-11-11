#include <assert.h>
#include <stdio.h>
#include "../src/tv_schedule.h"
#include "../src/tv_fantasy.h"
int main(void){
    assert(TV_FANTASY==17&&TV_MUSIC_VIDEOS==16&&TV_GENRES==18);
    assert(!strcmp(tv_genres[TV_FANTASY],"FANTASY"));
    TvBlock blocks[TV_BLOCKS];
    memcpy(blocks,tv_defaults,sizeof(blocks));
    blocks[4].genre=TV_FANTASY;
    assert(tv_valid(blocks));
    blocks[4].genre=TV_GENRES;assert(!tv_valid(blocks));
    assert(tv_fantasy_score("The Dragon Realm","A wizard must break a magical curse")>0);
    assert(tv_fantasy_score("Witches","An enchanted kingdom and a prophecy")>0);
    assert(tv_fantasy_score("Spaceship Crew","Astronauts and robots explore a galaxy")<=0);
    assert(tv_fantasy_score("Magic in Space","A wizard travels across a galaxy")>0);
    puts("PASS: Fantasy remains appended after existing genre IDs and ranks fantasy-themed shows");
    return 0;
}
