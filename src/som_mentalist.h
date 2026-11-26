/* The Mentalist's B string (player, 2026-11-26: "Mentalist should feel like a
 * Mentalist ... mess with enemy AI instructions, making them attack each
 * other maybe once per attack instead of controlling the whole enemy").
 *
 * Every Dream Petal that lands plants something in the monster's head:
 *   B1-B3 Whisper        a mark (up to 3, drawn over it)
 *   B4    Misdirection   its next attack goes at its nearest fellow monster
 *   B5    the finisher, by the D-pad against the monster:
 *         none  Sleep Paralysis (sleep by a chance that grows with the marks)
 *         toward Panic (it runs from the hero)
 *         away  Mirror (the next hit on the hero lands on the attacker's neighbour)
 *         side  Swap Minds (it and the nearest other monster trade places and
 *               turn on each other)
 *   toward, toward + B  Intrusion: Hold (it stands, it cannot attack)
 *   away, toward + B    Intrusion: Betray (one attack on its own side per mark)
 *
 * The levers are the ones the familiar uses and the enemy-tactics bench
 * measured: hero target +0x1AC = none, bite gauge +0x1ED held up, native
 * speed +0x06/+0x07 zeroed; strikes go through the game's own pending
 * damage +0x1F1, so the struck monster flinches and dies as usual.
 * Pure functions on WRAM; no I/O. */
#ifndef FLXTR_SOM_MENTALIST_H
#define FLXTR_SOM_MENTALIST_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SOM_MIND_BASE 0xE600u
#define SOM_MIND_OBJECT 0x200u
#define SOM_MIND_MARKS 3u
#define SOM_MIND_STEPS 5u
#define SOM_MIND_PARTNER_RANGE 160    /* px: the fellow monster it turns on */
#define SOM_MIND_REACH 24             /* px, Manhattan: close enough to strike */
#define SOM_MIND_SPEED 2              /* px per frame */
#define SOM_MIND_STRIKE_GAP 36ul      /* frames between betrayals' strikes */
#define SOM_MIND_BETRAY_FRAMES 240ul  /* it gives up after 4 s */
#define SOM_MIND_HOLD_FRAMES 90ul
#define SOM_MIND_PANIC_FRAMES 120ul
#define SOM_MIND_MIRROR_FRAMES 180ul
#define SOM_MIND_BOSS 0x57u           /* ids from here on are bosses: no minds to bend */

enum {SOM_MIND_NONE, SOM_MIND_BETRAY, SOM_MIND_HOLD, SOM_MIND_PANIC};
enum {SOM_MIND_WHISPER = 1, SOM_MIND_MISDIRECT, SOM_MIND_SLEEP, SOM_MIND_PANIC_MOVE,
      SOM_MIND_MIRROR, SOM_MIND_SWAP, SOM_MIND_HOLD_MOVE, SOM_MIND_BETRAY_MOVE};
static const char *const som_mind_move_names[] = {
    "", "Whisper", "Misdirection", "Sleep Paralysis", "Panic", "Mirror", "Swap Minds",
    "Intrusion: Hold", "Intrusion: Betray"};

typedef struct {
    uint8_t id[3], marks[3], effect[3], strikes[3], damage[3];
    int8_t target[3];
    int from_x[3], from_y[3];          /* panic: where the fear came from */
    unsigned long until[3], last_strike[3];
    unsigned step[3];                  /* per hero: presses so far in the string */
    unsigned long step_at[3];
    unsigned long mirror_until;
    unsigned mirror_who;
} SomMind;

static inline unsigned som_mind_u16(const uint8_t *p) { return (unsigned)(p[0] | p[1] << 8); }
static inline void som_mind_put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline uint8_t *som_mind_obj(uint8_t *ram, unsigned slot) {
    return ram + SOM_MIND_BASE + SOM_MIND_OBJECT * slot;
}
/* An ordinary living monster with a mind to bend (no boss, no familiar). */
static inline int som_mind_valid(const uint8_t *ram, unsigned slot, int familiar) {
    const uint8_t *m = ram + SOM_MIND_BASE + SOM_MIND_OBJECT * slot;
    return slot < 3u && (int)slot != familiar && m[0] == 1 && m[0x180] < SOM_MIND_BOSS &&
           !(som_mind_u16(m + 0x190) & 0x8000u) && som_mind_u16(m + 0x182) != 0;
}
static inline void som_mind_reset(SomMind *s) { memset(s, 0, sizeof(*s)); }
/* The same monster still? A new one in the slot starts with a clear head. */
static inline void som_mind_sync(SomMind *s, const uint8_t *ram, unsigned slot) {
    const uint8_t id = ram[SOM_MIND_BASE + SOM_MIND_OBJECT * slot + 0x180];
    if (s->id[slot] != id) {
        s->id[slot] = id; s->marks[slot] = 0; s->effect[slot] = SOM_MIND_NONE;
    }
}
/* The next step of hero `who`'s string (1..SOM_MIND_STEPS); a pause longer
 * than `reset` frames starts it again. */
