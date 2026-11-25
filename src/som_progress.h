/* Discipline progression owned by a save, not by the device (format v2).
 *
 * Secret of Mana Turbo keeps four save slots of 0x400 bytes at SRAM 0x0000
 * (with a mirror at 0x1000). A slot holds a copy of WRAM: slot + 0x004..
 * = 7E:CC01.. (party/inventory state), slot + 0x104.. = the hero records
 * (measured on the player's SRAM and the loaded game, 2026-11-15). So right
 * after a load, or right after a save, 7E:CC01..CC40 equals one slot.
 *
 * Identity: the FNV-1a 64 fingerprint of the whole slot block. A save the
 * game writes gets a new fingerprint; the progression is then stored under
 * it. Loading a slot restores the record of that exact block, so an
 * overwritten or foreign slot never inherits another playthrough's XP.
 * Records are small text files with a checksum; this header only builds and
 * parses them (pure, no I/O). The save-state trailer carries the same text
 * so resuming a state resumes its progression. */
#ifndef FLXTR_SOM_PROGRESS_H
#define FLXTR_SOM_PROGRESS_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "som_disciplines.h"

#define SOM_PROG_VERSION 2u
#define SOM_PROG_SLOTS 4u
#define SOM_PROG_SLOT_SIZE 0x400u
#define SOM_PROG_MATCH_BYTES 0x40u       /* SRAM slot+4 vs WRAM 7E:CC01 */
#define SOM_PROG_TEXT_MAX 1024u
#define SOM_PROG_TRAILER_MAGIC "SOMPROG2"
#define SOM_PROG_V1_CLASSES 12u     /* classes a record had before 2026-11-20 */

typedef struct {
    uint64_t owner;          /* fingerprint of the save block, 0 = none yet */
    unsigned slot;           /* 0..3, SOM_PROG_SLOTS = unknown */
    uint64_t rom;            /* fingerprint of the ROM it was made with */
    uint8_t selected;
    uint16_t level[SOM_DISCIPLINE_COUNT];
    uint32_t experience[SOM_DISCIPLINE_COUNT];
    uint32_t perks[4];       /* reserved for unlocks (bit sets) */
    uint8_t classless,party[2];   /* 0 in records from before classless games */
    uint16_t held[3];
} SomProgress;

static inline uint64_t som_prog_hash(const uint8_t *data,size_t size){
    uint64_t h=1469598103934665603ull;
    for(size_t i=0;i<size;i++){h^=data[i];h*=1099511628211ull;}
    return h?h:1u;
}
static inline int som_prog_slot_used(const uint8_t *sram,unsigned slot){
    const uint8_t *b=sram+slot*SOM_PROG_SLOT_SIZE;
    for(unsigned i=4;i<SOM_PROG_SLOT_SIZE;i++)if(b[i]&&b[i]!=0xFFu)return 1;
    return 0;
}
static inline uint64_t som_prog_slot_fingerprint(const uint8_t *sram,unsigned slot){
    return som_prog_hash(sram+slot*SOM_PROG_SLOT_SIZE,SOM_PROG_SLOT_SIZE);
}
/* The used slot whose state the game holds right now, or -1. */
static inline int som_prog_slot_in_play(const uint8_t *wram,const uint8_t *sram,size_t sram_size){
    if(!wram||!sram||sram_size<SOM_PROG_SLOTS*SOM_PROG_SLOT_SIZE)return -1;
    for(unsigned k=0;k<SOM_PROG_SLOTS;k++)
        if(som_prog_slot_used(sram,k)&&
           !memcmp(sram+k*SOM_PROG_SLOT_SIZE+4u,wram+0xCC01u,SOM_PROG_MATCH_BYTES))return (int)k;
    return -1;
}
static inline void som_prog_fresh(SomProgress *p,uint64_t rom){
    memset(p,0,sizeof(*p));
    p->slot=SOM_PROG_SLOTS;p->rom=rom;p->selected=SOM_DISCIPLINE_NONE;
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++)p->level[i]=1u;
}
static inline void som_prog_from_disc(SomProgress *p,const SomDisciplines *d){
    p->selected=d->selected;
    p->classless=d->classless;memcpy(p->party,d->party,sizeof(p->party));
    memcpy(p->held,d->held,sizeof(p->held));
    memcpy(p->level,d->level,sizeof(p->level));
    memcpy(p->experience,d->experience,sizeof(p->experience));
}
static inline void som_prog_to_disc(const SomProgress *p,SomDisciplines *d){
    d->selected=p->selected;
    d->classless=p->classless;
    memcpy(d->held,p->held,sizeof(d->held));memset(d->can,0,sizeof(d->can));
    if(p->classless)memcpy(d->party,p->party,sizeof(d->party));
    memcpy(d->level,p->level,sizeof(d->level));
    memcpy(d->experience,p->experience,sizeof(d->experience));
    memset(d->last_event,0,sizeof(d->last_event));
}
/* A new game in the classless rules: nobody has a class. */
static inline void som_prog_classless(SomProgress *p){
    p->classless=1;p->selected=SOM_DISCIPLINE_NONE;
    p->party[0]=p->party[1]=SOM_DISCIPLINE_NONE;
    memset(p->held,0,sizeof(p->held));
}
/* Text form; the last line is "sum <fnv of everything before it>". Returns
 * the length written, 0 if it did not fit. */
