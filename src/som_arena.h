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
enum {SOM_ARENA_WAVES=1,SOM_ARENA_BOSSES=2,SOM_ARENA_DUEL=3};
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
    unsigned n=0,at[8]={0},duel=0;
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
    TEXT("My arena is open. What will it be?|");
    BYTES(0x58,0x5A,0x01);TEXT("Waves");
    BYTES(0x5A,0x08);TEXT("Bosses");
    BYTES(0x5A,0x10);TEXT("Duel");
    BYTES(0x5A,0x16);TEXT("No");                   /* cancel, waves, bosses, duel, no */
    BYTES(0x5B,0x11,0xB5,0x11,0xB6,0x11,0xB7,0x14,0x82,0x11,0xB5);
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
    at[6]=n;                                       /* 1B6: waves */
    BYTES(0x50);TEXT("Ten waves, each tougher! Hold down at the gate to leave.|");
    BYTES(0x28,0x00,0x51,0x30,SOM_ARENA_FLAG,SOM_ARENA_WAVES,0x00);
    at[7]=n;                                       /* 1B7: bosses */
    BYTES(0x50);TEXT("Old foes want a rematch! Hold down at the gate to leave.|");
    BYTES(0x28,0x00,0x51,0x30,SOM_ARENA_FLAG,SOM_ARENA_BOSSES,0x00);
    duel=n;                                        /* 482: duel */
    BYTES(0x50);TEXT("Two players, best of three! Hold down at the gate to leave.|");
    BYTES(0x28,0x00,0x51,0x30,SOM_ARENA_FLAG,SOM_ARENA_DUEL,0x00);
