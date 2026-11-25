#ifndef FLXTR_SOM_ARENA_H
#define FLXTR_SOM_ARENA_H
/* Watts' arena (player, 2026-11-25): the smith no longer forges. Every Watts in
 * the game asks to send the party to an arena, and what his forging unlocked
 * is simply true (event flag 14: 2 = sword repaired, which the Dwarf elder's
 * event waits for; 7 = axe sold, shortcut open).
 *
 * The scripts are rewritten in memory only (Turbo: 3-byte pointers E0:6000,
 * som-decomp cutscene-engine.md for the commands); the saved ROM is untouched.
 * A yes raises event flag CD, which no script, record or save uses, and the
 * proxy runs the teleport as a script in free WRAM. The arena is the story
 * room 361; its bosses' flag 56 is raised for the whole visit (the game scans
 * a room's records again and again), so they stay out. Flag CE keeps the value
 * it had, plus one, in the game's own memory: save, state or crash, it is put
 * back as soon as the party is anywhere else. */
#include <stdint.h>
#include <string.h>
#define SOM_ARENA_MAP 361u
#define SOM_ARENA_TRANSITION 0x2C2u
#define SOM_ARENA_BOSS_FLAG 0x56u
#define SOM_ARENA_BOSS_DONE 5u
#define SOM_ARENA_FLAG 0xCDu
#define SOM_ARENA_KEEP 0xCEu
#define SOM_ARENA_HOME 0x0FCu            /* Watts' forge, map 294: if the way back is lost */
#define SOM_ARENA_SCRIPT 0xFFA0u         /* free WRAM */
#define SOM_ARENA_DOOR_Y 21u             /* tile row of the arena's gate */
#define SOM_ARENA_LEAVE_FRAMES 45u
#define SOM_ARENA_POINTERS 0x206000u
typedef struct {uint16_t event;uint8_t pointer[3];} SomArenaEvent;
/* Watts' own block (1B0-1B7) holds the new scripts; the others point to it. */
static const SomArenaEvent som_arena_events[]={
    {0x1B0,{0x37,0x97,0xE1}},{0x1B1,{0x76,0x98,0xE1}},{0x1B2,{0xB7,0x98,0xE1}},{0x1B3,{0xD5,0x9B,0xE1}},
    {0x1B4,{0x5A,0x9C,0xE1}},{0x1B5,{0xA2,0x9C,0xE1}},{0x1B6,{0x58,0x9E,0xE1}},{0x1B7,{0xE2,0x9F,0xE1}},
    {0x23D,{0xA2,0xEA,0xE1}},{0x23E,{0x23,0xEC,0xE1}},{0x0BC,{0x66,0x25,0xE1}},{0x267,{0xC6,0xFA,0xE1}},
    {0x2BC,{0x6D,0x24,0xE2}},{0x375,{0xF1,0x56,0xE2}},{0x378,{0x8D,0x57,0xE2}},{0x3D9,{0xAF,0x86,0xE2}},
    {0x482,{0xBA,0xA3,0xE2}},
};
#define SOM_ARENA_BLOCK 0x219737u
#define SOM_ARENA_BLOCK_END 0x21A033u

