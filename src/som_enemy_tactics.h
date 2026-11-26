#ifndef FLXTR_SOM_ENEMY_TACTICS_H
#define FLXTR_SOM_ENEMY_TACTICS_H
/* Enemy fighting-game tactics over the native monster AI (Phase C).
 *
 * The monster's own AI script (som-decomp monster-ai-opcodes.md) still picks
 * every command: hops, swings, casts, poses. This layer only bends what it
 * does with three levers, each measured on the Turbo ROM headless
 * (scratchpad probe, 2026-11-26, Silktail on map 52):
 *   1. velocity +0x06/+0x07 (sign-magnitude, 0x82 = -2 px/frame) while the
 *      game is moving the monster: rewriting it steers the native hop; the
 *      game's terrain integration (C1:B93E) still stops it at walls; zero
 *      holds it in place. Never written while it attacks, casts or is hurt,
 *      and never when the game is not moving it (no invented movement).
 *   2. weapon gauge +0x1ED: op E8 swings only at gauge 0 (decomp), so raising
 *      it delays the native swing (measured: 60 held = 0 attacks in 900
 *      frames). It is only ever raised, never lowered: enemy recovery is
 *      never shortened and no attack is forced.
 *   3. nothing else: targets, commands +0x140 (consumed inside the AI step,
 *      rewriting +0x141 has no effect, measured) and damage are untouched.
 * A punish is therefore "close to swing range while the opponent recovers
 * and do not hold the swing", and the native script throws the attack.
 *
 * Everything the enemies react to is visible in the world: hero poses
 * (+0x1C), positions, the hero's weapon gauge, HP lost by monsters while a
 * hero swings. No controller input is read. Pure functions; no I/O, no
 * allocation; the caller owns one SomTac. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SOM_TAC_FOES 3u
#define SOM_TAC_HEROES 3u
#define SOM_TAC_HERO_BASE 0xE000u
#define SOM_TAC_FOE_BASE 0xE600u
#define SOM_TAC_OBJECT 0x200u
#define SOM_TAC_SIGHT 128          /* px: a monster observes heroes this close */
#define SOM_TAC_GROUP 96           /* px: monsters this close to one target coordinate */

typedef enum {
    SOM_COMBAT_NEUTRAL, SOM_COMBAT_APPROACHING, SOM_COMBAT_RETREATING,
    SOM_COMBAT_STARTUP, SOM_COMBAT_ACTIVE, SOM_COMBAT_RECOVERY, SOM_COMBAT_GUARDING,
    SOM_COMBAT_EVADE, SOM_COMBAT_STAGGER, SOM_COMBAT_KNOCKDOWN, SOM_COMBAT_CASTING,
    SOM_COMBAT_UNKNOWN, SOM_COMBAT_PHASES
} SomCombatPhase;
static const char *const som_combat_phase_names[SOM_COMBAT_PHASES]={
    "Neutral","Approach","Retreat","Startup","Active","Recovery","Guard","Evade",
    "Stagger","Knockdown","Casting","Unknown"};

/* ---- species archetypes ------------------------------------------------ */
typedef enum {
    SOM_ARCH_NATIVE, SOM_ARCH_RUSHDOWN, SOM_ARCH_ZONER, SOM_ARCH_TRAPPER,
    SOM_ARCH_TECHNICAL, SOM_ARCH_DUELIST, SOM_ARCH_AMBUSHER, SOM_ARCH_BRUISER,
    SOM_ARCH_SKIRMISHER, SOM_ARCH_CASTER, SOM_ARCH_COWARD, SOM_ARCH_SWARM,
    SOM_ARCH_GUARDIAN, SOM_ARCH_BERSERKER, SOM_ARCH_COUNT
} SomArchetype;
static const char *const som_arch_names[SOM_ARCH_COUNT]={
    "Native","Rushdown","Zoner","Trapper","Technical","Duelist","Ambusher","Bruiser",
    "Skirmisher","Caster","Coward","Swarm","Guardian","Berserker"};

/* Tactics an archetype may use (bits). */
enum {
    SOM_TT_FOOTSIES=1u<<0, SOM_TT_PUNISH=1u<<1, SOM_TT_BAIT=1u<<2, SOM_TT_ZONE=1u<<3,
    SOM_TT_RUSH=1u<<4, SOM_TT_COUNTER=1u<<5, SOM_TT_HITRUN=1u<<6, SOM_TT_MIXUP=1u<<7,
    SOM_TT_CONDITION=1u<<8, SOM_TT_OKI=1u<<9, SOM_TT_FRAMETRAP=1u<<10, SOM_TT_DEFEND=1u<<11,
    SOM_TT_GROUP=1u<<12, SOM_TT_FLANK=1u<<13
};
#define SOM_TT_ALL 0x3FFFu

typedef struct {
    uint8_t aggression, patience, courage, adaptability, discipline, teamwork;
    uint8_t range_min, range_opt, range_max;   /* px, max(|dx|,|dy|) */
    uint8_t retreat_threshold;                 /* % HP below which it backs off */
    uint32_t supported_tactics;
} SomEnemyPersonality;

static const SomEnemyPersonality som_arch_profile[SOM_ARCH_COUNT]={
    /*                 agg pat cou ada dis tea  min opt max ret tactics */
    [SOM_ARCH_NATIVE]    ={  0,  0,  0,  0,  0,  0,   0,  0,  0,  0, 0},
    [SOM_ARCH_RUSHDOWN]  ={220, 50,200, 90, 80,120,  10, 22, 40, 15,
        SOM_TT_FOOTSIES|SOM_TT_RUSH|SOM_TT_PUNISH|SOM_TT_MIXUP|SOM_TT_FRAMETRAP|SOM_TT_OKI|SOM_TT_GROUP|SOM_TT_CONDITION},
    [SOM_ARCH_ZONER]     ={ 90,170, 90,120,170,170,  48, 58, 76, 35,
        SOM_TT_ZONE|SOM_TT_DEFEND|SOM_TT_PUNISH|SOM_TT_GROUP|SOM_TT_CONDITION|SOM_TT_FLANK},
    [SOM_ARCH_TRAPPER]   ={ 80,220,120,150,160,140,  20, 32, 48, 30,
        SOM_TT_FOOTSIES|SOM_TT_BAIT|SOM_TT_PUNISH|SOM_TT_DEFEND|SOM_TT_CONDITION|SOM_TT_COUNTER|SOM_TT_GROUP},
    [SOM_ARCH_TECHNICAL] ={140,160,160,170,220,150,  18, 30, 46, 25,
        SOM_TT_FOOTSIES|SOM_TT_PUNISH|SOM_TT_BAIT|SOM_TT_COUNTER|SOM_TT_MIXUP|SOM_TT_DEFEND|SOM_TT_OKI|SOM_TT_CONDITION|SOM_TT_GROUP|SOM_TT_FLANK},
    [SOM_ARCH_DUELIST]   ={150,170,190,230,210,110,  18, 30, 48, 20,
        SOM_TT_ALL&~SOM_TT_ZONE},
    [SOM_ARCH_AMBUSHER]  ={170,200,140,110,140, 90,  16, 28, 44, 30,
        SOM_TT_HITRUN|SOM_TT_PUNISH|SOM_TT_BAIT|SOM_TT_DEFEND|SOM_TT_OKI},
    [SOM_ARCH_BRUISER]   ={180, 90,230, 70, 90,130,  10, 20, 34, 10,
        SOM_TT_FOOTSIES|SOM_TT_RUSH|SOM_TT_PUNISH|SOM_TT_FRAMETRAP|SOM_TT_GROUP},
    [SOM_ARCH_SKIRMISHER]={160,120,130,120,150,140,  26, 44, 66, 30,
        SOM_TT_HITRUN|SOM_TT_FOOTSIES|SOM_TT_PUNISH|SOM_TT_MIXUP|SOM_TT_DEFEND|SOM_TT_FLANK|SOM_TT_GROUP|SOM_TT_CONDITION},
    [SOM_ARCH_CASTER]    ={ 70,200, 80,140,170,170,  56, 72, 96, 40,
        SOM_TT_ZONE|SOM_TT_DEFEND|SOM_TT_GROUP|SOM_TT_FLANK},
    [SOM_ARCH_COWARD]    ={ 60,140, 40, 90,100, 60,  30, 50, 80, 60,
        SOM_TT_HITRUN|SOM_TT_DEFEND|SOM_TT_PUNISH},
    [SOM_ARCH_SWARM]     ={190, 70,170, 60, 80,230,  12, 24, 44, 20,
        SOM_TT_RUSH|SOM_TT_FOOTSIES|SOM_TT_GROUP|SOM_TT_FLANK|SOM_TT_FRAMETRAP},
    [SOM_ARCH_GUARDIAN]  ={110,190,220,120,190,210,  14, 26, 40, 15,
        SOM_TT_FOOTSIES|SOM_TT_COUNTER|SOM_TT_PUNISH|SOM_TT_DEFEND|SOM_TT_GROUP},
    [SOM_ARCH_BERSERKER] ={200, 40,240, 60, 50, 80,  10, 20, 36,  0,
        SOM_TT_RUSH|SOM_TT_FOOTSIES|SOM_TT_FRAMETRAP|SOM_TT_OKI},
};

