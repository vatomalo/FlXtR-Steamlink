/* Gambit menu for the Secret of Mana AI, driven from the gamepad.
 *
 * While the AI plays, the player's buttons no longer take control away or
 * reach the game: any button opens this menu (drawn over the game, the AI
 * keeps playing). Gambits are the AI's activities, each with a priority
 * weight 0..SOM_GAMBIT_MAX_WEIGHT (0 = never). Up/Down move, Left/Right
 * lower/raise the highlighted weight (B also raises it, wrapping to 0), A
 * closes the menu. The last entry, "Exit gambits: you play", closes the
 * menu, turns the AI off and gives the controls back.
 *
 * The SkyLoft bridge reads the weights from SOM_GAMBIT_FILE (one digit per
 * gambit, this order) and weighs the activities against each other.
 * Pure model and drawing, no I/O. */
#ifndef FLXTR_SOM_GAMBIT_H
#define FLXTR_SOM_GAMBIT_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "som_overlay.h"

#define SOM_GAMBIT_COUNT 6u
#define SOM_GAMBIT_ITEMS (SOM_GAMBIT_COUNT + 1u)       /* + exit */
#define SOM_GAMBIT_MAX_WEIGHT 5u

/* Activities only; how the AI fights or moves is not a gambit. The order is
 * the bridge's GAMBITS order (tools/som_qwen_bridge.py). */
static const char *const som_gambit_names[SOM_GAMBIT_COUNT] = {
    "Fight monsters",
    "Rest at the inn",
    "Buy Candy at shop",
    "Save after resting",
    "Use Candy when hurt",
    "Talk to villagers",
};
static const uint8_t som_gambit_defaults[SOM_GAMBIT_COUNT] = {4, 3, 3, 3, 4, 1};

typedef struct {
    int open;
    unsigned cursor;
    uint8_t weight[SOM_GAMBIT_COUNT];
} SomGambits;

enum { SOM_GAMBIT_NONE, SOM_GAMBIT_OPENED, SOM_GAMBIT_CLOSED,
       SOM_GAMBIT_CHANGED, SOM_GAMBIT_EXIT };

static inline void som_gambit_defaults_into(SomGambits *g) {
    memcpy(g->weight, som_gambit_defaults, sizeof(g->weight));
}

/* "433341\n"-style text; invalid text keeps the defaults. */
static inline void som_gambit_parse(SomGambits *g, const char *text) {
    som_gambit_defaults_into(g);
    if (!text) return;
    for (unsigned i = 0; i < SOM_GAMBIT_COUNT; i++)
        if (text[i] < '0' || text[i] > (char)('0' + SOM_GAMBIT_MAX_WEIGHT)) {
            som_gambit_defaults_into(g);
            return;
        }
    for (unsigned i = 0; i < SOM_GAMBIT_COUNT; i++) g->weight[i] = (uint8_t)(text[i] - '0');
}

static inline void som_gambit_format(const SomGambits *g, char out[SOM_GAMBIT_COUNT + 1]) {
    for (unsigned i = 0; i < SOM_GAMBIT_COUNT; i++) out[i] = (char)('0' + g->weight[i]);
    out[SOM_GAMBIT_COUNT] = 0;
}