static inline int som_arena_text(uint8_t *out,unsigned *n,unsigned cap,const char *s){
    for(;*s;s++){
        const unsigned char c=(unsigned char)*s;
        const char *p;
        static const char marks[]="./,'!?:-",codes[]="\xBF\xC1\xC0\xC2\xC8\xCA\xC5\xC6";
        unsigned v;
        if(c>='a'&&c<='z')v=0x81u+c-'a';
        else if(c>='A'&&c<='Z')v=0x9Bu+c-'A';
        else if(c>='0'&&c<='9')v=0xB5u+c-'0';
        else if(c==' ')v=0x80u;
        else if(c=='|')v=0x7Fu;                    /* end of line */
        else if((p=strchr(marks,c))!=NULL)v=(unsigned char)codes[p-marks];
        else return 0;
        if(*n>=cap)return 0;
        out[(*n)++]=(uint8_t)v;
    }
    return 1;
}
static inline int som_arena_bytes(uint8_t *out,unsigned *n,unsigned cap,const void *data,unsigned len){
    if(*n+len>cap)return 0;
    memcpy(out+*n,data,len);*n+=len;
    return 1;
}
/* Rewrite Watts in an in-memory Turbo ROM (no copier header). All or nothing. */
static inline int som_arena_patch(uint8_t *rom,size_t size){
    static const uint8_t smith[]={0x06,0x03,0x2D,0x08,0x42,0x14,0x11,0x11,0xB3};   /* 1B0 */
    static const uint8_t gate[]={0x42,0x14,0x01,0x10,0x00,0x06};                 /* 1BE */
    static const uint8_t met1[]={0x30,0x14,0x06},met2[]={0x30,0x14,0x06};         /* 041, 042 */
    const unsigned count=sizeof(som_arena_events)/sizeof(som_arena_events[0]);
    if(!rom||size<0x230000u)return 0;
    for(unsigned i=0;i<count;i++)
        if(memcmp(rom+SOM_ARENA_POINTERS+3u*som_arena_events[i].event,som_arena_events[i].pointer,3u))return 0;
    if(memcmp(rom+SOM_ARENA_BLOCK,smith,sizeof(smith))||memcmp(rom+0x21A5D6u,gate,sizeof(gate))||
       memcmp(rom+0x2115D7u,met1,3u)||memcmp(rom+0x21162Cu,met2,3u))return 0;
    uint8_t block[SOM_ARENA_BLOCK_END-SOM_ARENA_BLOCK];
    unsigned n=0,at[8]={0};
    const unsigned cap=sizeof(block);
    int ok=1;
#define BYTES(...) do{const uint8_t b[]={__VA_ARGS__};ok&=som_arena_bytes(block,&n,cap,b,sizeof(b));}while(0)
#define TEXT(s) (ok&=som_arena_text(block,&n,cap,s))
    memset(block,0,sizeof(block));
    at[0]=n;                                       /* 1B0: every Watts */
    BYTES(0x06,0x42,0x14,0x01,0x21,0xB1,           /* FREEZE; flag 14 in 0..1: CALL 1B1 */
          0x42,0x14,0x56,0x21,0xB2,                /* 5..6: CALL 1B2 */
          0x42,0x14,0x7F,0x21,0xB3,                /* 7..15: CALL 1B3 */
          0x50);
    TEXT("Watts:My hammer's resting. These days I test fighters, not metal!");
    BYTES(0x28,0x00,0x52);
    TEXT("My arena is open. Want to show me what you've got?|");
    BYTES(0x58,0x5A,0x01);TEXT("Take me there");
    BYTES(0x5A,0x10);TEXT("Not now");
    BYTES(0x5B,0x11,0xB5,0x11,0xB6,0x11,0xB5);     /* cancel, yes, no */
    at[1]=n;                                       /* 1B1: the sword is already fine */
    BYTES(0x1E,0x8A,0x1F,0x08,0x2F,0x44,0x30,0x14,0x02,0x02);
    at[2]=n;                                       /* 1B2: the axe and the shortcut */
    BYTES(0x30,0x14,0x07,0x0A,0x1E,0x92,0x1F,0x08,0x2F,0x44,0x29,0xC2,0x02);
    at[3]=n;                                       /* 1B3: an axe, if he never gave one */
    BYTES(0x42,0xC2,0x1F,0x11,0xB4,0x1E,0x92,0x1F,0x08,0x2F,0x44,0x29,0xC2);
    at[4]=n;                                       /* 1B4 */
    BYTES(0x02);
    at[5]=n;                                       /* 1B5: no */
    BYTES(0x50);TEXT("Suit yourself! I'll be here when you feel like a fight.|");
    BYTES(0x28,0x00,0x51,0x00);
    at[6]=n;                                       /* 1B6: yes */
    BYTES(0x50);TEXT("That's the spirit! Walk back out through the gate to leave.|");
    BYTES(0x28,0x00,0x51,0x30,SOM_ARENA_FLAG,0x01,0x00);
    at[7]=at[0];
#undef BYTES
#undef TEXT
    if(!ok)return 0;
    memcpy(rom+SOM_ARENA_BLOCK,block,sizeof(block));
    for(unsigned i=0;i<count;i++){
        const unsigned e=som_arena_events[i].event;
        const uint32_t to=SOM_ARENA_BLOCK+(e>=0x1B0u&&e<=0x1B7u?at[e-0x1B0u]:at[0]);
        uint8_t *p=rom+SOM_ARENA_POINTERS+3u*e;
        p[0]=(uint8_t)to;p[1]=(uint8_t)(to>>8);p[2]=(uint8_t)(0xC0u+(to>>16));
    }
    /* The elder's event no longer waits for the forge: flag 14 in 0..1 makes
     * the repair true (CALL 1B1) instead of ending. The sprite joining opens
     * the shortcut as the axe did (6 -> 7). */
    rom[0x21A5D9u]=0x21;rom[0x21A5DAu]=0xB1;
    rom[0x2115D9u]=0x07;rom[0x21162Eu]=0x07;
    return 1;
}
/* A transition that leads to `map`: the one last taken, else the first. */
static inline int som_arena_transition_to(const uint8_t *table,unsigned map,unsigned last){
    if(last<0x400u&&((table[4u*last]|(unsigned)table[4u*last+1u]<<8)&0x1FFu)==map)return (int)last;
    for(unsigned n=0;n<0x400u;n++)
        if(((table[4u*n]|(unsigned)table[4u*n+1u]<<8)&0x1FFu)==map)return (int)n;
    return -1;
}
/* The story's flag as it was before the visit. */
static inline void som_arena_restore(uint8_t *ram){
    uint8_t *keep=ram+0xCF00u+SOM_ARENA_KEEP,*boss=ram+0xCF00u+SOM_ARENA_BOSS_FLAG;
    if(!*keep)return;
    *boss=(uint8_t)((*boss&0xF0u)|((*keep-1u)&15u));
    *keep=0;
}
/* Start a map change as an event script: only from idle field play. */
static inline int som_arena_go(uint8_t *ram,unsigned transition,int keep_bosses_out){
    if(ram[0xD0]!=0||ram[0xE4]!=1||ram[0xE6]!=0x0F||transition>=0x400u)return 0;
    uint8_t *s=ram+SOM_ARENA_SCRIPT;
    unsigned n=0;
    if(keep_bosses_out){
        const uint8_t boss=ram[0xCF00u+SOM_ARENA_BOSS_FLAG];
        if(!ram[0xCF00u+SOM_ARENA_KEEP])ram[0xCF00u+SOM_ARENA_KEEP]=(uint8_t)((boss&15u)+1u);
        ram[0xCF00u+SOM_ARENA_BOSS_FLAG]=(uint8_t)((boss&0xF0u)|SOM_ARENA_BOSS_DONE);
    }
    s[n++]=(uint8_t)(0x18u|transition>>8);s[n++]=(uint8_t)transition;
    s[n]=0x00;
    ram[0xD1]=(uint8_t)SOM_ARENA_SCRIPT;ram[0xD2]=(uint8_t)(SOM_ARENA_SCRIPT>>8);ram[0xD3]=0x7E;
    ram[0xD0]=1;
    return 1;
}
#endif
