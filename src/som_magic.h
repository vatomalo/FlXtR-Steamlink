/* Spells per discipline, and the "seal" input for instant charge levels.
 *
 * Spell ids, names and targets: cidwel/som-decomp docs/rom-combat.md 5.5
 * (row D0:2AD0 + 64*id; byte 8 = 0x81 hostile, 0x01 ally, 0x00 self).
 * The proxy reads byte 8 and the MP cost (byte 15) from the loaded Turbo
 * ROM at cast time; these lists only decide who may cast what.
 * Left out: 0x17 Analyzer (0xA0, a scan, not a cast), 0x20/0x23 (0x40,
 * undecoded), 0x21 Revivifier (revives others; the boy casts on himself).
 * Pure tables and helpers, no I/O. */
#ifndef FLXTR_SOM_MAGIC_H
#define FLXTR_SOM_MAGIC_H
#include <stdint.h>
#include "som_disciplines.h"

#define SOM_SPELL_COUNT 0x2Au
#define SOM_CLASS_SPELLS_MAX 12u
#define SOM_SEALS_MAX 8u          /* charge levels 1..8 */

static const char *const som_spell_names[SOM_SPELL_COUNT] = {
    "Earth Slide", "Gem Missile", "Speed Down", "Stone Saber", "Speed Up", "Defender",
    "Freeze", "Acid Storm", "Energy Absorb", "Ice Saber", "Remedy", "Cure Water",
    "Fire Ball", "Exploder", "Lava Wave", "Flame Saber", "Fire Bouquet", "Blaze Wall",
    "Air Blast", "Thunderbolt", "Silence", "Thunder Saber", "Balloon", "Analyzer",
    "Change Form", "Magic Absorb", "Lunar Magic", "Moon Saber", "Lunar Boost", "Moon Energy",
    "Sleep Flower", "Burst", "Spell 20", "Revivifier", "Wall", "Spell 23",
    "Evil Gate", "Dark Force", "Dispel Magic", "Light Saber", "Lucent Beam", "Lucid Barrier",
};

/* 0xFF ends a list. No discipline: Fire Ball only (the first X spell). */
static const uint8_t som_class_spells[SOM_DISCIPLINE_COUNT][SOM_CLASS_SPELLS_MAX] = {
    /* Swordsmanship: blade enchantments */
    {0x0F, 0x15, 0x09, 0xFF},
    /* Martial Arts: body buffs */
    {0x04, 0x1C, 0x05, 0xFF},
    /* Ninjutsu: elemental jutsu */
    {0x0C, 0x06, 0x12, 0x1E, 0xFF},
    /* Magic: the attack spells of every spirit */
    {0x0C, 0x06, 0x13, 0x00, 0x01, 0x0D, 0x0E, 0x11, 0x07, 0x1F, 0x28, 0xFF},
    /* Divine Arts: B Lucent Beam, Y Cure Water, X the rest */
    {0x28, 0x0B, 0x0A, 0x29, 0x27, 0x22, 0xFF},
    /* Dark Arts: shade and draining */
    /* B Dark Force, Y Energy Absorb, X the rest */
    {0x25, 0x08, 0x24, 0x26, 0x19, 0x18, 0xFF},
    /* Summoning: moon and forest */
    /* B Burst, Y Sleep Flower, X Lunar Magic and Moon Energy */
    {0x1F, 0x1E, 0x1A, 0x1D, 0xFF},
    /* Support & Dance: buffs and hexes */
    /* B Speed Down, Y Cure Water, X the rest */
    {0x02, 0x0B, 0x04, 0x05, 0x14, 0x16, 0xFF},
    /* Weapon Flow: no magic, X is the third weapon (som_disc_third_weapon) */
    {0xFF},
    /* Roguecraft: hexes */
    {0x02, 0x1E, 0x07, 0x14, 0xFF},
    /* Bushido: no magic, X is Iaijutsu (charge level 3/4 strikes) */
    {0xFF},
    /* Archery: wind and stone shots */
    {0x12, 0x13, 0x01, 0xFF},
};

/* Casters (Magic, Divine Arts, Dark Arts, Summoning, Support & Dance) trade
 * their weapons for spells: B casts the first spell of their list, Y the
 * second, X the rest (tap: cast, hold: next). With too little MP for the
 * B spell, B stays the game's weapon swing so he can always attack. */
#define SOM_CASTER_FIRST 3u
#define SOM_CASTER_LAST 7u
#define SOM_CASTER_BUTTON_SPELLS 2u
static inline int som_disc_caster(unsigned discipline) {
    return discipline >= SOM_CASTER_FIRST && discipline <= SOM_CASTER_LAST;
}

static inline const char *som_spell_label(unsigned spell) {
    return spell < SOM_SPELL_COUNT ? som_spell_names[spell] : "spell";
}

static inline unsigned som_class_spell_count(unsigned discipline) {
    if (discipline >= SOM_DISCIPLINE_COUNT) return 1u;
    unsigned n = 0;
    while (n < SOM_CLASS_SPELLS_MAX && som_class_spells[discipline][n] != 0xFFu) n++;
    return n;
}

/* The spell at cursor position i (wrapped) of a discipline. */
static inline unsigned som_class_spell(unsigned discipline, unsigned i) {
    const unsigned n = som_class_spell_count(discipline);
    if (discipline >= SOM_DISCIPLINE_COUNT || !n) return 0x0Cu;   /* Fire Ball */
    return som_class_spells[discipline][i % n];
}

/* Whether a discipline may cast a spell (for the AI's cast commands). */
static inline int som_class_knows(unsigned discipline, unsigned spell) {
    const unsigned n = som_class_spell_count(discipline);
    for (unsigned i = 0; i < n; i++)
        if (som_class_spell(discipline, i) == spell) return 1;
    return 0;
}

/* Naruto-style seals: while R2 is held each D-pad press is one seal; B or Y
 * then fires that weapon's charge level = number of seals (no seals: the
 * old R2+B level 1 / R2+Y level 2). */
static const char *const som_seal_names[4] = {"Tiger", "Boar", "Dog", "Ram"};  /* U D L R */

static inline unsigned som_seal_level(unsigned seals, unsigned which) {
    if (!seals) return which ? 2u : 1u;
    return seals > SOM_SEALS_MAX ? SOM_SEALS_MAX : seals;
}
#endif
