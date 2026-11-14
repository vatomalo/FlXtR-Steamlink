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

/* One frame of the familiar; returns 0 once it is gone (then f->slot = -1). */
static inline int som_familiar_tick(SomFamiliar *f, uint8_t *ram, unsigned long frame) {
    if (f->slot < 0) return 0;
    uint8_t *me = ram + SOM_FAMILIAR_MONSTER_BASE + (unsigned)f->slot * SOM_FAMILIAR_OBJECT;
    if (!som_fam_alive(me) || som_fam_u16(ram + 0xDC) != f->map) {
        f->slot = f->target = -1;
        return 0;
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
    return 1;
}

#endif