/* Native capabilities per species (som-decomp monster-commands.md):
 * PROJ  the swing launches a projectile (12 species);
 * NOSWING the script never issues command 02 (11 species);
 * BOSSLIKE ids without an ordinary swing row (0x20 0x2E 0x3C 0x3F 0x44 0x4C)
 * and 0x54 up: native only. */
enum { SOM_SP_PROJ=1u, SOM_SP_NOSWING=2u, SOM_SP_STILL=4u };
typedef struct { uint8_t archetype, caps; } SomSpecies;
#define SOM_TAC_SPECIES 0x54u
static const SomSpecies som_species_table[SOM_TAC_SPECIES]={
    [0x00]={SOM_ARCH_RUSHDOWN,0},             /* Rabite */
    [0x01]={SOM_ARCH_SWARM,0},                /* Buzz Bee */
    [0x02]={SOM_ARCH_TRAPPER,0},              /* Mushboom: defensive trapper */
    [0x03]={SOM_ARCH_ZONER,SOM_SP_PROJ},      /* Chobin Hood */
    [0x04]={SOM_ARCH_TRAPPER,SOM_SP_STILL},   /* Lullabud */
    [0x05]={SOM_ARCH_AMBUSHER,0},             /* Iffish */
    [0x06]={SOM_ARCH_ZONER,SOM_SP_PROJ},      /* Kid Goblin */
    [0x07]={SOM_ARCH_CASTER,0},               /* Eye Spy */
    [0x08]={SOM_ARCH_TRAPPER,0},              /* Green Drop */
    [0x09]={SOM_ARCH_CASTER,SOM_SP_NOSWING},  /* Specter */
    [0x0A]={SOM_ARCH_RUSHDOWN,0},             /* Blat */
    [0x0B]={SOM_ARCH_ZONER,SOM_SP_PROJ},      /* Goblin */
    [0x0C]={SOM_ARCH_ZONER,SOM_SP_PROJ},      /* Water Thug */
    [0x0D]={SOM_ARCH_AMBUSHER,SOM_SP_STILL},  /* Polter Chair */
    [0x0E]={SOM_ARCH_ZONER,SOM_SP_PROJ},      /* Ma Goblin */
    [0x0F]={SOM_ARCH_CASTER,0},               /* Dark Funk */
    [0x10]={SOM_ARCH_BRUISER,0},              /* Crawler */
    [0x11]={SOM_ARCH_ZONER,SOM_SP_PROJ},      /* Ice Thug */
    [0x12]={SOM_ARCH_RUSHDOWN,0},             /* Zombie */
    [0x13]={SOM_ARCH_CASTER,SOM_SP_NOSWING},  /* Kimono Bird */
    [0x14]={SOM_ARCH_SKIRMISHER,0},           /* Silktail: 80 px lunge */
    [0x15]={SOM_ARCH_SKIRMISHER,0},           /* Nemesis Owl */
    [0x16]={SOM_ARCH_BRUISER,0},              /* Pebbler */
    [0x17]={SOM_ARCH_COWARD,SOM_SP_NOSWING},  /* Pumpkin Bomb */
    [0x18]={SOM_ARCH_BRUISER,0},              /* Steamed Crab */
    [0x19]={SOM_ARCH_TECHNICAL,0},            /* Chess Knight */
    [0x1A]={SOM_ARCH_CASTER,0},               /* Wizard Eye */
    [0x1B]={SOM_ARCH_SKIRMISHER,0},           /* Howler */
    [0x1C]={SOM_ARCH_ZONER,SOM_SP_PROJ},      /* Robin Foot */
    [0x1D]={SOM_ARCH_CASTER,0},               /* LA Funk */
    [0x1E]={SOM_ARCH_BERSERKER,0},            /* Grave Bat */
    [0x1F]={SOM_ARCH_TECHNICAL,0},            /* Werewolf */
    [0x21]={SOM_ARCH_GUARDIAN,0},             /* Evil Sword */
    [0x22]={SOM_ARCH_COWARD,SOM_SP_NOSWING},  /* Tomato Man */
    [0x23]={SOM_ARCH_CASTER,0},               /* Mystic Book */
    [0x24]={SOM_ARCH_TRAPPER,0},              /* Sand Stinger */
    [0x25]={SOM_ARCH_SKIRMISHER,0},           /* Mad Mallard */
    [0x26]={SOM_ARCH_SWARM,0},                /* Emberman */
    [0x27]={SOM_ARCH_TRAPPER,0},              /* Red Drop */
    [0x28]={SOM_ARCH_SKIRMISHER,0},           /* Eggatrice */
    [0x29]={SOM_ARCH_SWARM,0},                /* Bomb Bee */
    [0x2A]={SOM_ARCH_TRAPPER,0},              /* Mushgloom */
    [0x2B]={SOM_ARCH_TRAPPER,SOM_SP_STILL},   /* Trap Flower */
    [0x2C]={SOM_ARCH_AMBUSHER,0},             /* Dinofish */
    [0x2D]={SOM_ARCH_AMBUSHER,SOM_SP_STILL},  /* Mimic Box */
    [0x2F]={SOM_ARCH_CASTER,SOM_SP_NOSWING},  /* Kimono Wizard */
    [0x30]={SOM_ARCH_CASTER,SOM_SP_NOSWING},  /* Ghost */
    [0x31]={SOM_ARCH_BRUISER,0},              /* Metal Crawler */
    [0x32]={SOM_ARCH_SWARM,SOM_SP_NOSWING},   /* Spider Legs */
    [0x33]={SOM_ARCH_AMBUSHER,0},             /* Weepy Eye */
    [0x34]={SOM_ARCH_BRUISER,0},              /* Shellblast */
    [0x35]={SOM_ARCH_BERSERKER,0},            /* Beast Zombie */
    [0x36]={SOM_ARCH_RUSHDOWN,0},             /* Ghoul */
    [0x37]={SOM_ARCH_ZONER,SOM_SP_PROJ},      /* Imp */
    [0x38]={SOM_ARCH_TRAPPER,0},              /* Blue Drop */
    [0x39]={SOM_ARCH_CASTER,SOM_SP_NOSWING},  /* Marmablue */
    [0x3A]={SOM_ARCH_BRUISER,0},              /* Fierce Head */
    [0x3B]={SOM_ARCH_GUARDIAN,0},             /* Griffin Hand */
    [0x3D]={SOM_ARCH_BRUISER,0},              /* Metal Crab */
    [0x3E]={SOM_ARCH_TECHNICAL,0},            /* Armored Man */
    [0x40]={SOM_ARCH_COWARD,SOM_SP_NOSWING},  /* Eggplant Man */
    [0x41]={SOM_ARCH_DUELIST,0},              /* Captain Duck */
    [0x42]={SOM_ARCH_COWARD,SOM_SP_NOSWING},  /* Nitro Pumpkin */
    [0x43]={SOM_ARCH_BRUISER,0},              /* Turtlance */
    [0x45]={SOM_ARCH_SKIRMISHER,0},           /* Basilisk */
    [0x46]={SOM_ARCH_ZONER,SOM_SP_PROJ},      /* Gremlin */
    [0x47]={SOM_ARCH_TRAPPER,0},              /* Steelpion */
    [0x48]={SOM_ARCH_DUELIST,SOM_SP_PROJ},    /* Dark Ninja */
    [0x49]={SOM_ARCH_COWARD,0},               /* Whimper */
    [0x4A]={SOM_ARCH_SKIRMISHER,0},           /* Heck Hound */
    [0x4B]={SOM_ARCH_BRUISER,0},              /* Fiend Head */
    [0x4D]={SOM_ARCH_DUELIST,SOM_SP_PROJ},    /* Dark Stalker: adaptive duelist */
    [0x4E]={SOM_ARCH_TECHNICAL,0},            /* Dark Knight */
    [0x4F]={SOM_ARCH_CASTER,SOM_SP_NOSWING},  /* Shape Shifter */
    [0x50]={SOM_ARCH_TECHNICAL,0},            /* Wolf Lord */
    [0x51]={SOM_ARCH_GUARDIAN,0},             /* Doom Sword */
    [0x52]={SOM_ARCH_TECHNICAL,0},            /* Terminator */
    [0x53]={SOM_ARCH_DUELIST,SOM_SP_PROJ},    /* Master Ninja */
    /* 0x20 0x2E 0x3C 0x3F 0x44 0x4C: no ordinary swing row: native. */
};
static inline SomSpecies som_tac_species(unsigned id){
    SomSpecies none={SOM_ARCH_NATIVE,0};
    return id<SOM_TAC_SPECIES?som_species_table[id]:none;
}