static inline size_t som_prog_format(const SomProgress *p,char *out,size_t cap){
    size_t n=0;int w;
    w=snprintf(out,cap,"som-progress %u\nsave %016llx\nslot %u\nrom %016llx\nselected %u\n",
               SOM_PROG_VERSION,(unsigned long long)p->owner,p->slot,
               (unsigned long long)p->rom,(unsigned)p->selected);
    if(w<0||(size_t)w>=cap)return 0;
    n=(size_t)w;
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++){
        w=snprintf(out+n,cap-n,"d %u %u %lu\n",i,(unsigned)p->level[i],(unsigned long)p->experience[i]);
        if(w<0||(size_t)w>=cap-n)return 0;
        n+=(size_t)w;
    }
    w=snprintf(out+n,cap-n,"perks %08lx %08lx %08lx %08lx\n",(unsigned long)p->perks[0],
               (unsigned long)p->perks[1],(unsigned long)p->perks[2],(unsigned long)p->perks[3]);
    if(w<0||(size_t)w>=cap-n)return 0;
    n+=(size_t)w;
    if(p->classless){
        w=snprintf(out+n,cap-n,"classless %04x %04x %04x %u %u\n",(unsigned)p->held[0],
                   (unsigned)p->held[1],(unsigned)p->held[2],(unsigned)p->party[0],(unsigned)p->party[1]);
        if(w<0||(size_t)w>=cap-n)return 0;
        n+=(size_t)w;
    }
    const uint64_t sum=som_prog_hash((const uint8_t*)out,n);
    w=snprintf(out+n,cap-n,"sum %016llx\n",(unsigned long long)sum);
    if(w<0||(size_t)w>=cap-n)return 0;
    return n+(size_t)w;
}
enum {SOM_PROG_OK=0,SOM_PROG_CORRUPT=1,SOM_PROG_UNKNOWN_VERSION=2};
/* Parse a record: checksum, version, ranges. On anything but SOM_PROG_OK
 * *p is left unchanged. */
