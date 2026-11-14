/* Gambit menu for the Secret of Mana AI, driven from the gamepad.
 *
 * While the AI plays, the player's buttons no longer take control away or
 * reach the game: any button opens this menu (drawn over the game, the AI
 * keeps playing). Up/Down move, B switches the highlighted gambit on or off,
 * A closes the menu. The last entry, "Exit gambits: you play", closes the
 * menu, turns the AI off and gives the controls back.
 *
 * Gambits are the AI's behaviours; the SkyLoft bridge reads the enabled mask
 * from SOM_GAMBIT_FILE and applies it. Pure model and drawing, no I/O. */
#ifndef FLXTR_SOM_GAMBIT_H
#define FLXTR_SOM_GAMBIT_H

#include <stdint.h>
#include <stdio.h>
#include "som_overlay.h"

#define SOM_GAMBIT_COUNT 6u
#define SOM_GAMBIT_ITEMS (SOM_GAMBIT_COUNT + 1u)       /* + exit */
#define SOM_GAMBIT_ALL ((uint16_t)((1u << SOM_GAMBIT_COUNT) - 1u))

/* Activities only; how the AI fights or moves is not a gambit. The bit
 * order is the SkyLoft bridge's GAMBITS order (tools/som_qwen_bridge.py). */
static const char *const som_gambit_names[SOM_GAMBIT_COUNT] = {
    "Fight monsters",
    "Rest at the inn",
    "Buy Candy at the shop",
    "Save after resting",
    "Use Candy when hurt",
    "Talk to villagers",
};

typedef struct {
    int open;
    unsigned cursor;
    uint16_t enabled;
} SomGambits;

enum { SOM_GAMBIT_NONE, SOM_GAMBIT_OPENED, SOM_GAMBIT_CLOSED,
       SOM_GAMBIT_CHANGED, SOM_GAMBIT_EXIT };

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
    if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_B)) {
        if (g->cursor == SOM_GAMBIT_COUNT) {
            g->open = 0;
            return SOM_GAMBIT_EXIT;
        }
        g->enabled ^= (uint16_t)(1u << g->cursor);
        return SOM_GAMBIT_CHANGED;
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
        snprintf(out, 33, "GAMBITS   B:on/off  A:close");
        return 1;
    }
    unsigned item = line - 1u;
    if (item >= SOM_GAMBIT_ITEMS) return 0;
    const char *mark = g->cursor == item ? ">" : " ";
    if (item == SOM_GAMBIT_COUNT)
        snprintf(out, 33, "%s     Exit gambits: you play", mark);
    else
        snprintf(out, 33, "%s %s %s", mark,
                 (g->enabled >> item) & 1u ? "ON" : "--", som_gambit_names[item]);
    return 1;
}

static inline void som_gambit_draw(uint8_t *pixels, unsigned width, unsigned height,
                                   size_t pitch, int format, const SomGambits *g) {
    if (!g->open || !pixels) return;
    const unsigned sx = width >= 512 ? 2u : 1u, sy = height >= 400 ? 2u : 1u;
    const unsigned lines = SOM_GAMBIT_ITEMS + 1u, columns = 30u;
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
        const int off = line >= 1u && line - 1u < SOM_GAMBIT_COUNT &&
                        !((g->enabled >> (line - 1u)) & 1u);
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