/* ---- difficulty and rank ---------------------------------------------- */
enum { SOM_FIGHT_CLASSIC, SOM_FIGHT_EASY, SOM_FIGHT_NORMAL, SOM_FIGHT_HARD, SOM_FIGHT_EXPERT,
       SOM_FIGHT_LEVELS };
static const char *const som_fight_names[SOM_FIGHT_LEVELS]={"classic","easy","normal","hard","expert"};
typedef struct {
    uint8_t interval;        /* frames between evaluations */
    uint8_t react;           /* minimum frames from a visible event to a reaction */
    uint8_t punish_pct;      /* chance to take a seen whiff */
    uint8_t mistake_pct;     /* chance to pick the second best action */
    uint8_t habit_confident; /* habit score that counts as a habit */
    uint8_t pressure;        /* attackers per hero (0 = no coordination) */
    uint8_t commit;          /* minimum frames an action is kept */
    uint32_t tactics;        /* tactics allowed at this level */
    uint32_t rank_tactics;   /* extra tactics a ranked enemy may unlock */
} SomFightLevel;
static const SomFightLevel som_fight_levels[SOM_FIGHT_LEVELS]={
    /* int rea pun mis hab pre com */
    {  0,  0,  0,  0,255,  0,  0, 0, 0},                                       /* classic */
    { 24, 30, 25, 35,220,  0, 30, SOM_TT_FOOTSIES|SOM_TT_ZONE|SOM_TT_DEFEND|SOM_TT_PUNISH,
      SOM_TT_HITRUN|SOM_TT_RUSH},                                               /* easy */
    { 14, 18, 55, 18,160,  0, 24, SOM_TT_FOOTSIES|SOM_TT_ZONE|SOM_TT_DEFEND|SOM_TT_PUNISH|
      SOM_TT_RUSH|SOM_TT_HITRUN|SOM_TT_BAIT|SOM_TT_CONDITION|SOM_TT_OKI,
      SOM_TT_MIXUP|SOM_TT_COUNTER|SOM_TT_GROUP},                                /* normal */
    { 10, 13, 80,  8,120,  1, 20, SOM_TT_ALL&~SOM_TT_FRAMETRAP,SOM_TT_FRAMETRAP}, /* hard */
    {  7, 10, 92,  3, 90,  2, 16, SOM_TT_ALL,0},                               /* expert */
};
static inline unsigned som_fight_parse(const char *s){
    if(!s||!*s)return SOM_FIGHT_NORMAL;
    for(unsigned i=0;i<SOM_FIGHT_LEVELS;i++)
        if(!strcmp(s,som_fight_names[i])||(s[0]=='0'+(int)i&&!s[1]))return i;
    return SOM_FIGHT_NORMAL;
}
enum { SOM_RANK_COMMON, SOM_RANK_VETERAN, SOM_RANK_ELITE, SOM_RANK_CAPTAIN,
       SOM_RANK_GENERAL, SOM_RANK_NEMESIS, SOM_RANKS };
static const char *const som_rank_names[SOM_RANKS]={"Common","Veteran","Elite","Captain","General","Nemesis"};
/* Tactics a rank brings, inside the level's envelope. */
static const uint32_t som_rank_tactics[SOM_RANKS]={
    0, SOM_TT_PUNISH|SOM_TT_FOOTSIES, SOM_TT_BAIT|SOM_TT_COUNTER|SOM_TT_MIXUP,
    SOM_TT_GROUP|SOM_TT_CONDITION|SOM_TT_FLANK, SOM_TT_ALL, SOM_TT_ALL};

/* ---- observed habits (per hero) ---------------------------------------- */
enum { SOM_HABIT_BLOCK, SOM_HABIT_RUSH_IN, SOM_HABIT_RETREAT, SOM_HABIT_RANGED,
       SOM_HABIT_DODGE_LEFT, SOM_HABIT_DODGE_RIGHT, SOM_HABIT_CLONES, SOM_HABIT_WHIFF,
       SOM_HABITS };
static const char *const som_habit_names[SOM_HABITS]={
    "blocks","attacks on approach","retreats after attacking","fights from range",
    "dodges left","dodges right","uses clones","whiffs"};
#define SOM_HABIT_GAIN 40u
#define SOM_HABIT_DECAY_FRAMES 60u

/* ---- actions ------------------------------------------------------------ */
typedef enum {
    SOM_TA_NATIVE, SOM_TA_WAIT, SOM_TA_APPROACH, SOM_TA_RETREAT, SOM_TA_SIDESTEP,
    SOM_TA_PUNISH, SOM_TA_BAIT, SOM_TA_ZONE, SOM_TA_DISENGAGE, SOM_TA_FLANK,
    SOM_TA_OKI, SOM_TA_COUNT
} SomTacAction;
static const char *const som_tac_action_names[SOM_TA_COUNT]={
    "Native","Wait","Approach","Retreat","Sidestep","Whiff Punish","Bait","Zone",
    "Disengage","Flank","Okizeme"};
/* Cooldown after an action ends (frames) and its time limit. */
static const uint16_t som_tac_cooldown[SOM_TA_COUNT]={0,0,0,0,40,30,200,0,60,90,0};
static const uint16_t som_tac_timeout[SOM_TA_COUNT]={60,50,90,60,40,60,150,90,50,120,120};

enum { SOM_ROLE_NONE, SOM_ROLE_PRESSURE, SOM_ROLE_FLANKER, SOM_ROLE_ZONER, SOM_ROLE_RESERVE,
       SOM_ROLES };
static const char *const som_role_names[SOM_ROLES]={"-","Pressure","Flanker","Zoner","Reserve"};

/* ---- observation -------------------------------------------------------- */
/* What the proxy knows about a hero beyond WRAM: its own guard and evades,
 * Kage Bunshin clones, and the weapon reach of its class. */
typedef struct { uint8_t guard, evade, clones, reach; } SomTacHeroExtra;
typedef struct {
    uint8_t valid, phase, layer, weapon, reach, gauge;
    int x, y;
} SomTacHero;
typedef struct {
    uint8_t live, species, state, layer;
    uint16_t hp, max_hp, status;
    int x, y;
} SomTacFoeView;

/* Per hero: the events the enemies may react to, latched until consumed. */
typedef struct {
    uint8_t state, attacking, hit, phase, prev_guard, prev_evade, clones;
    unsigned long attack_at, attack_end, last_close, guard_since;
    uint16_t attack_seq, whiff_seq;       /* whiff_seq = attack_seq of the last miss */
    unsigned long whiff_at;
    int last_x, last_y, approach_from;
    int8_t evade_dir;                     /* -1 left, +1 right of its facing foe, 0 none */
    uint16_t evade_seq, rush_seq, retreat_seq, block_seq, ranged_seq, clone_seq;
} SomTacTrack;

typedef struct {
    uint8_t live, species, archetype, rank, slot, layer;
    uint16_t max_hp, hp;
    unsigned long born, seen;
    SomEnemyPersonality p;                 /* species profile + individual variation */
    uint32_t rng;
    uint8_t action, prev_action, repeats, target, role, bait_stage;
    unsigned long action_since, action_until, next_eval, cooldown[SOM_TA_COUNT];
    unsigned long own_attack_end, react_after, token_until;
    uint8_t own_attacking;
    uint8_t habit[SOM_TAC_HEROES][SOM_HABITS];
    uint16_t seen_seq[SOM_TAC_HEROES][SOM_HABITS];
    uint16_t whiff_taken[SOM_TAC_HEROES];
    unsigned long habit_decay;
    int last_x, last_y;
    uint8_t stuck, detour;
    int8_t side;                           /* flank side: -1 / +1 */
    uint8_t hold_gauge;                    /* last gauge we set (0 none) */
    int score[SOM_TA_COUNT];
    char reason[40];
    uint16_t stats_actions[SOM_TA_COUNT];
} SomTacFoe;

