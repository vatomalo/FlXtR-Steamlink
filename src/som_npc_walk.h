/* Living world: native NPC walk for Secret of Mana Turbo (EXPERIMENTAL).
 *
 * This is a game-RAM write path. It is OFF unless all of these hold:
 *   - the opt-in marker SOM_NPC_ENABLE_FILE exists on the Steam Link,
 *   - the loaded ROM matches the fingerprints below (Turbo routines that
 *     were disassembled and compared with cidwel/som-decomp sources),
 *   - every live-state check in som_npc_walk_refusal() passes.
 *
 * It reproduces exactly what the game's own event command ACTOR_WALK (0x32,
 * $C1:EEEA) writes for an NPC object, so the original per-frame code then
 * moves it: the mover $C0:D229 applies terrain/collision, and the Turbo
 * countdown at $C0:EB86 (hooked from $C0:FBFE) stops it after `frames`
 * frames and clears velocity, busy and walk pose. Only static NPCs
 * (record byte +3 bit 7 -> obj+0x44 bit 7, native AI off, gate $F0:78F9)
 * are accepted, so no native script competes with the command.
 * See docs/SOM-LIVING-WORLD.md. */
#ifndef SOM_NPC_WALK_H
#define SOM_NPC_WALK_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SOM_NPC_FIRST_SLOT 3u
#define SOM_NPC_LAST_SLOT 11u
#define SOM_NPC_MAX_FRAMES 32u
#define SOM_NPC_OBJECT(slot) (0xE000u+(unsigned)(slot)*0x200u)
#define SOM_NPC_COMMAND_FILE "/tmp/flxtr-som-npc-command"
#define SOM_NPC_ENABLE_FILE "/home/apps/retroarch/cores/experimental/som_ai_living_world_enable"

typedef struct {
    unsigned map, slot, kind, x, y, dir, frames;
} SomNpcWalk;

typedef struct {
    unsigned long offset;  /* header-stripped HiROM file offset */
    unsigned length;
    uint32_t fnv1a;
    const char *name;
} SomRomFingerprint;

/* Measured on "Secret of Mana, Turbo.smc" (2026-11-13). The US-ROM sources
 * of ACTOR_WALK and the mover assemble to these exact bytes; Turbo moved the
 * countdown and the static-NPC AI gate, whose new code was disassembled. */
static const SomRomFingerprint som_npc_fingerprints[]={
    {0x01EEEAul,206u,0xc85d4187u,"ACTOR_WALK $C1:EEEA"},
    {0x00D229ul,153u,0xa5b0e386u,"NPC mover $C0:D229"},
    {0x00EB86ul,48u,0x5639fa07u,"Turbo walk countdown $C0:EB86"},
    {0x00FBEFul,18u,0x3278542bu,"countdown hook $C0:FBEF"},
    {0x3078F9ul,6u,0x182d858cu,"Turbo static-NPC AI gate $F0:78F9"},
    {0x00F8DFul,10u,0x1fa930bau,"AI gate call $C0:F8DF"},
};

static uint32_t som_fnv1a(const uint8_t *data,size_t size){
    uint32_t hash=0x811c9dc5u;
    for(size_t i=0;i<size;i++){hash^=data[i];hash*=0x01000193u;}
    return hash;
}

/* 1 when every routine the walk relies on is byte-identical to the measured
 * Turbo ROM. A copier header (512 bytes) is skipped. */
static int som_npc_rom_verified(const uint8_t *rom,size_t size){
    if(!rom)return 0;
    if(size%0x8000u==512u){rom+=512;size-=512;}
    for(size_t i=0;i<sizeof(som_npc_fingerprints)/sizeof(som_npc_fingerprints[0]);i++){
        const SomRomFingerprint *f=&som_npc_fingerprints[i];
        if(f->offset+f->length>size)return 0;
        if(som_fnv1a(rom+f->offset,f->length)!=f->fnv1a)return 0;
    }
    return 1;
}

