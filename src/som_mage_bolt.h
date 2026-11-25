/* Mage bolt (player, 2026-11-22: "all mage types should use a magic based
 * melee attack ... glove anim, single one, no kicks ... or just paint a
 * single fireball hadoken style"). A caster's B is the game's own glove
 * punch (a plain swing, +0x1C = 0x20; never charged, so no kicks) and, as
 * the fist goes out, a painted ball of the B spell's element flies from it
 * along the facing. The ball does the magic damage itself (the monster's
 * pending damage +0x1F1, as Kage Bunshin's blasts): the game's spell, its
 * casting pose and its spirit never run, so nothing waits.
 *
 * Elements are the game's spell groups (som_spell_element): 0 Gnome,
 * 1 Undine, 2 Salamando, 3 Sylphid, 4 Luna, 5 Dryad, 6 Shade, 7 Lumina.
 * Drawn on the proxy's copy of the video frame; game RAM is only written
 * for the damage. */
#ifndef FLXTR_SOM_MAGE_BOLT_H
#define FLXTR_SOM_MAGE_BOLT_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "som_overlay.h"

#define SOM_BOLTS 4u
#define SOM_BOLT_SPEED 5          /* px per frame */
#define SOM_BOLT_FRAMES 22u       /* flight: ~110 px */
#define SOM_BOLT_LEAD 10          /* starts this far ahead of the hero (the fist) */
#define SOM_BOLT_HIT_X 12
#define SOM_BOLT_HIT_Y 12
#define SOM_BOLT_BURST_FRAMES 10u /* the splash where it hits or fades */
#define SOM_BOLT_ELEMENTS 8u
/* Looks (player, 2026-11-25: "give them other types"): the ball; Dream
 * Petal (Mentalist), a slow lotus; Ribbon (Dancer) and Healing Touch
 * (Cleric), shown where they happen and never flying. */
enum { SOM_BOLT_BALL, SOM_BOLT_PETAL, SOM_BOLT_RIBBON, SOM_BOLT_TOUCH };
#define SOM_PETAL_SPEED 3
#define SOM_PETAL_FRAMES 34u
#define SOM_SHOW_FRAMES 18u

typedef struct {
    int active, x, y, dx, dy;     /* x, y: map pixels of the ball (the hero's feet frame) */
    unsigned who, element, damage, map;
    int big;                      /* Y's ball: larger (the class's second spell) */
    int kind;                     /* SOM_BOLT_* below: the class's own look */
    unsigned long start, burst;   /* burst: frame of the splash, 0 none */
} SomBolt;

/* Core, glow and rim colour of each element. */
static const uint8_t som_bolt_rgb[SOM_BOLT_ELEMENTS][3][3] = {
    {{255, 240, 200}, {214, 160, 72}, {120, 84, 40}},    /* Gnome: rock */
    {{240, 255, 255}, {120, 200, 255}, {40, 96, 220}},   /* Undine: ice */
    {{255, 250, 220}, {255, 200, 60}, {235, 90, 24}},    /* Salamando: fire */
    {{255, 255, 255}, {250, 245, 120}, {150, 170, 255}}, /* Sylphid: lightning */
    {{255, 250, 230}, {230, 200, 255}, {140, 90, 220}},  /* Luna: moon */
    {{240, 255, 220}, {130, 230, 110}, {40, 140, 60}},   /* Dryad: leaf */
    {{240, 200, 255}, {150, 60, 210}, {50, 10, 80}},     /* Shade: shadow */
    {{255, 255, 255}, {255, 245, 190}, {240, 200, 90}},  /* Lumina: light */
};

/* Facing byte +0x10 (0 up, 1 down, 2 right, 0x82 left) as a step. */
static inline void som_bolt_dir(unsigned facing, int *dx, int *dy) {
    *dx = facing == 0x02u ? 1 : facing == 0x82u ? -1 : 0;
    *dy = facing == 0x01u ? 1 : (facing == 0x02u || facing == 0x82u) ? 0 : -1;
}

/* Throw a ball from (x, y) along the facing. Its slot + 1, or 0 when none
 * was free. */
static inline int som_bolt_fire(SomBolt b[SOM_BOLTS], unsigned who, unsigned element,
                                int x, int y, unsigned facing, unsigned damage,
                                unsigned map, unsigned long frame) {
    for (unsigned i = 0; i < SOM_BOLTS; i++) {
        if (b[i].active) continue;
        SomBolt *o = &b[i];
        som_bolt_dir(facing, &o->dx, &o->dy);
        o->x = x + o->dx * SOM_BOLT_LEAD;
        o->y = y + o->dy * SOM_BOLT_LEAD;
        o->who = who; o->element = element % SOM_BOLT_ELEMENTS;
        o->damage = damage; o->map = map;
        o->start = frame ? frame : 1u; o->burst = 0; o->active = 1; o->big = 0; o->kind = SOM_BOLT_BALL;
        return (int)i + 1;
    }
    return 0;
}