typedef struct {
    unsigned level;
    unsigned map;
    int arena_archetype;                   /* forced archetype (training), -1 none */
    int rank_slot[SOM_TAC_FOES];           /* rank given by the caller (Generals) */
    uint8_t rank[SOM_TAC_FOES];
    SomTacHero hero[SOM_TAC_HEROES];
    SomTacTrack track[SOM_TAC_HEROES];
    SomTacFoeView view[SOM_TAC_FOES];
    SomTacFoe foe[SOM_TAC_FOES];
    unsigned long last_frame;
    uint32_t evals, writes;                /* counters for tests and perf */
} SomTac;

static inline unsigned som_tac_u16(const uint8_t *p){return (unsigned)(p[0]|p[1]<<8);}
static inline int som_tac_dist(int ax,int ay,int bx,int by){
    const int dx=abs(ax-bx),dy=abs(ay-by);
    return dx>dy?dx:dy;
}
static inline uint32_t som_tac_rand(uint32_t *s){
    uint32_t x=*s?*s:0x9E3779B9u;
    x^=x<<13;x^=x>>17;x^=x<<5;*s=x;return x;
}
static inline unsigned som_tac_pct(uint32_t *s){return som_tac_rand(s)%100u;}

static inline void som_tac_reset(SomTac *t,unsigned level){
    const int arena=t->arena_archetype;
    memset(t,0,sizeof(*t));
    t->level=level<SOM_FIGHT_LEVELS?level:SOM_FIGHT_NORMAL;
    t->map=0x10000u;t->arena_archetype=arena;
    for(unsigned i=0;i<SOM_TAC_FOES;i++)t->rank_slot[i]=-1;
}
static inline const SomFightLevel *som_tac_level(const SomTac *t){return &som_fight_levels[t->level];}

/* Hero phase from what is on screen (+0x1C pose, +0x11 animation, status,
 * weapon gauge) and the proxy's guard/evade. Hurt = 0x40 with animation
 * 0x88-0x8D and dying 0x8E (som-decomp hit-reactions.md). */
static inline unsigned som_tac_hero_phase(const uint8_t *o,const SomTacHeroExtra *x,
                                          const SomTacTrack *tr,unsigned long frame){
    const unsigned status=som_tac_u16(o+0x190),state=o[0x1C],anim=o[0x11];
    if(status&0x0002u||(state==0x40u&&anim==0x8Eu))return SOM_COMBAT_KNOCKDOWN;
    if(state==0x40u&&anim>=0x88u&&anim<=0x8Du)return SOM_COMBAT_STAGGER;
    if(x&&x->evade)return SOM_COMBAT_EVADE;
    if(status&0x0400u)return SOM_COMBAT_EVADE;
    if(state==0x40u&&o[0x60])return SOM_COMBAT_CASTING;
    if(state==0x20u||state==0x80u)
        return frame-tr->attack_at<6u?SOM_COMBAT_STARTUP:SOM_COMBAT_ACTIVE;
    if(x&&x->guard)return SOM_COMBAT_GUARDING;
    if(state)return SOM_COMBAT_UNKNOWN;
    if(o[0x1ED]&&tr->attack_end&&frame-tr->attack_end<40u)return SOM_COMBAT_RECOVERY;
    return SOM_COMBAT_NEUTRAL;
}

/* A monster slot this layer may touch: an ordinary monster, alive, not the
 * familiar, not a boss, not dying. */
static inline int som_tac_foe_ok(const uint8_t *o){
    return o[0]==1&&o[0x180]<SOM_TAC_SPECIES&&som_tac_u16(o+0x182)!=0&&
           !(som_tac_u16(o+0x190)&0x8000u);
}

/* Fills the snapshot and the hero trackers. Hit confirmation: a monster that
 * lost HP while a hero's attack was out or just after it marks that attack
 * as a hit (not a whiff). */
static inline void som_tac_observe(SomTac *t,const uint8_t *ram,const SomTacHeroExtra *extra,
                                   int familiar_slot,unsigned long frame){
    int hp_lost=0;
    for(unsigned s=0;s<SOM_TAC_FOES;s++){
        const uint8_t *o=ram+SOM_TAC_FOE_BASE+SOM_TAC_OBJECT*s;
        SomTacFoeView *v=&t->view[s];
        const unsigned hp=som_tac_u16(o+0x182);
        const int live=(int)s!=familiar_slot&&o[0]==1&&o[0x180]<0x80u&&!(som_tac_u16(o+0x190)&0x8000u);
        if(v->live&&o[0x180]==v->species&&hp<v->hp)hp_lost=1;
        if(v->live&&live&&o[0x180]==v->species&&!hp&&v->hp)hp_lost=1;
        v->live=(uint8_t)live;v->species=o[0x180];v->state=o[0x1C];v->layer=o[0x0B]&0x80u;
        v->hp=(uint16_t)hp;v->max_hp=(uint16_t)som_tac_u16(o+0x184);v->status=(uint16_t)som_tac_u16(o+0x190);
        v->x=(int)som_tac_u16(o+2);v->y=(int)som_tac_u16(o+4);
    }
    for(unsigned h=0;h<SOM_TAC_HEROES;h++){
        const uint8_t *o=ram+SOM_TAC_HERO_BASE+SOM_TAC_OBJECT*h;
        SomTacHero *hero=&t->hero[h];SomTacTrack *tr=&t->track[h];
        const SomTacHeroExtra *x=extra?&extra[h]:NULL;
        hero->valid=(uint8_t)(o[0]==1&&som_tac_u16(o+0x182)&&!(som_tac_u16(o+0x190)&0x8000u));
        hero->x=(int)som_tac_u16(o+2);hero->y=(int)som_tac_u16(o+4);
        hero->layer=o[0x0B]&0x80u;hero->weapon=o[0x1E4]&7u;hero->gauge=o[0x1ED];
        {
            static const uint8_t reach[8]={24,28,28,34,48,96,80,80};
            hero->reach=x&&x->reach?x->reach:reach[hero->weapon];
        }
        if(!hero->valid){memset(tr,0,sizeof(*tr));hero->phase=SOM_COMBAT_UNKNOWN;continue;}
        const unsigned state=o[0x1C];
        const int attacking=state==0x20u||state==0x80u;
        /* nearest live foe, for direction-dependent habits */
        int near=-1,nd=0x7FFF;
        for(unsigned s=0;s<SOM_TAC_FOES;s++){
            if(!t->view[s].live||t->view[s].layer!=hero->layer)continue;
            const int d=som_tac_dist(hero->x,hero->y,t->view[s].x,t->view[s].y);
            if(d<nd){nd=d;near=(int)s;}
        }
        if(attacking&&!tr->attacking){
            tr->attack_at=frame;tr->attack_seq++;tr->hit=0;
            if(hero->weapon>=5u&&near>=0&&nd>48)tr->ranged_seq++;
            /* attacked right after closing in: rush habit */
            if(tr->last_close&&frame-tr->last_close<24u)tr->rush_seq++;
        }
        if((attacking||(tr->attack_end&&frame-tr->attack_end<8u))&&hp_lost)tr->hit=1;
        if(!attacking&&tr->attacking){
            tr->attack_end=frame;
            if(!tr->hit&&near>=0&&nd<=SOM_TAC_SIGHT){tr->whiff_seq=tr->attack_seq;tr->whiff_at=frame;}
        }
        /* closing in: 8 px nearer the nearest foe since the last frame window */
        if(near>=0){
            const int was=tr->approach_from;
            if(!was||nd>was)tr->approach_from=nd;
            else if(was-nd>=12){tr->last_close=frame;tr->approach_from=nd;}
            /* backing off soon after an attack */
            if(tr->attack_end&&frame-tr->attack_end<30u&&tr->last_x|tr->last_y){
                const SomTacFoeView *v=&t->view[near];
                const int before=som_tac_dist(tr->last_x,tr->last_y,v->x,v->y);
                if(nd>before+1&&!attacking&&tr->retreat_seq!=tr->attack_seq)tr->retreat_seq=tr->attack_seq;
            }
        }
        const int guard=x&&x->guard,evade=x&&x->evade;
        if(guard&&!tr->prev_guard){tr->guard_since=frame;tr->block_seq++;}
        if(evade&&!tr->prev_evade&&near>=0){
            /* side relative to the foe: cross product of foe->hero and hero motion */
            const SomTacFoeView *v=&t->view[near];
            const int fx=hero->x-v->x,fy=hero->y-v->y,mx=hero->x-tr->last_x,my=hero->y-tr->last_y;
            const int cross=fx*my-fy*mx;
            tr->evade_dir=(int8_t)(cross>0?1:cross<0?-1:0);tr->evade_seq++;
        }
        if(x&&x->clones>tr->clones)tr->clone_seq++;
        tr->clones=x?x->clones:0;
        tr->prev_guard=(uint8_t)guard;tr->prev_evade=(uint8_t)evade;
        tr->attacking=(uint8_t)attacking;tr->state=(uint8_t)state;
        hero->phase=(uint8_t)som_tac_hero_phase(o,x,tr,frame);
        tr->phase=hero->phase;
        tr->last_x=hero->x;tr->last_y=hero->y;
    }
}

