/* In-game music player (player's request, 2026-11-20): FlXtR's R2 music
 * menu inside Secret of Mana, as the MUSIC page of the Start menu (L1/R1
 * switch between CLASSES and MUSIC). Radio tunes one of FlXtR's MTV genre
 * stations; Search types a query on an on-screen keyboard; picking a row
 * plays it; Stop gives the music back to the game's soundtrack covers.
 *
 * The page only writes one request line for the Steam Link's music bridge
 * (tools/som_music_bridge.py, JUKEBOX_REQUEST) and shows what the bridge
 * answers (JUKEBOX_LIST: status, now playing, one title per row). Pure state
 * and formatting here; file I/O and drawing glue live in the proxy.
 *
 * Page buttons: Up/Down move; B selects (Search: type, Radio: tune, Stop,
 * a row: play); Left/Right change the radio station; X skips to the next
 * track; A closes the menu. Typing: the D-pad moves on the keyboard, B
 * types the key, Y deletes, X searches, A goes back without searching. */
#ifndef FLXTR_SOM_JUKEBOX_H
#define FLXTR_SOM_JUKEBOX_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vendor/libretro.h"

#define SOM_JUKE_ROWS 60u
#define SOM_JUKE_TEXT 40u
#define SOM_JUKE_QUERY 32u
#define SOM_JUKE_VISIBLE 9u          /* result rows on the page */
#define SOM_JUKE_LINES 17u
#define SOM_JUKE_FIXED 3u            /* Search, Radio, Stop before the rows */
#define SOM_JUKE_KEY_COLS 10u
#define SOM_JUKE_STATIONS 16u
static const char som_juke_keys[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .-'";
static const char *const som_juke_stations[SOM_JUKE_STATIONS]={
    "MIX","POP","ROCK","HIP HOP","ELECTRONIC","METAL","JAZZ","COUNTRY",
    "R&B / SOUL","K-POP","ANIME","VGM REMIX","KARAOKE","ENKA","CITY POP","LATIN"};

typedef struct {
    int open,typing;
    unsigned cursor,scroll,station,key;
    char query[SOM_JUKE_QUERY+1u];
    char status[SOM_JUKE_TEXT],now[SOM_JUKE_TEXT];
    char rows[SOM_JUKE_ROWS][SOM_JUKE_TEXT];
    unsigned row_count;
} SomJukebox;

static inline void som_juke_init(SomJukebox *j){
    memset(j,0,sizeof(*j));
    j->station=6u;                                   /* JAZZ */
    snprintf(j->status,sizeof(j->status),"SEARCH OR TUNE A RADIO");
}
/* The bridge's answer: line 1 status, line 2 now playing, then rows. */
static inline void som_juke_load(SomJukebox *j,const char *text){
    unsigned line=0;j->row_count=0;
    while(text&&*text){
        const char *end=strchr(text,'\n');
        size_t len=end?(size_t)(end-text):strlen(text);
        char *dst=line==0?j->status:line==1?j->now:
                  j->row_count<SOM_JUKE_ROWS?j->rows[j->row_count]:NULL;
        if(dst){
            if(len>=SOM_JUKE_TEXT)len=SOM_JUKE_TEXT-1u;
            memcpy(dst,text,len);dst[len]=0;
            if(line>=2u)j->row_count++;
        }
        line++;
        if(!end)break;
        text=end+1;
    }
    const unsigned last=SOM_JUKE_FIXED+(j->row_count?j->row_count-1u:0u);
    if(j->cursor>last&&j->cursor>=SOM_JUKE_FIXED)j->cursor=last;
}
/* A button press. Returns 1 with a request line in req (for the bridge). */
static inline int som_juke_press(SomJukebox *j,uint16_t pressed,char *req,size_t cap){
#define SOM_JK(b) (pressed&(1u<<RETRO_DEVICE_ID_JOYPAD_##b))
    const unsigned keys=(unsigned)(sizeof(som_juke_keys)-1u);
    if(j->typing){
        if(SOM_JK(LEFT))j->key=(j->key+keys-1u)%keys;
        if(SOM_JK(RIGHT))j->key=(j->key+1u)%keys;
        if(SOM_JK(UP))j->key=(j->key+keys-SOM_JUKE_KEY_COLS)%keys;
        if(SOM_JK(DOWN))j->key=(j->key+SOM_JUKE_KEY_COLS)%keys;
        if(SOM_JK(B)){
            const size_t n=strlen(j->query);
            if(n<SOM_JUKE_QUERY){j->query[n]=som_juke_keys[j->key];j->query[n+1u]=0;}
        }
        if(SOM_JK(Y)){const size_t n=strlen(j->query);if(n)j->query[n-1u]=0;}
        if(SOM_JK(A)){j->typing=0;return 0;}
        if(SOM_JK(X)&&j->query[0]){
            j->typing=0;
            snprintf(j->status,sizeof(j->status),"SEARCHING...");
            snprintf(req,cap,"search %s",j->query);
            return 1;
        }
        return 0;
    }
    const unsigned last=SOM_JUKE_FIXED+(j->row_count?j->row_count-1u:0u);
    if(SOM_JK(UP)&&j->cursor)j->cursor--;
    if(SOM_JK(DOWN)&&j->cursor<last&&(j->cursor<SOM_JUKE_FIXED-1u||j->row_count))j->cursor++;
    if(j->cursor>=SOM_JUKE_FIXED){
        const unsigned row=j->cursor-SOM_JUKE_FIXED;
        if(row<j->scroll)j->scroll=row;
        if(row>=j->scroll+SOM_JUKE_VISIBLE)j->scroll=row-SOM_JUKE_VISIBLE+1u;
    }
    if(j->cursor==1u&&SOM_JK(LEFT))j->station=(j->station+SOM_JUKE_STATIONS-1u)%SOM_JUKE_STATIONS;
    if(j->cursor==1u&&SOM_JK(RIGHT))j->station=(j->station+1u)%SOM_JUKE_STATIONS;
    if(SOM_JK(X)){snprintf(req,cap,"next");return 1;}
    if(!SOM_JK(B))return 0;
    if(j->cursor==0u){j->typing=1;return 0;}
    if(j->cursor==1u){
        snprintf(j->status,sizeof(j->status),"TUNING %s...",som_juke_stations[j->station]);
        snprintf(req,cap,"radio %u",j->station);
        return 1;
    }
    if(j->cursor==2u){snprintf(req,cap,"stop");return 1;}
    snprintf(req,cap,"play %u",j->cursor-SOM_JUKE_FIXED);
    return 1;
#undef SOM_JK
}
/* Page line `line` (0..SOM_JUKE_LINES-1) as at most 32 characters. */
static inline int som_juke_line(const SomJukebox *j,unsigned line,char out[33]){
    if(line==0u){snprintf(out,33,"MUSIC         L/R:tab A:close");return 1;}
    if(line==1u){snprintf(out,33,"%.32s",j->now[0]?j->now:"(game soundtrack)");return 1;}
    if(line==2u){snprintf(out,33,"%.32s",j->status);return 1;}
    if(line==3u){
        snprintf(out,33,"%c SEARCH: %.20s%s",j->cursor==0u?'>':' ',j->query,j->typing?"_":"");
        return 1;
    }
    if(line==4u){
        snprintf(out,33,"%c RADIO: < %s >",j->cursor==1u?'>':' ',som_juke_stations[j->station]);
        return 1;
    }
    if(line==5u){snprintf(out,33,"%c STOP: game music",j->cursor==2u?'>':' ');return 1;}
    if(line==SOM_JUKE_LINES-1u){
        snprintf(out,33,j->typing?"B:key Y:del X:search A:back":
                                  "B:pick X:next <>:station");
        return 1;
    }
    const unsigned k=line-6u;                      /* 0..9 */
    if(j->typing){
        if(k>=4u){out[0]=0;return 1;}
        unsigned n=0;
        for(unsigned c=0;c<SOM_JUKE_KEY_COLS;c++){
            const unsigned i=k*SOM_JUKE_KEY_COLS+c;
            if(i>=sizeof(som_juke_keys)-1u)break;
            const char ch=som_juke_keys[i]==' '?'_':som_juke_keys[i];
            out[n++]=i==j->key?'[':' ';out[n++]=ch;out[n++]=i==j->key?']':' ';
        }
        out[n]=0;
        return 1;
    }
    if(k>=SOM_JUKE_VISIBLE){out[0]=0;return 1;}
    const unsigned row=j->scroll+k;
    if(row>=j->row_count){out[0]=0;return 1;}
    snprintf(out,33,"%c%.31s",j->cursor==SOM_JUKE_FIXED+row?'>':' ',j->rows[row]);
    return 1;
}
#endif