/* pressed: Libretro joypad ids that went down this frame (bit per id). */
static inline int som_gambit_press(SomGambits *g, uint16_t pressed) {
    if (!pressed) return SOM_GAMBIT_NONE;
    if (!g->open) {
        g->open = 1;
        g->cursor = 0;
        return SOM_GAMBIT_OPENED;
    }
    if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_UP))
        g->cursor = (g->cursor + SOM_GAMBIT_ITEMS - 1u) % SOM_GAMBIT_ITEMS;
    if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_DOWN))
        g->cursor = (g->cursor + 1u) % SOM_GAMBIT_ITEMS;
    if (g->cursor == SOM_GAMBIT_COUNT) {
        if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_B)) {
            g->open = 0;
            return SOM_GAMBIT_EXIT;
        }
    } else {
        uint8_t *w = &g->weight[g->cursor];
        const uint8_t before = *w;
        if ((pressed & (1u << RETRO_DEVICE_ID_JOYPAD_LEFT)) && *w > 0) (*w)--;
        if ((pressed & (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT)) && *w < SOM_GAMBIT_MAX_WEIGHT) (*w)++;
        if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_B))
            *w = (uint8_t)((*w + 1u) % (SOM_GAMBIT_MAX_WEIGHT + 1u));
        if (*w != before) return SOM_GAMBIT_CHANGED;
    }
    if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_A)) {
        g->open = 0;
        return SOM_GAMBIT_CLOSED;
    }
    return SOM_GAMBIT_NONE;
}

/* Text of menu line `line` (0 = title) into out[33]; returns 0 past the end. */
static inline int som_gambit_line(const SomGambits *g, unsigned line, char out[33]) {
    if (line == 0) {
        snprintf(out, 33, "GAMBITS  </>:priority A:close");
        return 1;
    }
    unsigned item = line - 1u;
    if (item >= SOM_GAMBIT_ITEMS) return 0;
    const char *mark = g->cursor == item ? ">" : " ";
    if (item == SOM_GAMBIT_COUNT) {
        snprintf(out, 33, "%s Exit gambits: you play", mark);
        return 1;
    }
    char bar[SOM_GAMBIT_MAX_WEIGHT + 1];
    for (unsigned i = 0; i < SOM_GAMBIT_MAX_WEIGHT; i++) bar[i] = i < g->weight[item] ? 'o' : '.';
    bar[SOM_GAMBIT_MAX_WEIGHT] = 0;
    snprintf(out, 33, "%s %-19s %u %s", mark, som_gambit_names[item],
             (unsigned)g->weight[item], bar);
    return 1;
}

static inline void som_gambit_draw(uint8_t *pixels, unsigned width, unsigned height,
                                   size_t pitch, int format, const SomGambits *g) {
    if (!g->open || !pixels) return;
    const unsigned sx = width >= 512 ? 2u : 1u, sy = height >= 400 ? 2u : 1u;
    const unsigned lines = SOM_GAMBIT_ITEMS + 1u, columns = 31u;
    const unsigned box_w = (columns * 6u + 9u) * sx, box_h = (lines * 10u + 7u) * sy;
    if (box_w + 4u * sx > width || box_h + 4u * sy > height) return;
    const unsigned x0 = 6u * sx, y0 = 6u * sy;
    som_overlay_fill(pixels, pitch, format, x0, y0, box_w, box_h,
                     som_overlay_color(format, 248, 248, 248));
    som_overlay_fill(pixels, pitch, format, x0 + sx, y0 + sy, box_w - 2u * sx, box_h - 2u * sy,
                     som_overlay_color(format, 16, 24, 96));
    const uint32_t ink = som_overlay_color(format, 255, 255, 255);
    const uint32_t dim = som_overlay_color(format, 150, 160, 200);
    char text[33];
    for (unsigned line = 0; line < lines && som_gambit_line(g, line, text); line++) {
        const int off = line >= 1u && line - 1u < SOM_GAMBIT_COUNT && !g->weight[line - 1u];
        for (unsigned i = 0; text[i]; i++) {
            unsigned char c = (unsigned char)text[i];
            if (c < 32u || c > 126u) continue;
            const uint8_t *glyph = som_overlay_font[c - 32u];
            for (unsigned row = 0; row < 7u; row++)
                for (unsigned col = 0; col < 5u; col++) {
                    if (!(glyph[row] & (0x10u >> col))) continue;
                    som_overlay_fill(pixels, pitch, format, x0 + (5u + i * 6u + col) * sx,
                                     y0 + (4u + line * 10u + row) * sy, sx, sy,
                                     off ? dim : ink);
                }
        }
    }
}

#endif