/* Individual variation: species profile +-24 on each trait from a seed of
 * map, slot, species and the frame it appeared. Ranges +-4 px. */
static inline void som_tac_personality(SomTacFoe *f,unsigned archetype,unsigned map,unsigned slot,
                                       unsigned long frame){
    f->p=som_arch_profile[archetype];
    f->rng=(uint32_t)(map*2654435761u)^(uint32_t)(slot*40503u)^(uint32_t)(f->species*97u)^
           (uint32_t)(frame*2246822519u)^0xA5A5u;
    uint8_t *traits[6]={&f->p.aggression,&f->p.patience,&f->p.courage,&f->p.adaptability,
                        &f->p.discipline,&f->p.teamwork};
    for(unsigned i=0;i<6;i++){
        const int v=(int)*traits[i]+(int)(som_tac_rand(&f->rng)%49u)-24;
        *traits[i]=(uint8_t)(v<0?0:v>255?255:v);
    }
    const int shift=(int)(som_tac_rand(&f->rng)%9u)-4;
    f->p.range_opt=(uint8_t)((int)f->p.range_opt+shift);
    f->side=(som_tac_rand(&f->rng)&1u)?1:-1;
}
/* Short name of the strongest trait for the debug overlay. */
static inline const char *som_tac_temper(const SomTacFoe *f,unsigned archetype){
    const SomEnemyPersonality *b=&som_arch_profile[archetype];
    const int d[5]={f->p.aggression-b->aggression,f->p.patience-b->patience,
                    f->p.courage-b->courage,f->p.adaptability-b->adaptability,
                    f->p.discipline-b->discipline};
    static const char *const names[5]={"Aggressive","Patient","Bold","Opportunistic","Disciplined"};
    static const char *const low[5]={"Restrained","Restless","Cautious","Stubborn","Reckless"};
    int best=0;
    for(int i=1;i<5;i++)if(abs(d[i])>abs(d[best]))best=i;
    return d[best]>=0?names[best]:low[best];
}

static inline unsigned som_tac_rank_of(const SomTac *t,unsigned slot){
    return t->rank[slot]<SOM_RANKS?t->rank[slot]:SOM_RANK_COMMON;
}
static inline uint32_t som_tac_allowed(const SomTac *t,const SomTacFoe *f){
    const SomFightLevel *L=som_tac_level(t);
    const uint32_t lv=L->tactics|(L->rank_tactics&som_rank_tactics[f->rank]);
    return lv&f->p.supported_tactics;
}
static inline unsigned som_tac_react(const SomTac *t,const SomTacFoe *f){
    const SomFightLevel *L=som_tac_level(t);
    /* rank shortens reactions a little, never below 8 frames */
    unsigned r=L->react>f->rank*2u?L->react-f->rank*2u:0u;
    return r<8u?8u:r;
}

/* Starts (or keeps) a foe's record for the monster in `slot`. A different
 * species, a full heal after a death, a gap in observation or a map change
 * is a new monster: its memory is not carried over. */
static inline SomTacFoe *som_tac_foe_sync(SomTac *t,unsigned slot,const uint8_t *o,unsigned long frame){
    SomTacFoe *f=&t->foe[slot];
    const unsigned species=o[0x180],hp=som_tac_u16(o+0x182),max=som_tac_u16(o+0x184);
    if(f->live&&(f->species!=species||f->max_hp!=max||hp>f->hp||frame<f->seen||frame-f->seen>2u))
        f->live=0;
    if(!f->live){
        memset(f,0,sizeof(*f));
        f->live=1;f->species=(uint8_t)species;f->slot=(uint8_t)slot;f->max_hp=(uint16_t)max;
        f->born=frame;f->target=0xFF;
        unsigned arch=som_tac_species(species).archetype;
        if(t->arena_archetype>0&&arch!=SOM_ARCH_NATIVE)arch=(unsigned)t->arena_archetype;
        f->archetype=(uint8_t)arch;
        som_tac_personality(f,arch,t->map,slot,frame);
        f->next_eval=frame+(unsigned long)(slot*3u)+4u;
        f->last_x=(int)som_tac_u16(o+2);f->last_y=(int)som_tac_u16(o+4);
        f->habit_decay=frame;
        for(unsigned h=0;h<SOM_TAC_HEROES;h++){
            const SomTacTrack *tr=&t->track[h];
            /* only what happens from now on is learned */
            f->seen_seq[h][SOM_HABIT_BLOCK]=tr->block_seq;f->seen_seq[h][SOM_HABIT_RUSH_IN]=tr->rush_seq;
            f->seen_seq[h][SOM_HABIT_RETREAT]=tr->retreat_seq;f->seen_seq[h][SOM_HABIT_RANGED]=tr->ranged_seq;
            f->seen_seq[h][SOM_HABIT_DODGE_LEFT]=f->seen_seq[h][SOM_HABIT_DODGE_RIGHT]=tr->evade_seq;
            f->seen_seq[h][SOM_HABIT_CLONES]=tr->clone_seq;f->seen_seq[h][SOM_HABIT_WHIFF]=tr->whiff_seq;
            f->whiff_taken[h]=tr->whiff_seq;
        }
    }
    f->rank=(uint8_t)som_tac_rank_of(t,slot);
    f->hp=(uint16_t)hp;f->seen=frame;f->layer=o[0x0B]&0x80u;
    return f;
}

static inline void som_tac_habit_add(SomTacFoe *f,unsigned h,unsigned habit,unsigned adapt){
    const unsigned gain=SOM_HABIT_GAIN*(64u+adapt)/320u+4u;   /* 12..48 */
    const unsigned v=f->habit[h][habit]+gain;
    f->habit[h][habit]=(uint8_t)(v>255u?255u:v);
}
/* Learns what the foe could see: events of heroes within sight. */
static inline void som_tac_learn(SomTac *t,SomTacFoe *f,int fx,int fy,unsigned long frame){
    const int adapt=(som_tac_allowed(t,f)&SOM_TT_CONDITION)!=0;
    for(unsigned h=0;h<SOM_TAC_HEROES;h++){
        const SomTacHero *hero=&t->hero[h];const SomTacTrack *tr=&t->track[h];
        uint16_t *seen=f->seen_seq[h];
        const int visible=hero->valid&&hero->layer==f->layer&&
                          som_tac_dist(hero->x,hero->y,fx,fy)<=SOM_TAC_SIGHT;
        const uint16_t now[SOM_HABITS]={tr->block_seq,tr->rush_seq,tr->retreat_seq,tr->ranged_seq,
                                        tr->evade_seq,tr->evade_seq,tr->clone_seq,tr->whiff_seq};
        for(unsigned k=0;k<SOM_HABITS;k++){
            if(now[k]==seen[k])continue;
            seen[k]=now[k];
            if(!visible||!adapt)continue;
            if(k==SOM_HABIT_DODGE_LEFT&&tr->evade_dir>=0)continue;
            if(k==SOM_HABIT_DODGE_RIGHT&&tr->evade_dir<=0)continue;
            som_tac_habit_add(f,h,k,f->p.adaptability);
        }
    }
    while(frame-f->habit_decay>=SOM_HABIT_DECAY_FRAMES){
        f->habit_decay+=SOM_HABIT_DECAY_FRAMES;
        for(unsigned h=0;h<SOM_TAC_HEROES;h++)for(unsigned k=0;k<SOM_HABITS;k++){
            uint8_t *v=&f->habit[h][k];
            *v=(uint8_t)(*v>(*v>>4)+1u?*v-(*v>>4)-1u:0u);
        }
    }
}
static inline int som_tac_habit(const SomTac *t,const SomTacFoe *f,unsigned h,unsigned k){
    const unsigned need=som_tac_level(t)->habit_confident;
    return h<SOM_TAC_HEROES&&f->habit[h][k]>=need?(int)f->habit[h][k]:0;
}

/* ---- decision --------------------------------------------------------------
 * Integer utilities; the best action wins with hysteresis for the current
 * one; difficulty adds deliberate mistakes (the second best). */
typedef struct {
    int d, band_min, band_opt, band_max, hero_reach;
    unsigned phase;
    int whiff_ready, recovery_seen, own_recent, ready, soon_ready, hero_ready, tokens_used, token_mine, hp_pct, allies_near;
} SomTacSituation;