#undef BYTES
#undef TEXT
    if(!ok)return 0;
    memcpy(rom+SOM_ARENA_BLOCK,block,sizeof(block));
    for(unsigned i=0;i<count;i++){
        const unsigned e=som_arena_events[i].event;
        const uint32_t to=SOM_ARENA_BLOCK+(e>=0x1B0u&&e<=0x1B7u?at[e-0x1B0u]:e==0x482u?duel:at[0]);
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
/* ---- The fights ---- */
#define SOM_ARENA_WAVE_COUNT 10u
#define SOM_ARENA_LIST 0xC800u            /* the room's object list: 16 bytes each */
#define SOM_ARENA_MONSTERS 0x54u          /* 0x00-0x53: every one draws and fights there */
/* Bosses whose art leaves the room's picture whole (all 29 tried, 2026-11-25),
 * each beaten once its story flag is past its record's range. */
typedef struct {uint8_t id,flag,past;} SomArenaBoss;
static const SomArenaBoss som_arena_bosses[]={
    {0x57,0x10,0},{0x5C,0x50,4},{0x65,0x1B,4},
    {0x5A,0x2C,3},{0x5D,0x21,3},{0x5E,0x26,4},
    {0x69,0x24,5},{0x63,0x34,4},{0x64,0x35,6},
    {0x66,0x37,5},{0x71,0xA9,1},{0x70,0x47,3},
    {0x76,0x46,3},{0x7B,0x46,1},{0x78,0x4E,2},
};
#define SOM_ARENA_BOSS_COUNT (sizeof(som_arena_bosses)/sizeof(som_arena_bosses[0]))
static inline int som_arena_boss_beaten(const uint8_t *ram,unsigned i){
    return i<SOM_ARENA_BOSS_COUNT&&(ram[0xCF00u+som_arena_bosses[i].flag]&15u)>=som_arena_bosses[i].past;
}
/* The n-th beaten boss, -1 with none. */
static inline int som_arena_boss_pick(const uint8_t *ram,unsigned n){
    unsigned have=0;
    for(unsigned i=0;i<SOM_ARENA_BOSS_COUNT;i++)have+=(unsigned)som_arena_boss_beaten(ram,i);
    if(!have)return -1;
    n%=have;
    for(unsigned i=0;i<SOM_ARENA_BOSS_COUNT;i++)
        if(som_arena_boss_beaten(ram,i)&&!n--)return (int)i;
    return -1;
}
/* A monster for a wave: one whose level (record byte 0, 29 bytes each) is near
 * the target; `roll` picks among them. */
static inline unsigned som_arena_species(const uint8_t *records,unsigned target,unsigned roll){
    for(unsigned spread=3u;;spread+=3u){
        unsigned have=0;
        for(unsigned id=0;id<SOM_ARENA_MONSTERS;id++){
            const unsigned lv=records[29u*id];
            have+=lv<=target+spread&&lv+spread>=target;
        }
        if(!have&&spread<120u)continue;
        if(!have)return 0;
        unsigned k=roll%have;
        for(unsigned id=0;id<SOM_ARENA_MONSTERS;id++){
            const unsigned lv=records[29u*id];
            if(lv<=target+spread&&lv+spread>=target&&!k--)return id;
        }
    }
}
static inline unsigned som_arena_wave_level(unsigned hero,unsigned wave){
    const unsigned lv=hero*(60u+7u*wave)/100u;
    return lv<1u?1u:lv>0x40u?0x40u:lv;
}
/* One entry of the room's list; off = it never spawns. Event 0 = none. */
static inline void som_arena_list(uint8_t *ram,unsigned k,int on,unsigned id,unsigned x,unsigned y){
    uint8_t *e=ram+SOM_ARENA_LIST+16u*k;
    memset(e,0,16u);
    e[0]=1;e[7]=0x40;e[9]=on?0x0F:0xFF;e[10]=(uint8_t)(x|0x80u);e[11]=(uint8_t)y;e[12]=0x40;e[13]=(uint8_t)id;
    if(id<0x57u)e[14]=1;                             /* an ordinary monster's contact event */
}
/* Run a short script from idle (REFRESH_OBJECTS 09, GOLD 36). */
static inline int som_arena_script(uint8_t *ram,const uint8_t *code,unsigned n){
    if(ram[0xD0]!=0||ram[0xE4]!=1||ram[0xE6]!=0x0F||n>16u)return 0;
    memcpy(ram+SOM_ARENA_SCRIPT,code,n);
    ram[0xD1]=(uint8_t)SOM_ARENA_SCRIPT;ram[0xD2]=(uint8_t)(SOM_ARENA_SCRIPT>>8);ram[0xD3]=0x7E;
    ram[0xD0]=1;
    return 1;
}
/* Duels: the game's own weapon box (+C0..C3) against the other's body box
 * (+C8..CB): centre, size, relative to the object (som-decomp glove 2.5). */
static inline int som_arena_boxes(int ax,int ay,int aw,int ah,int bx,int by,int bw,int bh){
    const int dx=ax>bx?ax-bx:bx-ax,dy=ay>by?ay-by:by-ay;
    return aw>0&&ah>0&&bw>0&&bh>0&&2*dx<aw+bw&&2*dy<ah+bh;
}
static inline int som_arena_box_at(const uint8_t *o,unsigned box,int *x,int *y,int *w,int *h){
    *x=(o[2]|o[3]<<8)+(int8_t)o[box];*y=(o[4]|o[5]<<8)+(int8_t)o[box+1u];
    *w=o[box+2u];*h=o[box+3u];
    return *w>0&&*h>0;
}
static inline int som_arena_body(const uint8_t *o,int *x,int *y,int *w,int *h){
    if(som_arena_box_at(o,0xC8u,x,y,w,h))return 1;
    *x=o[2]|o[3]<<8;*y=(o[4]|o[5]<<8)-14;*w=14;*h=26;  /* mid-swing the game clears it */
    return 1;
}
/* 2 = a weapon swing, 1 = one of his projectiles, 0 = no hit. */
static inline int som_arena_hit(uint8_t *ram,unsigned who,unsigned other){
    uint8_t *a=ram+0xE000u+0x200u*who;
    const uint8_t *b=ram+0xE000u+0x200u*other;
    int ax,ay,aw,ah,bx,by,bw,bh;
    som_arena_body(b,&bx,&by,&bw,&bh);
    if(a[0x1C]&&som_arena_box_at(a,0xC0u,&ax,&ay,&aw,&ah)&&som_arena_boxes(ax,ay,aw,ah,bx,by,bw,bh))return 2;
    for(unsigned k=0;k<3u;k++){
        uint8_t *e=ram+0xD000u+0x40u*who+0x10u*k;
        const unsigned kind=e[0]&7u;
        if(!kind||(e[0]&0xF8u)||e[7])continue;
        const int size=kind==2u?32:16;
        if(!som_arena_boxes(e[2]|e[3]<<8,e[4]|e[5]<<8,size,size,bx,by,bw,bh))continue;
        if(kind<3u)e[7]=8;                           /* spent, as on a monster */
        return 1;
    }
    return 0;
}
static inline unsigned som_arena_damage(const uint8_t *a,const uint8_t *b){
    const unsigned stage=a[0x19B]>8u?8u:a[0x19B],power=a[0x198]*(2u*stage+4u)/4u;
    const unsigned guard=(b[0x1A5]|(unsigned)b[0x1A6]<<8)/2u;
    unsigned damage=power>guard?power-guard:0u;
    if(damage<power/5u)damage=power/5u;
    damage/=2u;                                      /* a duel lasts a while */
    return damage<2u?2u:damage;
}
/* All three down in the arena: stand the party up (the game's own recovery,
 * som-decomp cutscene-ending.md event 41E) and take it home. Overrides the
 * game-over event in its first frame. */
static inline int som_arena_all_down(const uint8_t *ram){
    unsigned members=0;
    for(unsigned who=0;who<3u;who++){
        const uint8_t *o=ram+0xE000u+0x200u*who;
        if(!(o[0x184]|o[0x185]))continue;            /* not in the party */
        members++;
        if(o[0x182]|o[0x183])return 0;
    }
    return members!=0;
}
static inline int som_arena_carry_home(uint8_t *ram,unsigned transition){
    if(ram[0xE4]!=1||ram[0xE6]!=0x0F||transition>=0x400u)return 0;
    uint8_t *s=ram+SOM_ARENA_SCRIPT;
    unsigned n=0;
    for(unsigned who=0;who<3u;who++)for(unsigned b=0x90u;b<=0x91u;b++){
        s[n++]=0x39;s[n++]=(uint8_t)who;s[n++]=(uint8_t)b;s[n++]=0;
    }
    s[n++]=0x2F;s[n++]=0x04;s[n++]=0x2F;s[n++]=0x84;s[n++]=0x2F;s[n++]=0x44;
    s[n++]=(uint8_t)(0x18u|transition>>8);s[n++]=(uint8_t)transition;s[n]=0;
    for(unsigned who=0;who<3u;who++){
        uint8_t *o=ram+0xE000u+0x200u*who;
        if(o[0x184]|o[0x185]){o[0x182]=1;o[0x183]=0;o[0x191]&=0x7Fu;o[0x1F1]=o[0x1F2]=0;}
    }
    ram[0xD1]=(uint8_t)SOM_ARENA_SCRIPT;ram[0xD2]=(uint8_t)(SOM_ARENA_SCRIPT>>8);ram[0xD3]=0x7E;
    ram[0xD0]=1;
    return 1;
}
#endif
