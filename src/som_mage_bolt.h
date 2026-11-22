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

typedef struct {
    int active, x, y, dx, dy;     /* x, y: map pixels of the ball (the hero's feet frame) */
    unsigned who, element, damage, map;
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

/* Throw a ball from (x, y) along the facing. 1 when a slot was free. */
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
        o->start = frame ? frame : 1u; o->burst = 0; o->active = 1;
        return 1;
    }
    return 0;
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
    if (frame - o->start >= SOM_BOLT_FRAMES) { o->burst = frame; return -1; }
    o->x += o->dx * SOM_BOLT_SPEED;
    o->y += o->dy * SOM_BOLT_SPEED;
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
    if (o->burst) {                     /* splash: a ring that grows and thins */
        const int r = 4 + (int)age;
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
        const int r = t ? 4 - t : 5 + (int)(age & 1);
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