static inline unsigned som_mind_next_step(SomMind *s, unsigned who, unsigned long frame,
                                          unsigned long reset) {
    if (who > 2u) return 1u;
    if (!s->step_at[who] || frame - s->step_at[who] > reset || s->step[who] >= SOM_MIND_STEPS)
        s->step[who] = 0;
    s->step_at[who] = frame ? frame : 1u;
    return ++s->step[who];
}
static inline unsigned som_mind_mark(SomMind *s, const uint8_t *ram, unsigned slot) {
    som_mind_sync(s, ram, slot);
    if (s->marks[slot] < SOM_MIND_MARKS) s->marks[slot]++;
    return s->marks[slot];
}
/* Its nearest fellow monster within SOM_MIND_PARTNER_RANGE, or -1. */
static inline int som_mind_partner(const uint8_t *ram, unsigned slot, int familiar) {
    const uint8_t *m = ram + SOM_MIND_BASE + SOM_MIND_OBJECT * slot;
    const int mx = (int)som_mind_u16(m + 2), my = (int)som_mind_u16(m + 4);
    int best = -1, best_d = 0;
    for (unsigned k = 0; k < 3u; k++) {
        if (k == slot) continue;
        const uint8_t *o = ram + SOM_MIND_BASE + SOM_MIND_OBJECT * k;
        if ((int)k == familiar || o[0] != 1 || o[0x180] >= 0x80u ||
            (som_mind_u16(o + 0x190) & 0x8000u) || !som_mind_u16(o + 0x182)) continue;
        const int d = abs((int)som_mind_u16(o + 2) - mx) + abs((int)som_mind_u16(o + 4) - my);
        if (d <= SOM_MIND_PARTNER_RANGE && (best < 0 || d < best_d)) { best = (int)k; best_d = d; }
    }
    return best;
}
/* It turns on its nearest fellow for `strikes` attacks. 0: nobody to turn on. */
static inline int som_mind_betray(SomMind *s, const uint8_t *ram, unsigned slot, unsigned strikes,
                                  unsigned damage, unsigned long frame, int familiar) {
    if (!som_mind_valid(ram, slot, familiar) || !strikes) return 0;
    const int partner = som_mind_partner(ram, slot, familiar);
    if (partner < 0) return 0;
    som_mind_sync(s, ram, slot);
    s->effect[slot] = SOM_MIND_BETRAY; s->target[slot] = (int8_t)partner;
    s->strikes[slot] = (uint8_t)(strikes > 9u ? 9u : strikes);
    s->damage[slot] = (uint8_t)(damage > 0xFFu ? 0xFFu : damage);
    s->until[slot] = frame + SOM_MIND_BETRAY_FRAMES; s->last_strike[slot] = 0;
    return 1;
}
static inline int som_mind_hold(SomMind *s, const uint8_t *ram, unsigned slot, unsigned long frame,
                                int familiar) {
    if (!som_mind_valid(ram, slot, familiar)) return 0;
    som_mind_sync(s, ram, slot);
    s->effect[slot] = SOM_MIND_HOLD; s->until[slot] = frame + SOM_MIND_HOLD_FRAMES;
    return 1;
}
static inline int som_mind_panic(SomMind *s, const uint8_t *ram, unsigned slot, int from_x, int from_y,
                                 unsigned long frame, int familiar) {
    if (!som_mind_valid(ram, slot, familiar)) return 0;
    som_mind_sync(s, ram, slot);
    s->effect[slot] = SOM_MIND_PANIC; s->until[slot] = frame + SOM_MIND_PANIC_FRAMES;
    s->from_x[slot] = from_x; s->from_y[slot] = from_y;
    return 1;
}
/* Swap Minds: it and its nearest fellow trade places, then each strikes the
 * other once. Returns the partner slot, or -1. */
