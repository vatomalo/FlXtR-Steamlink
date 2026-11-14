/* Familiar: a cloned monster that fights for the hero (EXPERIMENTAL).
 *
 * Monsters only ever target heroes (cidwel/som-decomp docs/rabite.md), so
 * the familiar's own aggression is switched off every frame (target slot
 * +0x1AC = none, bite cooldown +0x1ED held up, velocity cleared) and it is
 * driven here with the player AI's fighting rules: stay on the nearest
 * monster unless another is clearly closer, close in, strike at most every
 * SOM_FAMILIAR_STRIKE_FRAMES (the player's 0.6 s swing pacing), and follow
 * a step behind the hero when no monster is near. Strikes go through the
 * game's own damage path (pending damage +0x1F1), so targets flinch, are
 * knocked back and die normally. The hero's own hits cannot kill it.
 * Pure functions on WRAM; no I/O. */
#ifndef FLXTR_SOM_FAMILIAR_H
#define FLXTR_SOM_FAMILIAR_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "som_class_palette.h"

#define SOM_FAMILIAR_MONSTER_BASE 0xE600u
#define SOM_FAMILIAR_OBJECT 0x200u
#define SOM_FAMILIAR_SPEED 2            /* px per frame */
#define SOM_FAMILIAR_REACH 24           /* px, Manhattan */
#define SOM_FAMILIAR_RANGE 160          /* px: monsters it goes after */
#define SOM_FAMILIAR_FOLLOW 28          /* px it keeps from the hero when idle */
#define SOM_FAMILIAR_SWITCH 24          /* px closer before switching target */
#define SOM_FAMILIAR_STRIKE_FRAMES 36ul /* 0.6 s at 60 fps */
#define SOM_FAMILIAR_DAMAGE 8

typedef struct {
    int slot;                 /* monster slot 0..2, -1 = none */
    int target;               /* monster slot it fights, -1 = none */
    unsigned long last_strike;
    unsigned map;             /* map id it was summoned on */
} SomFamiliar;

static inline unsigned som_fam_u16(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8); }
static inline void som_fam_put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static inline int som_fam_alive(const uint8_t *object) {
    const unsigned status = som_fam_u16(object + 0x190);
    return object[0] == 1 && object[0x180] < 0x80 && !(status & 0x8000u);
}

enum { SOM_FAMILIAR_DIED = 0, SOM_FAMILIAR_HERE = 1, SOM_FAMILIAR_LEFT_MAP = 2 };

/* One frame of the familiar. Returns SOM_FAMILIAR_HERE while it is with the
 * hero, SOM_FAMILIAR_DIED or SOM_FAMILIAR_LEFT_MAP once it is gone from this
 * map (then f->slot = -1). */
static inline int som_familiar_tick(SomFamiliar *f, uint8_t *ram, unsigned long frame) {
    if (f->slot < 0) return SOM_FAMILIAR_DIED;
    uint8_t *me = ram + SOM_FAMILIAR_MONSTER_BASE + (unsigned)f->slot * SOM_FAMILIAR_OBJECT;
    if (som_fam_u16(ram + 0xDC) != f->map) {
        f->slot = f->target = -1;
        return SOM_FAMILIAR_LEFT_MAP;
    }
    if (!som_fam_alive(me)) {
        f->slot = f->target = -1;
        return SOM_FAMILIAR_DIED;
    }
    /* Friendly fire cannot kill it: queued damage is cleared before the
     * game applies it and HP stays full (the player killed the first one). */
    me[0x1F1] = 0;
    som_fam_put16(me + 0x182, som_fam_u16(me + 0x184));
    me[0x1AC] = 0xFF;          /* no hero target */
    me[0x1ED] = 0x7F;          /* bite never ready */
    me[0x06] = me[0x07] = 0;   /* no native movement */
    const int mx = (int)som_fam_u16(me + 2), my = (int)som_fam_u16(me + 4);
    int best = -1, best_d = 0, kept_d = -1;
    for (int slot = 0; slot < 3; slot++) {
        if (slot == f->slot) continue;
        const uint8_t *o = ram + SOM_FAMILIAR_MONSTER_BASE + (unsigned)slot * SOM_FAMILIAR_OBJECT;
        if (!som_fam_alive(o)) continue;
        const int d = abs((int)som_fam_u16(o + 2) - mx) + abs((int)som_fam_u16(o + 4) - my);
        if (d > SOM_FAMILIAR_RANGE) continue;
        if (slot == f->target) kept_d = d;
        if (best < 0 || d < best_d) { best = slot; best_d = d; }
    }
    if (kept_d >= 0 && kept_d <= best_d + SOM_FAMILIAR_SWITCH) { best = f->target; best_d = kept_d; }
    f->target = best;
    int gx, gy, stop;
    if (best >= 0) {
        const uint8_t *t = ram + SOM_FAMILIAR_MONSTER_BASE + (unsigned)best * SOM_FAMILIAR_OBJECT;
        gx = (int)som_fam_u16(t + 2); gy = (int)som_fam_u16(t + 4); stop = SOM_FAMILIAR_REACH - 4;
    } else {
        gx = (int)som_fam_u16(ram + 0xE002); gy = (int)som_fam_u16(ram + 0xE004);
        stop = SOM_FAMILIAR_FOLLOW;
    }
    int dx = gx - mx, dy = gy - my;
    if (abs(dx) + abs(dy) > stop) {
        int sx = dx > 0 ? SOM_FAMILIAR_SPEED : dx < 0 ? -SOM_FAMILIAR_SPEED : 0;
        int sy = dy > 0 ? SOM_FAMILIAR_SPEED : dy < 0 ? -SOM_FAMILIAR_SPEED : 0;
        if (abs(dx) < SOM_FAMILIAR_SPEED) sx = dx;
        if (abs(dy) < SOM_FAMILIAR_SPEED) sy = dy;
        som_fam_put16(me + 2, (unsigned)(mx + sx));
        som_fam_put16(me + 4, (unsigned)(my + sy));
    }
    if (best >= 0 && best_d <= SOM_FAMILIAR_REACH &&
        frame - f->last_strike >= SOM_FAMILIAR_STRIKE_FRAMES) {
        uint8_t *t = ram + SOM_FAMILIAR_MONSTER_BASE + (unsigned)best * SOM_FAMILIAR_OBJECT;
        const unsigned pending = t[0x1F1] + SOM_FAMILIAR_DAMAGE;
        t[0x1F1] = (uint8_t)(pending > 0xFF ? 0xFF : pending);
        f->last_strike = frame;
    }
    return SOM_FAMILIAR_HERE;
}