static inline void som_tac_score(const SomTac *t,SomTacFoe *f,unsigned h,const SomTacSituation *s,
                                 unsigned long frame,int *score){
    const uint32_t ok=som_tac_allowed(t,f);
    const SomEnemyPersonality *p=&f->p;
    const SomSpecies sp=som_tac_species(f->species);
    for(unsigned a=0;a<SOM_TA_COUNT;a++)score[a]=-1000;
    score[SOM_TA_NATIVE]=20;
    const int threat=s->phase==SOM_COMBAT_STARTUP||s->phase==SOM_COMBAT_ACTIVE;
    const int down=s->phase==SOM_COMBAT_KNOCKDOWN||s->phase==SOM_COMBAT_STAGGER;
    const int inside_reach=s->d<=s->hero_reach+6;
    const int ranged=(sp.caps&SOM_SP_PROJ)||f->archetype==SOM_ARCH_CASTER||f->archetype==SOM_ARCH_ZONER;
    const int block=som_tac_habit(t,f,h,SOM_HABIT_BLOCK),rush=som_tac_habit(t,f,h,SOM_HABIT_RUSH_IN);
    const int range_h=som_tac_habit(t,f,h,SOM_HABIT_RANGED),clones=som_tac_habit(t,f,h,SOM_HABIT_CLONES);
    const int whiffs=som_tac_habit(t,f,h,SOM_HABIT_WHIFF),retreats=som_tac_habit(t,f,h,SOM_HABIT_RETREAT);
    const int pressure_ok=!s->tokens_used||s->token_mine;
    /* the opponent can hit back now: guard down, gauge full, not hurt */
    const int danger=s->hero_ready&&s->d<=s->hero_reach+6;
    /* footsies / rushdown: close the distance */
    if(ok&(SOM_TT_FOOTSIES|SOM_TT_RUSH)){
        if(s->d>s->band_max)score[SOM_TA_APPROACH]=40+p->aggression/4;
        else if(s->d>s->band_opt)score[SOM_TA_APPROACH]=12+p->aggression/8;
        if(threat&&inside_reach+0&&s->d<=s->hero_reach+16)score[SOM_TA_APPROACH]-=40;
        if((ok&(SOM_TT_RUSH|SOM_TT_PUNISH))&&s->recovery_seen)score[SOM_TA_APPROACH]+=24;
        if(retreats&&(ok&SOM_TT_RUSH))score[SOM_TA_APPROACH]+=retreats/8;   /* chase a backer */
        if(range_h)score[SOM_TA_APPROACH]+=range_h/10;                       /* get in on a shooter */
        if(!pressure_ok)score[SOM_TA_APPROACH]-=45;
    }
    /* defensive movement */
    if(ok&(SOM_TT_DEFEND|SOM_TT_FOOTSIES|SOM_TT_ZONE)){
        int r=-1000;
        if(s->d<s->band_min&&!s->ready&&danger)r=24+(255-p->courage)/8;
        /* a swing that can reach it: step out, unless brave enough to trade */
        if(threat&&s->d<=s->hero_reach&&(ok&SOM_TT_DEFEND))
            r=(r>0?r:0)+20+p->discipline/12-(int)p->courage/10;
        if(s->hp_pct<p->retreat_threshold&&danger)r=(r>0?r:0)+30;
        if(s->phase==SOM_COMBAT_RECOVERY)r-=30;
        score[SOM_TA_RETREAT]=r;
    }
    if((ok&SOM_TT_ZONE)&&ranged){
        if(s->d<s->band_min)score[SOM_TA_ZONE]=48+p->discipline/10;
        else if(s->d>s->band_max)score[SOM_TA_ZONE]=30;
    }
    if((ok&SOM_TT_DEFEND)&&threat&&!s->ready&&s->d<=s->hero_reach+8&&s->d>s->hero_reach/2)
        score[SOM_TA_SIDESTEP]=10+p->discipline/10+(range_h?10:0);
    /* waiting: patience against a guard, a threat or doubtful clones */
    {
        int w=-1000;
        if(s->phase==SOM_COMBAT_GUARDING&&s->d<=s->band_max+10)w=22+p->patience/6+block/6;
        /* just outside a swing that is out: let it finish, then punish */
        if(threat&&s->d>s->hero_reach&&s->d<=s->hero_reach+20)w=(w>0?w:0)+10+p->patience/10;
        if(clones)w=(w>0?w:0)+clones/8;
        if(!pressure_ok&&s->d<s->band_max+16)w=(w>0?w:0)+30;
        score[SOM_TA_WAIT]=w;
    }
    /* whiff punish: an attack that missed, the opponent recovering, in reach */
    if((ok&SOM_TT_PUNISH)&&s->whiff_ready&&s->soon_ready&&s->d<=s->band_max+24&&!(sp.caps&SOM_SP_NOSWING))
        score[SOM_TA_PUNISH]=60+p->discipline/4+whiffs/10;
    if((ok&SOM_TT_COUNTER)&&s->recovery_seen&&s->soon_ready&&s->d<=s->band_max&&
       !(sp.caps&SOM_SP_NOSWING))
        score[SOM_TA_PUNISH]=(score[SOM_TA_PUNISH]>0?score[SOM_TA_PUNISH]:0)+35+p->discipline/8;
    /* bait: step into their reach and out again */
    if((ok&SOM_TT_BAIT)&&frame>=f->cooldown[SOM_TA_BAIT]&&!threat&&!down&&
       s->phase!=SOM_COMBAT_GUARDING&&s->d>=s->hero_reach&&s->d<=s->hero_reach+44)
        score[SOM_TA_BAIT]=16+p->patience/10+rush/4+whiffs/6+(block?block/8:0);
    /* hit and run after its own attack */
    if((ok&SOM_TT_HITRUN)&&s->own_recent&&danger&&frame>=f->cooldown[SOM_TA_DISENGAGE])
        score[SOM_TA_DISENGAGE]=44+(255-p->courage)/10;
    /* flanking when another monster has the pressure */
    if((ok&SOM_TT_FLANK)&&s->tokens_used&&!s->token_mine&&frame>=f->cooldown[SOM_TA_FLANK])
        score[SOM_TA_FLANK]=34+p->teamwork/10;
    /* okizeme: wait at the wake-up spot while they lie down; strike when up */
    if((ok&SOM_TT_OKI)&&s->phase==SOM_COMBAT_KNOCKDOWN&&s->d<=s->band_max+40)score[SOM_TA_OKI]=55;
    (void)down;
    if(ranged&&(ok&SOM_TT_ZONE)&&s->d>=s->band_min&&s->d<=s->band_max)score[SOM_TA_NATIVE]+=30;
    if(s->ready&&s->d<=s->band_max&&!(threat&&inside_reach))score[SOM_TA_NATIVE]+=34+p->aggression/8;
    /* cooling down inside their reach: footsies, keep just outside it */
    if(!s->ready&&danger&&(ok&SOM_TT_FOOTSIES)&&score[SOM_TA_RETREAT]<22+p->discipline/10)
        score[SOM_TA_RETREAT]=22+p->discipline/10;
    /* repetition penalty, cooldowns */
    for(unsigned a=1;a<SOM_TA_COUNT;a++){
        if(frame<f->cooldown[a])score[a]-=200;
        if(a==f->prev_action&&f->repeats>2u)score[a]-=10*(f->repeats-2);
    }
}

static inline int som_tac_band(const SomTacFoe *f,const SomTacHero *hero,int *mn,int *op,int *mx){
    *mn=f->p.range_min;*op=f->p.range_opt;*mx=f->p.range_max;
    /* footsies against reach: technical fighters sit just outside it */
    if((f->archetype==SOM_ARCH_TECHNICAL||f->archetype==SOM_ARCH_DUELIST||
        f->archetype==SOM_ARCH_GUARDIAN)&&hero->reach+6>*op){
        *op=hero->reach+6;if(*mx<*op+12)*mx=*op+12;
    }
    return 1;
}