static inline int som_mind_swap(SomMind *s, uint8_t *ram, unsigned slot, unsigned damage,
                                unsigned long frame, int familiar) {
    if (!som_mind_valid(ram, slot, familiar)) return -1;
    const int partner = som_mind_partner(ram, slot, familiar);
    if (partner < 0 || !som_mind_valid(ram, (unsigned)partner, familiar)) return -1;
    uint8_t *a = som_mind_obj(ram, slot), *b = som_mind_obj(ram, (unsigned)partner);
    uint8_t keep[4];
    memcpy(keep, a + 2, 4); memcpy(a + 2, b + 2, 4); memcpy(b + 2, keep, 4);
    som_mind_betray(s, ram, slot, 1u, damage, frame, familiar);
    s->target[slot] = (int8_t)partner;
    som_mind_betray(s, ram, (unsigned)partner, 1u, damage, frame, familiar);
    s->target[partner] = (int8_t)slot;
    return partner;
}
/* Does the monster box fit there? `open` is the map check (walls). */
typedef int (*SomMindOpen)(const uint8_t *ram, int x, int y, unsigned place);
static inline void som_mind_step(uint8_t *ram, uint8_t *m, int dx, int dy, SomMindOpen open) {
    const int mx = (int)som_mind_u16(m + 2), my = (int)som_mind_u16(m + 4);
    int sx = dx > 0 ? SOM_MIND_SPEED : dx < 0 ? -SOM_MIND_SPEED : 0;
    int sy = dy > 0 ? SOM_MIND_SPEED : dy < 0 ? -SOM_MIND_SPEED : 0;
    if (abs(dx) < SOM_MIND_SPEED) sx = dx;
    if (abs(dy) < SOM_MIND_SPEED) sy = dy;
    if (sx && (!open || open(ram, mx + sx, my, m[0x0B]))) som_mind_put16(m + 2, (unsigned)(mx + sx));
    if (sy && (!open || open(ram, (int)som_mind_u16(m + 2), my + sy, m[0x0B])))
        som_mind_put16(m + 4, (unsigned)(my + sy));
}
/* One frame of every bent mind. Bit k of the result: monster k struck its
 * target this frame; *ended gets bit k when k's effect ran out. */
static inline unsigned som_mind_tick(SomMind *s, uint8_t *ram, unsigned long frame, int familiar,
                                     SomMindOpen open, unsigned *ended) {
    unsigned struck = 0;
    if (ended) *ended = 0;
    for (unsigned k = 0; k < 3u; k++) {
        if (!s->effect[k]) continue;
        uint8_t *m = som_mind_obj(ram, k);
        const int gone = m[0x180] != s->id[k] || !som_mind_valid(ram, k, familiar);
        if (gone || frame >= s->until[k]) {
            if (!gone) m[0x1ED] = 24;      /* it comes to itself, a moment late */
            s->effect[k] = SOM_MIND_NONE;
            if (ended) *ended |= 1u << k;
            continue;
        }
        m[0x1AC] = 0xFF;                   /* no hero to go for */
        m[0x1ED] = 0x7F;                   /* its own bite never ready */
        m[0x06] = m[0x07] = 0;             /* the game does not move it */
        const int mx = (int)som_mind_u16(m + 2), my = (int)som_mind_u16(m + 4);
        if (s->effect[k] == SOM_MIND_PANIC) {
            som_mind_step(ram, m, mx - s->from_x[k], my - s->from_y[k], open);
        } else if (s->effect[k] == SOM_MIND_BETRAY) {
            const int t = s->target[k];
            if (t < 0 || t > 2 || !som_mind_valid(ram, (unsigned)t, familiar)) {
                const int again = som_mind_partner(ram, k, familiar);
                if (again < 0) { s->until[k] = frame; continue; }
                s->target[k] = (int8_t)again;
                continue;
            }
            uint8_t *o = som_mind_obj(ram, (unsigned)t);
            const int dx = (int)som_mind_u16(o + 2) - mx, dy = (int)som_mind_u16(o + 4) - my;
            if (abs(dx) + abs(dy) > SOM_MIND_REACH - 4) som_mind_step(ram, m, dx, dy, open);
            if (abs(dx) + abs(dy) <= SOM_MIND_REACH &&
                (!s->last_strike[k] || frame - s->last_strike[k] >= SOM_MIND_STRIKE_GAP)) {
                const unsigned pending = o[0x1F1] + s->damage[k];
                o[0x1F1] = (uint8_t)(pending > 0xFFu ? 0xFFu : pending);
                s->last_strike[k] = frame ? frame : 1u;
                struck |= 1u << k;
                if (!--s->strikes[k]) s->until[k] = frame + 12u;   /* the swing plays out */
            }
        }
    }
    return struck;
}
/* Mirror: armed for hero `who`; som_mind_mirror_take spends it on a hit. */
static inline void som_mind_mirror(SomMind *s, unsigned who, unsigned long frame) {
    s->mirror_who = who; s->mirror_until = frame + SOM_MIND_MIRROR_FRAMES;
}
static inline int som_mind_mirror_armed(const SomMind *s, unsigned who, unsigned long frame) {
    return s->mirror_until && frame < s->mirror_until && s->mirror_who == who;
}
#endif