/* Blue-white familiar identity. The same pure BGR555 transform applies to
 * the copied monster's palette, without touching its allegiance or logic. */
static inline void som_familiar_tint(uint8_t *palette) {
    som_pal_apply(palette,SOM_PAL_CELESTIAL_FAMILIAR);
}

/* Only summoners summon: a character whose mind (intelligence or wisdom,
 * +0x18B/+0x18C) is at least its strength, agility and constitution
 * (+0x188..+0x18A). Live: girl 11/11/11/7/15 and sprite 8/13/9/15/5 are
 * summoners, the boy 19/19/18/8/9 is not. */
static inline int som_familiar_summoner(const uint8_t *character) {
    const unsigned str = character[0x188], agi = character[0x189], con = character[0x18A];
    const unsigned mind = character[0x18B] > character[0x18C] ? character[0x18B] : character[0x18C];
    const unsigned body = str > agi ? (str > con ? str : con) : (agi > con ? agi : con);
    return mind > 0 && mind >= body;
}

/* Everything that makes up the familiar, carried to the next map. */
typedef struct {
    int valid;
    uint8_t object[SOM_FAMILIAR_OBJECT];
    uint8_t tiles[0x1000];
    uint8_t palette[0x20];
} SomFamiliarCarry;

/* Object fields that belong to a monster slot (VRAM tiles/addresses,
 * palette attribute +0x8B and address +0xF8); same list as the clone. */
static const uint8_t som_familiar_slot_fields[] = {0x6C,0x6E,0x86,0x8A,0x92,0x96,0x9A,0x9E,0xA2,
    0xA6,0xAA,0xAE,0xB2,0xB6,0xD2,0xD6,0xDA,0xF8,0xFA};

static inline void som_familiar_save(SomFamiliarCarry *c, const uint8_t *ram, int slot) {
    memcpy(c->object, ram + SOM_FAMILIAR_MONSTER_BASE + (unsigned)slot * SOM_FAMILIAR_OBJECT,
           SOM_FAMILIAR_OBJECT);
    memcpy(c->tiles, ram + 0x5000u + (unsigned)slot * 0x1000u, sizeof(c->tiles));
    memcpy(c->palette, ram + 0xDFA0u + (unsigned)slot * 0x20u, sizeof(c->palette));
    c->valid = 1;
}

/* Put a carried familiar into the first empty monster slot next to the
 * hero; returns the slot or -1 (no empty slot yet). Occupied slots, which
 * in towns hold villagers, are never touched. */
static inline int som_familiar_restore(const SomFamiliarCarry *c, uint8_t *ram) {
    if (!c->valid) return -1;
    for (int slot = 0; slot < 3; slot++) {
        uint8_t *to = ram + SOM_FAMILIAR_MONSTER_BASE + (unsigned)slot * SOM_FAMILIAR_OBJECT;
        if (to[0] == 1) continue;
        uint8_t keep[sizeof(som_familiar_slot_fields)][2];
        for (unsigned i = 0; i < sizeof(som_familiar_slot_fields); i++) {
            keep[i][0] = to[som_familiar_slot_fields[i]];
            keep[i][1] = to[som_familiar_slot_fields[i] + 1];
        }
        memcpy(to, c->object, SOM_FAMILIAR_OBJECT);
        for (unsigned i = 0; i < sizeof(som_familiar_slot_fields); i++) {
            to[som_familiar_slot_fields[i]] = keep[i][0];
            to[som_familiar_slot_fields[i] + 1] = keep[i][1];
        }
        memcpy(ram + 0x5000u + (unsigned)slot * 0x1000u, c->tiles, sizeof(c->tiles));
        memcpy(ram + 0xDFA0u + (unsigned)slot * 0x20u, c->palette, sizeof(c->palette));
        som_fam_put16(to + 2, som_fam_u16(ram + 0xE002) + 24u);
        som_fam_put16(to + 4, som_fam_u16(ram + 0xE004));
        to[0x1F1] = 0;
        som_fam_put16(to + 0x182, som_fam_u16(to + 0x184));
        return slot;
    }
    return -1;
}

#endif