/* A sparkle where it stands (map pixels): the heal's splash, no flight. */
static inline int som_bolt_sparkle(SomBolt b[SOM_BOLTS], unsigned who, unsigned element,
                                   int x, int y, unsigned map, unsigned long frame) {
    for (unsigned i = 0; i < SOM_BOLTS; i++) {
        if (b[i].active) continue;
        SomBolt *o = &b[i];
        o->dx = o->dy = 0; o->x = x; o->y = y;
        o->who = who; o->element = element % SOM_BOLT_ELEMENTS;
        o->damage = 0; o->map = map; o->big = 1; o->kind = SOM_BOLT_BALL;
        o->start = o->burst = frame ? frame : 1u; o->active = 1;
        return 1;
    }
    return 0;
}

/* Y heals with these spells (Remedy, Cure Water, Revivifier); every other
 * Y spell is a ball. */
static inline int som_bolt_heal_spell(unsigned spell) {
    return spell == 0x0Au || spell == 0x0Bu || spell == 0x21u;
}

/* The game's monster objects: slots 3-5 (7E:E600 + 0x200 each). */
#define SOM_BOLT_MONSTER_BASE 0xE600u
#define SOM_BOLT_OBJECT 0x200u
static inline unsigned som_bolt_u16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static inline int som_bolt_monster_alive(const uint8_t *o) {
    return o[0] == 1 && o[0x180] < 0x80 && !(som_bolt_u16(o + 0x190) & 0x8000u);
}

/* One frame of one ball: fly, then hit the first live monster it touches
 * (not `skip`, the familiar). Returns the slot hit, or -1. A ball that hits
 * or reaches its range bursts; the burst ends the ball. */
static inline int som_bolt_step(SomBolt *o, uint8_t *ram, unsigned map, int skip,
                                unsigned long frame) {
    if (!o->active) return -1;
    if (o->map != map) { o->active = 0; return -1; }
    if (o->burst) {
        if (frame - o->burst >= SOM_BOLT_BURST_FRAMES) o->active = 0;
        return -1;
    }
    if (o->kind >= SOM_BOLT_RIBBON) {          /* a picture only */
        if (frame - o->start >= SOM_SHOW_FRAMES) o->active = 0;
        return -1;
    }
    const int petal = o->kind == SOM_BOLT_PETAL;
    if (frame - o->start >= (petal ? SOM_PETAL_FRAMES : SOM_BOLT_FRAMES)) { o->burst = frame; return -1; }
    o->x += o->dx * (petal ? SOM_PETAL_SPEED : SOM_BOLT_SPEED);
    o->y += o->dy * (petal ? SOM_PETAL_SPEED : SOM_BOLT_SPEED);
    for (int slot = 0; slot < 3; slot++) {
        if (slot == skip) continue;
        uint8_t *m = ram + SOM_BOLT_MONSTER_BASE + (unsigned)slot * SOM_BOLT_OBJECT;
        if (!som_bolt_monster_alive(m)) continue;
        if (abs((int)som_bolt_u16(m + 2) - o->x) > SOM_BOLT_HIT_X ||
            abs((int)som_bolt_u16(m + 4) - o->y) > SOM_BOLT_HIT_Y) continue;
        const unsigned pending = m[0x1F1] + o->damage;
        m[0x1F1] = (uint8_t)(pending > 0xFFu ? 0xFFu : pending);
        o->burst = frame;
        return slot;
    }
    return -1;
}

static inline int som_bolts_active(const SomBolt b[SOM_BOLTS]) {
    for (unsigned i = 0; i < SOM_BOLTS; i++) if (b[i].active) return 1;
    return 0;
}

static inline void som_bolt_dot(uint8_t *pixels, unsigned width, unsigned height, size_t pitch,
                                int format, int x, int y, int r, uint32_t c) {
    const unsigned sx = width >= 512 ? 2u : 1u, sy = height >= 400 ? 2u : 1u;
    for (int oy = -r; oy <= r; oy++) for (int ox = -r; ox <= r; ox++) {
        if (ox * ox + oy * oy > r * r + 1) continue;
        if (x + ox < 0 || y + oy < 0 || x + ox >= (int)(width / sx) || y + oy >= (int)(height / sy)) continue;
        som_overlay_fill(pixels, pitch, format, (unsigned)(x + ox) * sx, (unsigned)(y + oy) * sy, sx, sy, c);
    }
}
/* Eight compass steps, scaled by 8 (a turn without sin). */
static const int8_t som_bolt_ring[8][2] = {{8,0},{6,6},{0,8},{-6,6},{-8,0},{-6,-6},{0,-8},{6,-6}};
/* Paint one ball centred on base pixel (cx, cy): a glowing orb with a tail
 * behind it, or its splash (age frames into the burst). */