static inline void som_tac_pick(SomTac *t,SomTacFoe *f,const int *score,unsigned long frame){
    const SomFightLevel *L=som_tac_level(t);
    int best=0,second=0;
    for(unsigned a=1;a<SOM_TA_COUNT;a++){
        if(score[a]>score[best]){second=best;best=(int)a;}
        else if(a!=(unsigned)best&&score[a]>score[second])second=(int)a;
    }
    /* hysteresis: keep the current action while it is committed */
    if(f->action!=SOM_TA_NATIVE&&frame<f->action_since+L->commit&&frame<f->action_until&&
       score[f->action]>-500&&score[f->action]+12>=score[best]&&best!=SOM_TA_PUNISH)
        best=f->action;
    else if(score[second]>-500&&score[best]-score[second]<10&&
            som_tac_pct(&f->rng)<(unsigned)(L->mistake_pct+((som_tac_allowed(t,f)&SOM_TT_MIXUP)?20u:0u)))
        best=second;                         /* a mistake, or a mix-up */
    if(best!=(int)f->action){
        if(f->action!=SOM_TA_NATIVE)
            f->cooldown[f->action]=frame+som_tac_cooldown[f->action];
        f->repeats=(uint8_t)(best==(int)f->prev_action&&f->repeats<250u?f->repeats+1u:0u);
        f->prev_action=f->action;
        f->action=(uint8_t)best;f->action_since=frame;
        f->action_until=frame+som_tac_timeout[best];
        f->bait_stage=0;f->stuck=0;f->detour=0;
        if(f->stats_actions[best]<0xFFFFu)f->stats_actions[best]++;
    }
}

/* ---- the native adapter ------------------------------------------------- */
static inline int som_tac_sign(int v){return v>0?1:v<0?-1:0;}
/* Steers a native move: velocity keeps the game's magnitude. Returns 1 if
 * written. Only while the game itself moves the monster (velocity != 0)
 * and the monster is free (state 0): no new movement, no attack touched. */
static inline int som_tac_steer(SomTac *t,uint8_t *o,int sx,int sy){
    if(o[0x1C]||(!o[6]&&!o[7]))return 0;
    unsigned mag=(o[6]&0x7Fu)>(o[7]&0x7Fu)?(o[6]&0x7Fu):(o[7]&0x7Fu);
    if(!mag)return 0;
    if(mag>3u)mag=3u;
    const uint8_t vx=(uint8_t)(sx?(sx<0?0x80u:0u)|mag:0u),vy=(uint8_t)(sy?(sy<0?0x80u:0u)|mag:0u);
    if(o[6]==vx&&o[7]==vy)return 0;
    o[6]=vx;o[7]=vy;t->writes++;
    return 1;
}
/* Stops only the part of a native move that goes toward (tx,ty). */
static inline int som_tac_hold_ground(SomTac *t,uint8_t *o,int tx,int ty){
    if(o[0x1C]||(!o[6]&&!o[7]))return 0;
    const int vx=(o[6]&0x80u)?-(int)(o[6]&0x7Fu):(int)(o[6]&0x7Fu);
    const int vy=(o[7]&0x80u)?-(int)(o[7]&0x7Fu):(int)(o[7]&0x7Fu);
    const int dx=tx-(int)som_tac_u16(o+2),dy=ty-(int)som_tac_u16(o+4);
    int w=0;
    if(vx&&som_tac_sign(vx)==som_tac_sign(dx)){o[6]=0;w=1;}
    if(vy&&som_tac_sign(vy)==som_tac_sign(dy)){o[7]=0;w=1;}
    if(w)t->writes++;
    return w;
}
/* Delays the native swing: the gauge is raised to `frames`, never lowered. */
static inline int som_tac_hold_swing(SomTac *t,SomTacFoe *f,uint8_t *o,unsigned frames){
    if(frames>45u)frames=45u;
    if(o[0x1C]||o[0x1ED]>=frames)return 0;
    o[0x1ED]=(uint8_t)frames;f->hold_gauge=(uint8_t)frames;t->writes++;
    return 1;
}

/* Wanted direction for an action (sign per axis), relative to the target. */
static inline void som_tac_want(const SomTacFoe *f,unsigned action,int dx,int dy,int d,int opt,
                                int *sx,int *sy){
    *sx=0;*sy=0;
    const int ax=som_tac_sign(dx),ay=som_tac_sign(dy);   /* toward the target */
    switch(action){
    case SOM_TA_APPROACH:case SOM_TA_PUNISH:
        /* attacks go in four directions: get into the target's row or
         * column first (the smaller offset), then close along it */
        if(abs(dx)>=abs(dy)){
            if(abs(dy)>3)*sy=ay;
            if(d>opt-4&&abs(dx)>4)*sx=ax;
        }else{
            if(abs(dx)>3)*sx=ax;
            if(d>opt-4&&abs(dy)>4)*sy=ay;
        }
        break;
    case SOM_TA_RETREAT:case SOM_TA_DISENGAGE:case SOM_TA_ZONE:
        if(action!=SOM_TA_ZONE||d<opt){*sx=-ax;*sy=-ay;}
        else if(d>opt+12){*sx=ax;*sy=ay;}
        break;
    case SOM_TA_SIDESTEP:case SOM_TA_FLANK:{
        /* perpendicular to the line to the target, on its chosen side */
        int px=-ay*f->side,py=ax*f->side;
        if(!px&&!py)px=f->side;
        *sx=px;*sy=py;
        if(action==SOM_TA_FLANK){
            if(d>opt+16){*sx+=ax;*sy+=ay;}
            else if(d<opt){*sx-=ax;*sy-=ay;}
            *sx=som_tac_sign(*sx);*sy=som_tac_sign(*sy);
        }
        break;}
    case SOM_TA_OKI:
        if(d>opt+8){*sx=ax;*sy=ay;}else if(d<opt-4){*sx=-ax;*sy=-ay;}
        break;
    default:break;
    }
}

