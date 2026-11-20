/* The Start menu's MUSIC page: navigation, keyboard, requests, answers. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/som_jukebox.h"

#define P(b) ((uint16_t)(1u<<RETRO_DEVICE_ID_JOYPAD_##b))

int main(void){
    SomJukebox j;som_juke_init(&j);
    char req[160],line[33];
    /* Radio: down to it, change the station, tune. */
    assert(!som_juke_press(&j,P(DOWN),req,sizeof(req))&&j.cursor==1u);
    assert(!som_juke_press(&j,P(RIGHT),req,sizeof(req))&&j.station==7u);
    assert(!som_juke_press(&j,P(LEFT),req,sizeof(req))&&j.station==6u);
    assert(som_juke_press(&j,P(B),req,sizeof(req))&&!strcmp(req,"radio 6"));
    som_juke_line(&j,4,line);assert(strstr(line,"RADIO: < JAZZ >")&&line[0]=='>');
    /* Search: type "AB", delete, type, search. */
    assert(!som_juke_press(&j,P(UP),req,sizeof(req))&&j.cursor==0u);
    assert(!som_juke_press(&j,P(B),req,sizeof(req))&&j.typing);
    assert(!som_juke_press(&j,P(B),req,sizeof(req)));                 /* A */
    assert(!som_juke_press(&j,P(RIGHT),req,sizeof(req)));
    assert(!som_juke_press(&j,P(B),req,sizeof(req))&&!strcmp(j.query,"AB"));
    assert(!som_juke_press(&j,P(Y),req,sizeof(req))&&!strcmp(j.query,"A"));
    assert(!som_juke_press(&j,P(DOWN),req,sizeof(req))&&j.key==11u);  /* L */
    assert(!som_juke_press(&j,P(B),req,sizeof(req))&&!strcmp(j.query,"AL"));
    som_juke_line(&j,7,line);assert(strstr(line,"[L]"));
    assert(som_juke_press(&j,P(X),req,sizeof(req))&&!strcmp(req,"search AL")&&!j.typing);
    /* The bridge's answer: status, now playing, rows; pick row 1. */
    som_juke_load(&j,"SEARCH: AL\n> Song zero\nSong zero - Band\nSong one\n");
    assert(j.row_count==2u&&!strcmp(j.now,"> Song zero"));
    som_juke_line(&j,1,line);assert(!strcmp(line,"> Song zero"));
    som_juke_line(&j,6,line);assert(!strcmp(line," Song zero - Band"));
    for(int i=0;i<4;i++)som_juke_press(&j,P(DOWN),req,sizeof(req));
    assert(j.cursor==4u);                                            /* the last row */
    assert(som_juke_press(&j,P(B),req,sizeof(req))&&!strcmp(req,"play 1"));
    assert(som_juke_press(&j,P(X),req,sizeof(req))&&!strcmp(req,"next"));
    j.cursor=2u;assert(som_juke_press(&j,P(B),req,sizeof(req))&&!strcmp(req,"stop"));
    /* A long list scrolls with the cursor. */
    char text[4096];size_t n=(size_t)snprintf(text,sizeof(text),"ok\n\n");
    for(int i=0;i<20;i++)n+=(size_t)snprintf(text+n,sizeof(text)-n,"Row %d\n",i);
    som_juke_load(&j,text);
    j.cursor=2u;for(int i=0;i<15;i++)som_juke_press(&j,P(DOWN),req,sizeof(req));
    assert(j.cursor==SOM_JUKE_FIXED+14u&&j.scroll==6u);
    som_juke_line(&j,6+8,line);assert(!strcmp(line,">Row 14"));
    puts("som jukebox: ok");
    return 0;
}