static inline void som_bolt_draw(uint8_t *pixels, unsigned width, unsigned height,
                                 size_t pitch, int format, const SomBolt *o,
                                 int cx, int cy, unsigned long frame) {
    if (!pixels || !o->active) return;
    const unsigned sx = width >= 512 ? 2u : 1u, sy = height >= 400 ? 2u : 1u;
    const int base_w = (int)(width / sx), base_h = (int)(height / sy);
    const uint8_t (*rgb)[3] = som_bolt_rgb[o->element % SOM_BOLT_ELEMENTS];
    const uint32_t core = som_overlay_color(format, rgb[0][0], rgb[0][1], rgb[0][2]);
    const uint32_t glow = som_overlay_color(format, rgb[1][0], rgb[1][1], rgb[1][2]);
    const uint32_t rim = som_overlay_color(format, rgb[2][0], rgb[2][1], rgb[2][2]);
    const unsigned long age = frame - (o->burst ? o->burst : o->start);
    if (o->kind == SOM_BOLT_PETAL) {    /* five petals round a pale heart; it opens on a hit */
        const uint32_t pink = som_overlay_color(format, 255, 170, 230);
        const uint32_t pale = som_overlay_color(format, 255, 236, 250);
        const uint32_t deep = som_overlay_color(format, 176, 92, 210);
        const int spread = o->burst ? 5 + (int)age : 4;
        for (unsigned k = 0; k < 8u; k++) {
            const int8_t *v = som_bolt_ring[(k + age / 4u) & 7u];
            som_bolt_dot(pixels, width, height, pitch, format, cx + v[0] * spread / 8,
                         cy + v[1] * spread / 8 + (o->burst ? 0 : (int)((age / 6u) & 1u)),
                         o->burst ? 1 : 2, (k & 1u) ? pink : deep);
        }
        if (!o->burst) som_bolt_dot(pixels, width, height, pitch, format, cx, cy, 2, pale);
        return;
    }
    if (o->kind == SOM_BOLT_RIBBON) {   /* a ribbon swept through the half circle in front */
        const uint32_t rose = som_overlay_color(format, 255, 96, 160);
        const uint32_t light = som_overlay_color(format, 255, 214, 232);
        for (int t = 0; t < 22; t++) {
            const int a = (int)age * 3 - t;
            if (a < 0 || a > 40) continue;
            const int along = 18 + ((a * 7) & 7), across = (a - 20) * 2;
            const int lift = 20 - abs(a - 20);
            const int x = cx + (o->dx ? o->dx * (along + lift) : across);
            const int y = cy + (o->dx ? across / 2 : o->dy * (along + lift / 2));
            som_bolt_dot(pixels, width, height, pitch, format, x, y, t < 6 ? 2 : 1, t & 1 ? light : rose);
        }
        return;
    }
    if (o->kind == SOM_BOLT_TOUCH) {    /* rising sparks round the healed (or judged) one */
        const uint32_t gold = som_overlay_color(format, 255, 236, 150);
        const uint32_t white = som_overlay_color(format, 255, 255, 255);
        for (unsigned k = 0; k < 8u; k++) {
            const int8_t *v = som_bolt_ring[(k + age / 3u) & 7u];
            som_bolt_dot(pixels, width, height, pitch, format, cx + v[0] * 11 / 8,
                         cy + v[1] * 5 / 8 - (int)age, (k & 1u) ? 1 : 0, (k & 1u) ? gold : white);
        }
        return;
    }
    if (o->burst) {                     /* splash: a ring that grows and thins */
        const int r = 4 + (int)age + (o->big ? 3 : 0);
        for (int oy = -r; oy <= r; oy++) for (int ox = -r; ox <= r; ox++) {
            const int d2 = ox * ox + oy * oy;
            if (d2 > r * r || d2 < (r - 3) * (r - 3)) continue;
            if ((unsigned)((ox * 5 + oy * 3 + (int)age) & 3) < age / 4u) continue;
            const int x = cx + ox, y = cy + oy;
            if (x < 0 || y < 0 || x >= base_w || y >= base_h) continue;
            som_overlay_fill(pixels, pitch, format, (unsigned)x * sx, (unsigned)y * sy,
                             sx, sy, d2 > (r - 1) * (r - 1) ? rim : glow);
        }
        return;
    }
    /* Tail: three shrinking puffs behind the ball. */
    for (int t = 3; t >= 0; t--) {
        const int r = (t ? 4 - t : 5 + (int)(age & 1)) + (o->big ? 2 : 0);
        const int bx = cx - o->dx * t * 5, by = cy - o->dy * t * 5;
        for (int oy = -r; oy <= r; oy++) for (int ox = -r; ox <= r; ox++) {
            const int d2 = ox * ox + oy * oy;
            if (d2 > r * r) continue;
            const int x = bx + ox, y = by + oy;
            if (x < 0 || y < 0 || x >= base_w || y >= base_h) continue;
            uint32_t c = rim;
            if (!t) c = d2 * 5 < r * r ? core : d2 * 3 < r * r * 2 ? glow : rim;
            else if (t == 1 && d2 * 2 < r * r) c = glow;
            som_overlay_fill(pixels, pitch, format, (unsigned)x * sx, (unsigned)y * sy,
                             sx, sy, c);
        }
    }
}
#endif