/* "npc_walk MAP SLOT KIND X Y DIR FRAMES", all decimal. DIR: 0 up, 1 down,
 * 2 right, 3 left (ACTOR_WALK order). X/Y are the pixel position the
 * caller observed: a stale command (the NPC moved meanwhile) is refused. */
static int som_npc_parse_walk(const char *line,SomNpcWalk *out){
    SomNpcWalk walk;
    char tail;
    if(!line||!out)return 0;
    if(sscanf(line,"npc_walk %u %u %u %u %u %u %u %c",&walk.map,&walk.slot,&walk.kind,
              &walk.x,&walk.y,&walk.dir,&walk.frames,&tail)!=7)return 0;
    *out=walk;
    return 1;
}

static unsigned som_npc_u16(const uint8_t *wram,unsigned offset){
    return (unsigned)wram[offset]|(unsigned)wram[offset+1u]<<8;
}

/* NULL when the walk may be applied now, else the reason it is refused.
 * Order matters only for the message; every check must pass. */
static const char *som_npc_walk_refusal(const uint8_t *wram,size_t size,const SomNpcWalk *w){
    if(!wram||size<0x20000u)return "no WRAM";
    if(!w)return "no command";
    if(w->slot<SOM_NPC_FIRST_SLOT||w->slot>SOM_NPC_LAST_SLOT)return "slot is not an NPC slot";
    if(w->dir>3u)return "bad direction";
    if(w->frames<1u||w->frames>SOM_NPC_MAX_FRAMES)return "bad frame count";
    if(w->kind<0x80u||w->kind>0xFFu)return "kind is not an NPC kind";
    /* Field mode only: the same test as WRAM.game_mode() in som_memory.py. */
    if(wram[0xE000]!=1u)return "no game";
    if(wram[0x00E4]!=1u||wram[0x00E6]!=0x0Fu)return "map transition";
    if(wram[0x00D0]!=0u)return "event running";
    if(wram[0x007D]==1u)return "ring menu open";
    if(wram[0x0052]|wram[0x00FF]|wram[0x00F1])return "world paused or map change pending";
    if(wram[0x00E8]&0x04u)return "objects frozen";
    if(wram[0x00ED]&0x80u)return "boss object mode";
    if((som_npc_u16(wram,0x00DC)&0x1FFu)!=w->map)return "different map";
    unsigned o=SOM_NPC_OBJECT(w->slot);
    if(wram[o]!=1u)return "slot not active";
    if(wram[o+0x3Du]!=w->kind)return "kind changed";
    if(!(wram[o+0x44u]&0x80u))return "NPC has native AI (not static)";
    if(som_npc_u16(wram,o+0x02u)!=w->x||som_npc_u16(wram,o+0x04u)!=w->y)return "stale position";
    if(som_npc_u16(wram,0x003A)==o)return "NPC is the talk target";
    if(wram[o+0x0Au]|wram[o+0x42u]|wram[o+0x1Cu]|wram[o+0x60u]|wram[o+0x06u]|wram[o+0x07u])
        return "NPC busy";
    return NULL;
}

/* The NPC branch of ACTOR_WALK ($C1:EEEA) with a nonzero count. */
static void som_npc_apply_walk(uint8_t *wram,const SomNpcWalk *w){
    static const uint8_t vx[4]={0x00,0x00,0x02,0x82},vy[4]={0x82,0x02,0x00,0x00};
    static const uint8_t facing[4]={0x00,0x01,0x02,0x82};
    unsigned o=SOM_NPC_OBJECT(w->slot);
    wram[o+0x1Cu]=0;wram[o+0x13u]=0;
    wram[o+0x0Au]=(uint8_t)w->frames;
    wram[o+0x67u]=0;wram[o+0x42u]=1;
    wram[o+0x06u]=vx[w->dir];wram[o+0x07u]=vy[w->dir];
    wram[o+0x10u]=facing[w->dir];
    wram[o+0x30u]=0xFF;
    uint8_t flags=wram[o+0x71u];
    if(flags&0x60u){
        if(flags&0x40u)wram[o+0x10u]=0;
        wram[o+0x11u]=0;
    }else wram[o+0x11u]=1;
}

#endif
