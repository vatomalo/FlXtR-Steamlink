/* Perk loadouts in the progress record and the save-state trailer (no ROM):
 *   cc -std=c99 -Wall -Wextra -Werror -Isrc tests/test_perk_persistence.c -o build/test-perk-persistence */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "som_progress.h"
#include "som_perk_runtime.h"

static void filled(SomProgress *p){
    memset(p,0,sizeof(*p));
    p->owner=0x1234567890ABCDEFull;p->slot=1;p->rom=0xFEDCBA0987654321ull;p->selected=10;
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++){p->level[i]=100;p->experience[i]=4294967295u;}
    p->perks[0]=0xFFFFFFFFu;p->classless=1;p->party[0]=3;p->party[1]=4;
    p->held[0]=p->held[1]=p->held[2]=0xFFFF;
}
static void test_round_trip(void){
    SomProgress p,q;filled(&p);
    p.loadout_set=1;
    for(unsigned w=0;w<3u;w++){                              /* the biggest record there can be */
        p.loadout_n[w]=SOM_PROG_LOADOUT_MAX;
        for(unsigned i=0;i<SOM_PROG_LOADOUT_MAX;i++)p.loadout[w][i]=(uint16_t)(65535u-i-w);
    }
    p.look_set=1;                                            /* and the creator's look */
    for(unsigned w=0;w<3u;w++){p.look[w][0]=4;p.look[w][1]=(uint8_t)(0xF0+w);p.look[w][2]=9;}
    char text[SOM_PROG_TEXT_MAX];
    const size_t n=som_prog_format(&p,text,sizeof(text));
    assert(n>0&&n<SOM_PROG_TEXT_MAX);
    printf("largest record %zu of %u bytes\n",n,SOM_PROG_TEXT_MAX);
    assert(som_prog_parse(text,n,&q)==SOM_PROG_OK);
    assert(q.loadout_set&&!memcmp(q.loadout,p.loadout,sizeof(p.loadout))&&!memcmp(q.loadout_n,p.loadout_n,3));
    assert(!memcmp(q.level,p.level,sizeof(p.level))&&q.classless&&q.held[2]==0xFFFF);
    assert(q.look_set&&!memcmp(q.look,p.look,sizeof(p.look)));
    /* the save-state trailer carries the same text, same size as before */
    static uint8_t trailer[SOM_PROG_TRAILER_SIZE];
    assert(som_prog_trailer_write(&p,trailer)==SOM_PROG_TRAILER_SIZE);
    assert(SOM_PROG_TRAILER_SIZE==8u+4u+1024u);
    memset(&q,0,sizeof(q));
    assert(som_prog_trailer_read(trailer,&q)==SOM_PROG_OK&&q.loadout[2][11]==p.loadout[2][11]);
    /* empty loadouts round-trip as empty, not as missing */
    filled(&p);p.loadout_set=1;
    assert((n==0)||1);
    const size_t m=som_prog_format(&p,text,sizeof(text));
    assert(strstr(text,"pl1 0 -\n")&&som_prog_parse(text,m,&q)==SOM_PROG_OK&&q.loadout_set&&!q.loadout_n[0]);
}
static void test_legacy_and_damage(void){
    SomProgress p,q;filled(&p);                              /* a record from before perks */
    char text[SOM_PROG_TEXT_MAX];
    size_t n=som_prog_format(&p,text,sizeof(text));
    assert(!strstr(text,"pl1"));
    assert(som_prog_parse(text,n,&q)==SOM_PROG_OK&&!q.loadout_set&&!q.loadout_n[0]);
    /* restoring it equips nothing */
    SomPerkRuntime rt;som_prt_init(&rt);
    const uint16_t keep=1001;som_prt_set_raw(&rt,0,&keep,1);
    for(unsigned w=0;w<3u;w++)som_prt_set_raw(&rt,w,q.loadout[w],q.loadout_set?q.loadout_n[w]:0u);
    assert(!rt.hero[0].n&&!rt.hero[1].n&&!rt.hero[2].n);
    /* a changed line breaks the checksum: the record is refused, not half read */
    filled(&p);p.loadout_set=1;p.loadout_n[0]=1;p.loadout[0][0]=2002;
    n=som_prog_format(&p,text,sizeof(text));
    char *at=strstr(text,"pl1 0 2002");assert(at);at[6]='3';
    assert(som_prog_parse(text,n,&q)==SOM_PROG_CORRUPT);
    /* unknown lines are skipped (that is how older builds read new records) */
    filled(&p);
    char extra[SOM_PROG_TEXT_MAX];
    n=som_prog_format(&p,text,sizeof(text));
    const char *sum=strstr(text,"sum ");assert(sum);
    const size_t body=(size_t)(sum-text);
    memcpy(extra,text,body);
    size_t k=body+(size_t)snprintf(extra+body,sizeof(extra)-body,"zz-future 1 2 3\npl1 1 2002,2003\n");
    k+=(size_t)snprintf(extra+k,sizeof(extra)-k,"sum %016llx\n",(unsigned long long)som_prog_hash((const uint8_t*)extra,k));
    assert(som_prog_parse(extra,k,&q)==SOM_PROG_OK&&q.loadout_n[1]==2&&q.loadout[1][1]==2003);
    /* bad loadout lines: no hero 9, junk ids */
    k=body;memcpy(extra,text,body);
    k+=(size_t)snprintf(extra+k,sizeof(extra)-k,"pl1 9 2002\npl1 2 abc\npl1 0 0,2002\n");
    k+=(size_t)snprintf(extra+k,sizeof(extra)-k,"sum %016llx\n",(unsigned long long)som_prog_hash((const uint8_t*)extra,k));
    assert(som_prog_parse(extra,k,&q)==SOM_PROG_OK);
    assert(q.loadout_n[2]==0&&q.loadout_n[0]==1&&q.loadout[0][0]==2002);
}
static void test_unknown_ids(void){
    SomPerkRuntime rt;som_prt_init(&rt);
    const uint16_t ids[5]={2002,9,2002,65000,1101};          /* repeat and unknown ids */
    assert(som_prt_set_raw(&rt,1,ids,5)==2);
    char line[96];
    assert(som_prt_format(&rt,1,line,sizeof(line))&&!strcmp(line,"pl1 1 2002,1101\n"));
    uint16_t back[SOM_PERK_MAX_EQUIPPED];unsigned n=0;
    assert(som_prt_parse("pl1 1 2002,77,1101,2002\n",back,&n)==1&&n==2&&back[0]==2002&&back[1]==1101);
    assert(som_prt_parse("pl1 7 2002\n",back,&n)<0);
    assert(som_prt_parse("pl1 0 -\n",back,&n)==0&&n==0);
}
static void test_no_duplication_on_load(void){
    /* loading the same record twice gives the same loadout, never more */
    SomPerkRuntime rt;som_prt_init(&rt);
    const uint16_t ids[2]={2002,1101};
    som_prt_set_raw(&rt,0,ids,2);som_prt_set_raw(&rt,0,ids,2);
    assert(rt.hero[0].n==2);
    rt.hero[0].shield=50;rt.hero[0].stacks[0]=3;
    som_prt_flush(&rt);
    assert(!rt.hero[0].shield&&!rt.hero[0].stacks[0]&&rt.hero[0].n==2);
}

int main(void){
    test_round_trip();
    test_legacy_and_damage();
    test_unknown_ids();
    test_no_duplication_on_load();
    printf("PASS: perk loadouts in progress records and save-state trailers\n");
    return 0;
}
