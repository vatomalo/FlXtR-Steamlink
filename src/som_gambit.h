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
 * "Infinite MP (test)" above Exit is a testing switch, not a gambit: B or
 * Left/Right toggles it. It is not written to SOM_GAMBIT_FILE (the bridge
 * reads that) and the proxy resets it on every game load.
 *
 * The SkyLoft bridge reads the weights from SOM_GAMBIT_FILE (one digit per
 * gambit, this order) and weighs the activities against each other.
 *
 * Three pages (L1/R1 in the proxy): ACTIONS (the activities above),
 * COMBAT, how the AI fights: one weight per strategy, saved separately as
 * SOM_COMBAT_COUNT digits (the bridge's COMBAT order), so the actions file
 * keeps its six-digit format, and QUEST: advance the story and level up
 * (SOM_QUEST_COUNT digits, the bridge's QUEST order, tools/som_quest.py).
 * Pure model and drawing, no I/O. */
#ifndef FLXTR_SOM_GAMBIT_H
#define FLXTR_SOM_GAMBIT_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "som_overlay.h"

#define SOM_GAMBIT_COUNT 6u
#define SOM_MUSIC_GENRE_COUNT 9u
static const char *const som_music_genre_names[SOM_MUSIC_GENRE_COUNT] = {
    "Lo-Fi", "Jazz", "City Pop", "Orchestral", "Piano",
    "Synthwave", "Rock", "Ambient", "Off"
};
/* The AI's brain on SkyLoft: the Ollama model its planner talks to and the
 * critic (a second model that judges the play and coaches it). Saved as
 * "<model index> <critic 0/1>" to SOM_BRAIN_FILE; the bridge downloads a
 * model that SkyLoft does not have yet. Order = the bridge's AI_MODELS
 * (tools/som_qwen_bridge.py; a test keeps the two lists equal). */
#define SOM_AI_MODEL_COUNT 15u
static const char *const som_ai_model_names[SOM_AI_MODEL_COUNT] = {
    "Qwen3 0.6B", "Qwen3 1.7B", "Qwen3 4B", "Qwen3.5 4B", "Qwen2.5 3B",
    "StarCoder2 3B", "Llama3.2 1B", "Llama3.2 3B", "Gemma3 1B", "Gemma3 4B",
    "Phi-4 mini", "SmolLM2 1.7B", "DeepSeek-R1 1.5B", "Mistral 7B", "Granite3.3 2B"
};
#define SOM_GAMBIT_MODEL_ITEM SOM_GAMBIT_COUNT
#define SOM_GAMBIT_CRITIC_ITEM (SOM_GAMBIT_COUNT + 1u)
#define SOM_GAMBIT_MUSIC_ITEM (SOM_GAMBIT_COUNT + 2u)
#define SOM_GAMBIT_INFINITE_MP (SOM_GAMBIT_COUNT + 3u) /* test switch row */
#define SOM_GAMBIT_DEBUG_ITEM (SOM_GAMBIT_COUNT + 4u)
#define SOM_GAMBIT_P2_ITEM (SOM_GAMBIT_COUNT + 5u)     /* the AI plays player 2 */
#define SOM_GAMBIT_WHACK_ITEM (SOM_GAMBIT_COUNT + 6u)  /* the whacked camera */
#define SOM_GAMBIT_EXIT_ITEM (SOM_GAMBIT_COUNT + 7u)
#define SOM_GAMBIT_ITEMS (SOM_GAMBIT_COUNT + 8u)       /* + model, critic, music, test, debug, p2, exit */
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

/* Fighting strategies, page 2. Order = the bridge's COMBAT order. */
#define SOM_COMBAT_COUNT 8u
#define SOM_COMBAT_EXIT_ITEM SOM_COMBAT_COUNT
#define SOM_COMBAT_ITEMS (SOM_COMBAT_COUNT + 1u)          /* + exit */
static const char *const som_combat_names[SOM_COMBAT_COUNT] = {
    "Press the attack",
    "Keep distance",
    "Guard and counter",
    "Evade (dodge roll)",
    "Charged attacks",
    "Cast magic",
    "Switch classes",
    "Switch weapons",
};
static const uint8_t som_combat_defaults[SOM_COMBAT_COUNT] = {3, 2, 2, 2, 3, 3, 3, 3};
/* Quest page: what the AI's trips are for. Order = the bridge's QUEST order. */
#define SOM_QUEST_COUNT 2u
#define SOM_QUEST_EXIT_ITEM SOM_QUEST_COUNT
#define SOM_QUEST_ITEMS (SOM_QUEST_COUNT + 1u)            /* + exit */
static const char *const som_quest_names[SOM_QUEST_COUNT] = {
    "Advance the story",
    "Level up",
};
static const uint8_t som_quest_defaults[SOM_QUEST_COUNT] = {3, 3};
enum { SOM_PAGE_ACTIONS, SOM_PAGE_COMBAT, SOM_PAGE_QUEST, SOM_GAMBIT_PAGES };

typedef struct {
    int open;
    unsigned cursor;
    uint8_t weight[SOM_GAMBIT_COUNT];
    int infinite_mp;                                /* test switch, not saved */
    unsigned page;                                  /* SOM_PAGE_* */
    uint8_t combat[SOM_COMBAT_COUNT];
    uint8_t quest[SOM_QUEST_COUNT];
    unsigned music_genre; /* stored separately from the six AI priority digits */
    unsigned ai_model;    /* index into som_ai_model_names */
    int critic;           /* the critic model judges and coaches the play */
    int debug_messages;   /* AI status/debug overlays and speech bubbles */
    int player2;          /* the AI plays player 2, saved */
    int no_whack;         /* the whacked camera is off, saved */
    int resting;          /* opened from the ring while the player plays */
} SomGambits;

enum { SOM_GAMBIT_NONE, SOM_GAMBIT_OPENED, SOM_GAMBIT_CLOSED,
       SOM_GAMBIT_CHANGED, SOM_GAMBIT_EXIT, SOM_GAMBIT_TEST_SWITCH,
       SOM_GAMBIT_GENRE_CHANGED, SOM_GAMBIT_BRAIN_CHANGED, SOM_GAMBIT_DEBUG_CHANGED,
       SOM_GAMBIT_P2_CHANGED, SOM_GAMBIT_WHACK_CHANGED };

/* "<model> <critic>\n"; invalid text keeps model 0 with the critic on. */
static inline void som_brain_parse(SomGambits *g, const char *text) {
    unsigned model = 0; int critic = 1;
    g->ai_model = 0; g->critic = 1;
    if (text && sscanf(text, "%u %d", &model, &critic) == 2 && model < SOM_AI_MODEL_COUNT &&
        (critic == 0 || critic == 1)) {
        g->ai_model = model; g->critic = critic;
    }
}
static inline void som_brain_format(const SomGambits *g, char out[16]) {
    snprintf(out, 16, "%u %d", g->ai_model % SOM_AI_MODEL_COUNT, g->critic ? 1 : 0);
}

static inline void som_gambit_defaults_into(SomGambits *g) {
    memcpy(g->weight, som_gambit_defaults, sizeof(g->weight));
    g->music_genre = 0; /* Lo-Fi */
    g->debug_messages = 0;
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

/* Combat page digits, same rules: invalid text keeps the defaults. */
static inline void som_combat_parse(SomGambits *g, const char *text) {
    memcpy(g->combat, som_combat_defaults, sizeof(g->combat));
    if (!text) return;
    for (unsigned i = 0; i < SOM_COMBAT_COUNT; i++)
        if (text[i] < '0' || text[i] > (char)('0' + SOM_GAMBIT_MAX_WEIGHT)) return;
    for (unsigned i = 0; i < SOM_COMBAT_COUNT; i++) g->combat[i] = (uint8_t)(text[i] - '0');
}

static inline void som_combat_format(const SomGambits *g, char out[SOM_COMBAT_COUNT + 1]) {
    for (unsigned i = 0; i < SOM_COMBAT_COUNT; i++) out[i] = (char)('0' + g->combat[i]);
    out[SOM_COMBAT_COUNT] = 0;
}

/* Quest page digits, same rules: invalid text keeps the defaults. */
static inline void som_quest_parse(SomGambits *g, const char *text) {
    memcpy(g->quest, som_quest_defaults, sizeof(g->quest));
    if (!text) return;
    for (unsigned i = 0; i < SOM_QUEST_COUNT; i++)
        if (text[i] < '0' || text[i] > (char)('0' + SOM_GAMBIT_MAX_WEIGHT)) return;
    for (unsigned i = 0; i < SOM_QUEST_COUNT; i++) g->quest[i] = (uint8_t)(text[i] - '0');
}

static inline void som_quest_format(const SomGambits *g, char out[SOM_QUEST_COUNT + 1]) {
    for (unsigned i = 0; i < SOM_QUEST_COUNT; i++) out[i] = (char)('0' + g->quest[i]);
    out[SOM_QUEST_COUNT] = 0;
}

/* A weights-only page (COMBAT, QUEST): its names, weights and size. */
static inline unsigned som_page_weights(const SomGambits *g, const char *const **names,
                                        uint8_t **weights) {
    if (g->page == SOM_PAGE_QUEST) {
        *names = som_quest_names; *weights = (uint8_t *)g->quest; return SOM_QUEST_COUNT;
    }
    *names = som_combat_names; *weights = (uint8_t *)g->combat; return SOM_COMBAT_COUNT;
}

/* L1/R1: next or previous page; the cursor starts at the top. */
static inline void som_gambit_turn_page(SomGambits *g, int forward) {
    g->page = (g->page + (forward ? 1u : SOM_GAMBIT_PAGES - 1u)) % SOM_GAMBIT_PAGES;
    g->cursor = 0;
}

static inline int som_combat_press(SomGambits *g, uint16_t pressed) {
    const char *const *names = NULL; uint8_t *weights = NULL;
    const unsigned count = som_page_weights(g, &names, &weights), items = count + 1u;
    (void)names;
    if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_UP))
        g->cursor = (g->cursor + items - 1u) % items;
    if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_DOWN))
        g->cursor = (g->cursor + 1u) % items;
    if (g->cursor == count) {
        if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_B)) {
            g->open = 0;
            return SOM_GAMBIT_EXIT;
        }
    } else {
        uint8_t *w = &weights[g->cursor];
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

/* pressed: Libretro joypad ids that went down this frame (bit per id). */
static inline int som_gambit_press(SomGambits *g, uint16_t pressed) {
    if (!pressed) return SOM_GAMBIT_NONE;
    if (!g->open) {
        g->open = 1;
        g->cursor = 0;
        return SOM_GAMBIT_OPENED;
    }
    if (g->page == SOM_PAGE_COMBAT || g->page == SOM_PAGE_QUEST)
        return som_combat_press(g, pressed);
    if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_UP))
        g->cursor = (g->cursor + SOM_GAMBIT_ITEMS - 1u) % SOM_GAMBIT_ITEMS;
    if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_DOWN))
        g->cursor = (g->cursor + 1u) % SOM_GAMBIT_ITEMS;
    if (g->cursor == SOM_GAMBIT_MODEL_ITEM) {
        const unsigned before = g->ai_model;
        if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_LEFT))
            g->ai_model = (g->ai_model + SOM_AI_MODEL_COUNT - 1u) % SOM_AI_MODEL_COUNT;
        if (pressed & ((1u << RETRO_DEVICE_ID_JOYPAD_RIGHT) | (1u << RETRO_DEVICE_ID_JOYPAD_B)))
            g->ai_model = (g->ai_model + 1u) % SOM_AI_MODEL_COUNT;
        if (before != g->ai_model) return SOM_GAMBIT_BRAIN_CHANGED;
    } else if (g->cursor == SOM_GAMBIT_CRITIC_ITEM) {
        if (pressed & ((1u << RETRO_DEVICE_ID_JOYPAD_B) | (1u << RETRO_DEVICE_ID_JOYPAD_LEFT) |
                       (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT))) {
            g->critic = !g->critic;
            return SOM_GAMBIT_BRAIN_CHANGED;
        }
    } else if (g->cursor == SOM_GAMBIT_MUSIC_ITEM) {
        unsigned before = g->music_genre;
        if (pressed & (1u << RETRO_DEVICE_ID_JOYPAD_LEFT))
            g->music_genre = (g->music_genre + SOM_MUSIC_GENRE_COUNT - 1u) % SOM_MUSIC_GENRE_COUNT;
        if (pressed & ((1u << RETRO_DEVICE_ID_JOYPAD_RIGHT) | (1u << RETRO_DEVICE_ID_JOYPAD_B)))
            g->music_genre = (g->music_genre + 1u) % SOM_MUSIC_GENRE_COUNT;
        if (before != g->music_genre) return SOM_GAMBIT_GENRE_CHANGED;
    } else if (g->cursor == SOM_GAMBIT_INFINITE_MP) {
        if (pressed & ((1u << RETRO_DEVICE_ID_JOYPAD_B) | (1u << RETRO_DEVICE_ID_JOYPAD_LEFT) |
                       (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT))) {
            g->infinite_mp = !g->infinite_mp;
            return SOM_GAMBIT_TEST_SWITCH;
        }
    } else if (g->cursor == SOM_GAMBIT_DEBUG_ITEM) {
        if (pressed & ((1u << RETRO_DEVICE_ID_JOYPAD_B) | (1u << RETRO_DEVICE_ID_JOYPAD_LEFT) |
                       (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT))) {
            g->debug_messages = !g->debug_messages;
            return SOM_GAMBIT_DEBUG_CHANGED;
        }
    } else if (g->cursor == SOM_GAMBIT_P2_ITEM) {
        if (pressed & ((1u << RETRO_DEVICE_ID_JOYPAD_B) | (1u << RETRO_DEVICE_ID_JOYPAD_LEFT) |
                       (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT))) {
            g->player2 = !g->player2;
            return SOM_GAMBIT_P2_CHANGED;
        }
    } else if (g->cursor == SOM_GAMBIT_WHACK_ITEM) {
        if (pressed & ((1u << RETRO_DEVICE_ID_JOYPAD_B) | (1u << RETRO_DEVICE_ID_JOYPAD_LEFT) |
                       (1u << RETRO_DEVICE_ID_JOYPAD_RIGHT))) {
            g->no_whack = !g->no_whack;
            return SOM_GAMBIT_WHACK_CHANGED;
        }
    } else if (g->cursor == SOM_GAMBIT_EXIT_ITEM) {
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
    if (g->page == SOM_PAGE_COMBAT || g->page == SOM_PAGE_QUEST) {
        const char *const *names = NULL; uint8_t *weights = NULL;
        const unsigned count = som_page_weights(g, &names, &weights);
        if (line == 0) {
            snprintf(out, 33, g->page == SOM_PAGE_QUEST ? "GAMBITS: QUEST     L1/R1:page"
                                                        : "GAMBITS: COMBAT    L1/R1:page");
            return 1;
        }
        const unsigned item = line - 1u;
        if (item > count) return 0;
        const char *mark = g->cursor == item ? ">" : " ";
        if (item == count) {
            snprintf(out, 33, "%s %s", mark, g->resting ? "Start AI: it plays" : "Exit gambits: you play");
            return 1;
        }
        char bar[SOM_GAMBIT_MAX_WEIGHT + 1];
        for (unsigned i = 0; i < SOM_GAMBIT_MAX_WEIGHT; i++) bar[i] = i < weights[item] ? 'o' : '.';
        bar[SOM_GAMBIT_MAX_WEIGHT] = 0;
        snprintf(out, 33, "%s %-19s %u %s", mark, names[item], (unsigned)weights[item], bar);
        return 1;
    }
    if (line == 0) {
        snprintf(out, 33, "GAMBITS: ACTIONS   L1/R1:page");
        return 1;
    }
    unsigned item = line - 1u;
    if (item >= SOM_GAMBIT_ITEMS) return 0;
    const char *mark = g->cursor == item ? ">" : " ";
    if (item == SOM_GAMBIT_MODEL_ITEM) {
        snprintf(out, 33, "%s AI model: %s", mark,
                 som_ai_model_names[g->ai_model % SOM_AI_MODEL_COUNT]);
        return 1;
    }
    if (item == SOM_GAMBIT_CRITIC_ITEM) {
        snprintf(out, 33, "%s %-19s %s", mark, "Critic", g->critic ? "ON" : "off");
        return 1;
    }
    if (item == SOM_GAMBIT_MUSIC_ITEM) {
        snprintf(out, 33, "%s Music genre: %s", mark,
                 som_music_genre_names[g->music_genre % SOM_MUSIC_GENRE_COUNT]);
        return 1;
    }
    if (item == SOM_GAMBIT_INFINITE_MP) {
        snprintf(out, 33, "%s %-19s %s", mark, "Infinite MP (test)", g->infinite_mp ? "ON" : "off");
        return 1;
    }
    if (item == SOM_GAMBIT_DEBUG_ITEM) {
        snprintf(out, 33, "%s %-19s %s", mark, "AI debug messages", g->debug_messages ? "ON" : "off");
        return 1;
    }
    if (item == SOM_GAMBIT_P2_ITEM) {
        snprintf(out, 33, "%s %-19s %s", mark, "Player 2 AI", g->player2 ? "ON" : "off");
        return 1;
    }
    if (item == SOM_GAMBIT_WHACK_ITEM) {
        snprintf(out, 33, "%s %-19s %s", mark, "Whacked camera", g->no_whack ? "off" : "ON");
        return 1;
    }
    if (item == SOM_GAMBIT_EXIT_ITEM) {
        snprintf(out, 33, "%s %s", mark, g->resting ? "Start AI: it plays" : "Exit gambits: you play");
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
    const char *const *page_names = NULL; uint8_t *page_weights = NULL;
    const int weights_page = g->page == SOM_PAGE_COMBAT || g->page == SOM_PAGE_QUEST;
    const unsigned page_count = weights_page ? som_page_weights(g, &page_names, &page_weights) : 0u;
    (void)page_names;
    const unsigned lines = (weights_page ? page_count + 1u : SOM_GAMBIT_ITEMS) + 1u,
                   columns = 31u;
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
        const int off = line >= 1u && (weights_page ?
            (line - 1u < page_count && !page_weights[line - 1u]) :
            ((line - 1u < SOM_GAMBIT_COUNT && !g->weight[line - 1u]) ||
             (line - 1u == SOM_GAMBIT_MUSIC_ITEM &&
              g->music_genre == SOM_MUSIC_GENRE_COUNT - 1u) ||
             (line - 1u == SOM_GAMBIT_INFINITE_MP && !g->infinite_mp) ||
             (line - 1u == SOM_GAMBIT_DEBUG_ITEM && !g->debug_messages) ||
             (line - 1u == SOM_GAMBIT_P2_ITEM && !g->player2) ||
             (line - 1u == SOM_GAMBIT_WHACK_ITEM && g->no_whack) ||
             (line - 1u == SOM_GAMBIT_CRITIC_ITEM && !g->critic)));
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