/* One frame for one foe. Returns the action in effect. */
static inline unsigned som_tac_foe_frame(SomTac *t,unsigned slot,uint8_t *ram,unsigned long frame){
    uint8_t *o=ram+SOM_TAC_FOE_BASE+SOM_TAC_OBJECT*slot;
    SomTacFoe *f=som_tac_foe_sync(t,slot,o,frame);
    if(f->archetype==SOM_ARCH_NATIVE){f->action=SOM_TA_NATIVE;return SOM_TA_NATIVE;}
    const SomFightLevel *L=som_tac_level(t);
    const int fx=(int)som_tac_u16(o+2),fy=(int)som_tac_u16(o+4);
    som_tac_learn(t,f,fx,fy,frame);
    /* own attack transitions: hit-and-run and frame-trap timing */
    const int own_attack=o[0x1C]==0x80u||o[0x1C]==0x20u;
    if(!own_attack&&f->own_attacking){
        f->own_attack_end=frame;
        if((som_tac_allowed(t,f)&SOM_TT_FRAMETRAP)&&!o[0x1ED])
            som_tac_hold_swing(t,f,o,6u+som_tac_rand(&f->rng)%14u);   /* vary the next gap */
    }
    f->own_attacking=(uint8_t)own_attack;
    /* target: nearest valid hero on the same layer within sight */
    int target=-1,best=0x7FFF;
    for(unsigned h=0;h<SOM_TAC_HEROES;h++){
        const SomTacHero *hero=&t->hero[h];
        if(!hero->valid||hero->layer!=f->layer)continue;
        const int d=som_tac_dist(hero->x,hero->y,fx,fy);
        if(d<best){best=d;target=(int)h;}
    }
    if(target<0||best>SOM_TAC_SIGHT){
        f->action=SOM_TA_NATIVE;f->target=0xFF;f->role=SOM_ROLE_NONE;f->token_until=0;
        snprintf(f->reason,sizeof(f->reason),"no hero in sight");
        return SOM_TA_NATIVE;
    }
    if(f->target!=(unsigned)target){f->target=(uint8_t)target;f->token_until=0;}
    const SomTacHero *hero=&t->hero[target];const SomTacTrack *tr=&t->track[target];
    /* never act through native attacks, hurt, casts or status */
    if(o[0x1C]||o[0x1F1]||(som_tac_u16(o+0x190)&0x00FFu)){f->last_x=fx;f->last_y=fy;return f->action;}
    int mn,op,mx;som_tac_band(f,hero,&mn,&op,&mx);
    const int dx=hero->x-fx,dy=hero->y-fy;
    /* decide */
    if(frame>=f->next_eval||frame>=f->action_until){
        f->next_eval=frame+L->interval;
        t->evals++;
        SomTacSituation s;memset(&s,0,sizeof(s));
        s.d=best;s.band_min=mn;s.band_opt=op;s.band_max=mx;s.hero_reach=hero->reach;
        s.phase=hero->phase;
        s.hp_pct=f->max_hp?(int)(f->hp*100u/f->max_hp):100;
        /* a whiff is seen only after the reaction time, once */
        if(tr->whiff_seq!=f->whiff_taken[target]&&frame>=tr->whiff_at+som_tac_react(t,f)){
            /* reachable: the gap to its range fits in what is left of the
             * opponent's recovery at about 2 px per frame */
            const int left=45-(int)(frame-tr->whiff_at),gap=best-op;
            if(left>0&&gap<=2*left&&som_tac_pct(&f->rng)<L->punish_pct)s.whiff_ready=1;
            if(!s.whiff_ready||frame-tr->whiff_at>=45u)f->whiff_taken[target]=tr->whiff_seq;
        }
        s.recovery_seen=hero->phase==SOM_COMBAT_RECOVERY&&frame>=tr->attack_end+som_tac_react(t,f);
        s.ready=!o[0x1ED];s.soon_ready=o[0x1ED]<12u;
        s.hero_ready=hero->phase==SOM_COMBAT_NEUTRAL||hero->phase==SOM_COMBAT_APPROACHING||
                     hero->phase==SOM_COMBAT_STARTUP||hero->phase==SOM_COMBAT_ACTIVE||
                     hero->phase==SOM_COMBAT_RETREATING;
        s.own_recent=f->own_attack_end&&frame-f->own_attack_end<24u;
        /* pressure tokens on this hero from monsters near it */
        unsigned used=0;
        for(unsigned k=0;k<SOM_TAC_FOES;k++){
            const SomTacFoe *g=&t->foe[k];
            if(k==slot||!g->live||g->target!=(unsigned)target||frame>=g->token_until||
               g->layer!=f->layer||!t->view[k].live)continue;
            if(som_tac_dist(t->view[k].x,t->view[k].y,hero->x,hero->y)>SOM_TAC_GROUP)continue;
            used++;
        }
        s.token_mine=frame<f->token_until;
        s.tokens_used=L->pressure&&(som_tac_allowed(t,f)&SOM_TT_GROUP)&&used>=L->pressure;
        som_tac_score(t,f,(unsigned)target,&s,frame,f->score);
        som_tac_pick(t,f,f->score,frame);
        if(f->action==SOM_TA_PUNISH&&s.whiff_ready)f->whiff_taken[target]=tr->whiff_seq;
        /* roles: pressure holders take a token; others flank, zone or wait */
        const int pressing=f->action==SOM_TA_APPROACH||f->action==SOM_TA_PUNISH||f->action==SOM_TA_BAIT;
        if(L->pressure&&pressing&&!s.tokens_used){
            if(frame>=f->token_until)f->token_until=frame+150u;
            f->role=SOM_ROLE_PRESSURE;
        }else if(!pressing){
            f->token_until=0;
            f->role=(uint8_t)(f->action==SOM_TA_FLANK?SOM_ROLE_FLANKER:f->action==SOM_TA_ZONE?SOM_ROLE_ZONER:
                    s.tokens_used?SOM_ROLE_RESERVE:SOM_ROLE_NONE);
        }
        snprintf(f->reason,sizeof(f->reason),"%s at %d (%d-%d)",som_combat_phase_names[hero->phase],
                 best,mn,mx);
    }
    /* act */
    unsigned act=f->action;
    if(act==SOM_TA_BAIT){
        /* stage 0 step in, 1 hesitate, 2 step out; whiff during it: punish */
        if(f->bait_stage==0&&best<=hero->reach+8)f->bait_stage=1,f->action_until=frame+10u+som_tac_rand(&f->rng)%12u;
        else if(f->bait_stage==1&&frame>=f->action_until)f->bait_stage=2,f->action_until=frame+36u;
        else if(f->bait_stage==2&&frame>=f->action_until){
            f->cooldown[SOM_TA_BAIT]=frame+som_tac_cooldown[SOM_TA_BAIT];
            f->action=SOM_TA_NATIVE;f->action_until=frame;act=SOM_TA_NATIVE;
        }
        if(f->bait_stage==0&&frame-f->action_since>90u){
            f->cooldown[SOM_TA_BAIT]=frame+som_tac_cooldown[SOM_TA_BAIT];
            f->action=SOM_TA_NATIVE;act=SOM_TA_NATIVE;
        }
    }
    int sx=0,sy=0;
    if(act==SOM_TA_BAIT)
        som_tac_want(f,f->bait_stage==2?SOM_TA_RETREAT:f->bait_stage==1?SOM_TA_WAIT:SOM_TA_APPROACH,
                     dx,dy,best,hero->reach+6,&sx,&sy);
    else som_tac_want(f,act,dx,dy,best,op,&sx,&sy);
    /* blocked: no progress while steering, try the other side, then give up */
    if(sx||sy){
        if(fx==f->last_x&&fy==f->last_y&&(o[6]||o[7])){
            if(++f->stuck>=8u){
                f->stuck=0;
                if(!f->detour){f->detour=1;f->side=(int8_t)-f->side;}
                else{f->cooldown[act]=frame+90u;f->action=SOM_TA_NATIVE;f->action_until=frame;act=SOM_TA_NATIVE;sx=sy=0;}
            }
        }else f->stuck=0;
        if(f->detour&&act!=SOM_TA_NATIVE){
            /* go around: the perpendicular on the new side */
            const int px=-som_tac_sign(dy)*f->side,py=som_tac_sign(dx)*f->side;
            if(px||py){sx=px;sy=py;}
        }
    }
    if(sx||sy)som_tac_steer(t,o,sx,sy);
    else if(act==SOM_TA_WAIT||act==SOM_TA_OKI||(act==SOM_TA_BAIT&&f->bait_stage==1))
        som_tac_hold_ground(t,o,hero->x,hero->y);
    /* swing timing: hold it while waiting, baiting in, retreating, flanking,
     * or while the target is down (no attack into a knockdown). */
    /* Only waiting, baiting in and lying opponents hold the swing: a monster
     * that retreats, flanks or zones keeps its native attacks. */
    const int down=hero->phase==SOM_COMBAT_KNOCKDOWN;
    if(act==SOM_TA_WAIT||act==SOM_TA_OKI||(act==SOM_TA_BAIT&&f->bait_stage<2)||down)
        som_tac_hold_swing(t,f,o,down?30u:12u+(unsigned)f->p.patience/32u);
    f->last_x=fx;f->last_y=fy;
    return act;
}

/* The whole layer for one frame. `ram` is 7E/7F WRAM. Bosses (ids 0x54+,
 * and ids without an ordinary swing row) and the familiar are untouched. */
static inline void som_tac_frame(SomTac *t,uint8_t *ram,const SomTacHeroExtra *extra,int familiar_slot,
                                 unsigned long frame){
    if(t->level==SOM_FIGHT_CLASSIC)return;
    const unsigned map=som_tac_u16(ram+0xDC);
    if(map!=t->map||frame<t->last_frame||frame-t->last_frame>2u){
        const unsigned level=t->level;const int arena=t->arena_archetype;
        int rank_slot[SOM_TAC_FOES];uint8_t rank[SOM_TAC_FOES];
        memcpy(rank_slot,t->rank_slot,sizeof(rank_slot));memcpy(rank,t->rank,sizeof(rank));
        som_tac_reset(t,level);t->arena_archetype=arena;
        memcpy(t->rank_slot,rank_slot,sizeof(rank_slot));memcpy(t->rank,rank,sizeof(rank));
        t->map=map;
    }
    t->last_frame=frame;
    som_tac_observe(t,ram,extra,familiar_slot,frame);
    for(unsigned s=0;s<SOM_TAC_FOES;s++){
        uint8_t *o=ram+SOM_TAC_FOE_BASE+SOM_TAC_OBJECT*s;
        if((int)s==familiar_slot||!som_tac_foe_ok(o)){t->foe[s].live=0;continue;}
        som_tac_foe_frame(t,s,ram,frame);
    }
}

/* Debug text for one monster (overlay). Returns lines written. */
static inline unsigned som_tac_describe(const SomTac *t,unsigned slot,char lines[][40],unsigned max){
    const SomTacFoe *f=&t->foe[slot];
    if(!f->live||max<4u)return 0;
    unsigned n=0;
    snprintf(lines[n++],40,"%s %s",som_arch_names[f->archetype],som_rank_names[f->rank]);
    snprintf(lines[n++],40,"%s %s",som_tac_temper(f,f->archetype),som_fight_names[t->level]);
    snprintf(lines[n++],40,"%s P%u %s",som_tac_action_names[f->action],
             f->target<SOM_TAC_HEROES?f->target+1u:0u,som_role_names[f->role]);
    snprintf(lines[n++],40,"%s",f->reason);
    if(n<max&&f->target<SOM_TAC_HEROES){
        int bh=-1;unsigned bv=0;
        for(unsigned k=0;k<SOM_HABITS;k++)if(f->habit[f->target][k]>bv){bv=f->habit[f->target][k];bh=(int)k;}
        if(bh>=0)snprintf(lines[n++],40,"%s %u%%",som_habit_names[bh],bv*100u/255u);
    }
    return n;
}
#endif