static inline int som_prog_parse(const char *text,size_t len,SomProgress *p){
    unsigned version=0;
    if(sscanf(text,"som-progress %u",&version)!=1)return SOM_PROG_CORRUPT;
    if(version!=SOM_PROG_VERSION)return SOM_PROG_UNKNOWN_VERSION;
    const char *sum_line=NULL;
    for(size_t i=0;i+4<len;i++)if(text[i]=='\n'&&!strncmp(text+i+1,"sum ",4))sum_line=text+i+1;
    unsigned long long want=0;
    if(!sum_line||sscanf(sum_line,"sum %llx",&want)!=1||
       som_prog_hash((const uint8_t*)text,(size_t)(sum_line-text))!=want)return SOM_PROG_CORRUPT;
    SomProgress q;memset(&q,0,sizeof(q));
    unsigned long long owner=0,rom=0;unsigned slot=0,selected=0;
    const char *line=text;int seen=0;
    while(line&&line<sum_line){
        unsigned i=0,level=0,h[3],g=0,q2=0;unsigned long xp=0,k[4];
        if(sscanf(line,"save %llx",&owner)==1)seen|=1;
        else if(sscanf(line,"slot %u",&slot)==1)seen|=2;
        else if(sscanf(line,"rom %llx",&rom)==1)seen|=4;
        else if(sscanf(line,"classless %x %x %x %u %u",&h[0],&h[1],&h[2],&g,&q2)==5){
            if((g>=SOM_DISCIPLINE_COUNT&&g!=SOM_DISCIPLINE_NONE)||
               (q2>=SOM_DISCIPLINE_COUNT&&q2!=SOM_DISCIPLINE_NONE))return SOM_PROG_CORRUPT;
            q.classless=1;q.party[0]=(uint8_t)g;q.party[1]=(uint8_t)q2;
            for(unsigned j=0;j<3u;j++)q.held[j]=(uint16_t)h[j];
        }
        else if(sscanf(line,"selected %u",&selected)==1)seen|=8;
        else if(sscanf(line,"d %u %u %lu",&i,&level,&xp)==3){
            if(i>=SOM_DISCIPLINE_COUNT||level<1u||level>100u)return SOM_PROG_CORRUPT;
            q.level[i]=(uint16_t)level;q.experience[i]=(uint32_t)xp;seen|=16<<0;
        }else if(sscanf(line,"perks %lx %lx %lx %lx",&k[0],&k[1],&k[2],&k[3])==4){
            for(unsigned j=0;j<4u;j++)q.perks[j]=(uint32_t)k[j];
        }
        line=strchr(line,'\n');if(line)line++;
    }
    if((seen&31)!=31||slot>SOM_PROG_SLOTS||
       (selected>=SOM_DISCIPLINE_COUNT&&selected!=SOM_DISCIPLINE_NONE))return SOM_PROG_CORRUPT;
    /* Records from before a class was added lack it: it starts at level 1. */
    for(unsigned i=0;i<SOM_DISCIPLINE_COUNT;i++)
        if(!q.level[i]){
            if(i<SOM_PROG_V1_CLASSES)return SOM_PROG_CORRUPT;
            q.level[i]=1u;
        }
    q.owner=owner;q.slot=slot;q.rom=rom;q.selected=(uint8_t)selected;
    *p=q;
    return SOM_PROG_OK;
}
/* Save-state trailer: MAGIC, 4-byte little-endian text length, text. */
#define SOM_PROG_TRAILER_SIZE (8u+4u+SOM_PROG_TEXT_MAX)
static inline size_t som_prog_trailer_write(const SomProgress *p,uint8_t *out){
    memset(out,0,SOM_PROG_TRAILER_SIZE);
    memcpy(out,SOM_PROG_TRAILER_MAGIC,8);
    const size_t n=som_prog_format(p,(char*)out+12,SOM_PROG_TEXT_MAX);
    out[8]=(uint8_t)n;out[9]=(uint8_t)(n>>8);out[10]=out[11]=0;
    return n?SOM_PROG_TRAILER_SIZE:0;
}
static inline int som_prog_trailer_read(const uint8_t *in,SomProgress *p){
    if(memcmp(in,SOM_PROG_TRAILER_MAGIC,8))return SOM_PROG_CORRUPT;
    const size_t n=(size_t)(in[8]|in[9]<<8);
    if(!n||n>SOM_PROG_TEXT_MAX)return SOM_PROG_CORRUPT;
    return som_prog_parse((const char*)in+12,n,p);
}
#endif
