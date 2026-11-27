#ifndef FLXTR_SOM_CLASS_GUIDE_H
#define FLXTR_SOM_CLASS_GUIDE_H

#include "som_disciplines.h"

typedef struct {
    const char *role;
    const char *moves[3];
} SomClassGuide;

static const SomClassGuide som_class_guide[SOM_DISCIPLINE_COUNT]={
    {"Close-range fighter with crowd control.",
     {"L2+X  Whirlwind around you","R2+A  Bash through enemies","B x4, B  Axe Cleave"}},
    {"Close-range strikes and guard counters.",
     {"X  Intercept an attack (!)","L2+X  Hundred Fists","R2  Guard to counter"}},
    {"Fast weapon swaps, spells and clones.",
     {"L2+X  Shunshin with a close weapon","X then A  Blink step","B x4, B  Taijutsu"}},
    {"Elemental spells and ranged damage.",
     {"Tap X  Cast the selected spell","Hold X  Cycle spells","A  Blink step"}},
    {"Healing, protection and holy damage.",
     {"Tap X  Cast the selected spell","Hold X  Cycle spells","R1  Change spell set"}},
    {"Dark magic, drains and a familiar.",
     {"X  Shadow Pact binds a familiar","X again  Release familiar","R1  Change spell set"}},
    {"Summons an ally and disrupts enemies.",
     {"X  Summon a familiar","X again  Release familiar","R1  Change spell set"}},
    {"Heals allies and changes combat tempo.",
     {"Tap X  Cast the selected spell","Hold X  Cycle spells","R1  Change spell set"}},
    {"Cycles melee and ranged weapons.",
     {"X  Attack with the bow","B x4, B  Javelin Step","Switch weapons to build combos"}},
    {"Quick attacks and rear ambushes.",
     {"L2+X  Shadowstep behind a foe","Next hit  Bonus backstab damage","B x4, B  Quick Shot"}},
    {"Katana counters and charged cuts.",
     {"Tap X / Hold X  Iaijutsu Lv3 / 4","L2+X  Issen dash cut","R2  Guard to parry"}},
    {"Ranged pressure and cone attacks.",
     {"L2+X  Volley in a forward cone","A  Evade","Hold B/Y after a combo  Finisher"}},
    {"Whip, thrown cross and area damage.",
     {"X  Throw Holy Water","L2+X  Grand Cross","A  Backflip"}},
    {"Drains life and raises fallen foes.",
     {"X after a kill  Raise Dead","X again  Release familiar","R1  Change spell set"}},
    {"Traps, explosives and portals.",
     {"Tap X  Plant a bomb","Hold X  Place a portal","L2+X  Place a mine"}},
    {"Takes control and manipulates objects.",
     {"X  Mind Control a nearby foe","X again  Release controlled foe","L2+X  Pull a dropped chest"}},
};

#endif
