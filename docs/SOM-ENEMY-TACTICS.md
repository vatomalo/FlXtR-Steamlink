# Enemy fighting-game tactics (Phase C)

`src/som_enemy_tactics.h` gives ordinary monsters fighting-game decisions: spacing,
whiff punishment, baiting, zoning, hit-and-run, okizeme, habit learning and group
pressure. It runs on top of the game's own monster AI and never replaces it. The
proxy glue is `som_enemy_fight_frame()` in `src/som_ai_proxy.c`.

Status, 2026-11-26: implemented and tested on synthetic WRAM and on the real Turbo
ROM (headless, bare core). **Not played live on the Steam Link yet.**

## 1. How it acts on the game (measured)

Everything the native AI does is still decided by the native AI: the monster's
script (som-decomp `monster-ai-opcodes.md`) runs every 5th frame in its slot and
issues hops, swings, casts and poses. The tactics layer only uses levers that were
measured on the Turbo ROM with `tools/som_headless/tactics_probe.c` (Silktail, map 52):

| lever | measured | how it is used |
|---|---|---|
| velocity `+0x06/+0x07` (sign-magnitude, `0x82` = -2 px/frame) during a native move | rewriting it steers the hop (right push: +225 px in 150 frames); zero holds it in place; pushed into walls it stops in all four directions (the game's terrain integration, C1:B93E, still runs) | approach, retreat, sidestep, flank, zone; only while the game itself moves the monster (velocity non-zero, state 0) |
| weapon gauge `+0x1ED` | op E8 swings only at gauge 0; holding it at 60 gave 0 attacks in 900 frames (native: 2); forcing it to 0 did **not** add attacks (the script decides) | delays a swing (wait, bait, okizeme, frame-trap gaps); only ever raised, never lowered, so recovery is never shortened |
| command bytes `+0x140..0x143` | the AI step dispatches them in the same call; rewriting the direction `+0x141` changed nothing | not used |

So a "punish" means getting into the native swing range while the opponent
recovers, without holding the swing back. The native script throws the attack.
No damage, target, invulnerability or animation is ever written.

Never touched: monsters that are attacking (`+0x1C` 0x20/0x80), hurt or casting
(any `+0x1C`), carrying pending damage (`+0x1F1`), with any status low bit (sleep,
poison and so on), dying (`0x8000`), the familiar's slot, boss ids 0x54 and up, and
the six ordinary ids without a swing row (0x20 0x2E 0x3C 0x3F 0x44 0x4C).

## 2. Observation

`som_tac_observe()` takes one snapshot per frame from WRAM plus what only the proxy
knows: guard (R2 / AI block), its own evades, Kage Bunshin clones and class weapon
reach. Heroes P1-P3 each have their own tracker (attack start and end, hit or whiff,
guard, evade side, clones, rush-in, retreat-after-attack and ranged events), latched
as sequence numbers until each monster has seen them.

Hero phase (`SomCombatPhase`), from the screen only: knockdown (status bit 2 or
animation 0x8E), stagger (0x40 with animation 0x88-0x8D), evade (proxy evade or
status 0x0400), casting (0x40 with `+0x60`), startup (the first 6 frames of 0x20/0x80),
active, guarding, recovery (weapon gauge refilling within 40 frames of an attack),
neutral. A whiff is an attack that ended while no monster lost HP during it or in
the 8 frames after it. No controller input is read.

## 3. Archetypes and personalities

Every ordinary species 0x00-0x53 has an explicit archetype (78 of 84; the other six
stay native), from the decomp survey: projectile users, casters, non-swingers and
stationary scripts are flagged. Examples: Rabite Rushdown, Mushboom Trapper, Chobin
Hood Zoner, Werewolf Technical, Dark Stalker Duelist, Silktail Skirmisher, Polter
Chair Ambusher, Steamed Crab Bruiser, Specter Caster, Tomato Man Coward, Buzz Bee
Swarm, Evil Sword Guardian, Grave Bat Berserker. The table is `som_species_table`.

An archetype is a `SomEnemyPersonality` (aggression, patience, courage,
adaptability, discipline, teamwork, range band min/opt/max, retreat HP threshold,
supported tactics). Each monster gets a bounded individual variation (±24 per
trait, ±4 px on its range) from a deterministic seed of map, slot, species and the
frame it appeared. The overlay names the strongest deviation ("Aggressive",
"Cautious", "Opportunistic" and so on). Variation never adds a tactic the archetype
lacks.

Technical, Duelist and Guardian monsters set their preferred range just outside
the observed hero's weapon reach (glove 24 ... spear 34, whip 48, bow 96, plus a
class style's reach bonus).

## 4. Decisions

Integer utilities over `NATIVE WAIT APPROACH RETREAT SIDESTEP PUNISH BAIT ZONE
DISENGAGE FLANK OKI`. They score the distance against the band, the hero's phase,
the monster's own readiness (gauge 0), whether the hero can hit back now, its HP,
habits and pressure tokens. On top of that come repetition penalties, per-action
cooldowns and time limits, hysteresis for the commitment time, and difficulty
mistakes or mix-ups (picking the second best when close).

The key rules, learned on the ROM: a monster that is ready and in its own range
lets the native script attack; it backs off only when it is cooling down and the
hero can actually hit back; it punishes only when its own swing is (nearly) ready;
and it approaches into the target's row or column first, because SoM attacks go
in four directions. Before these rules the layer made monsters passive (Silktail
attacks fell from 12 to 2): see section 7.

- **Footsies:** approach outside the band, stop at the preferred range, step out of
  a swing that can reach.
- **Whiff punish:** only after the level's reaction time (8-30 frames), within 45
  frames of the whiff, if the gap to range fits what remains of the recovery
  (about 2 px/frame), with the level's chance; the PUNISH action then steers in and
  never holds the swing.
- **Bait:** step to the hero's reach + 8 (swing held), hesitate 10-21 frames, step
  out for 36 frames; a whiff during it is punished; 90-frame limit, 200-frame cooldown.
- **Zoning:** projectile species and casters keep their band (Chobin Hood 48-76 px).
- **Hit-and-run:** after its own attack, if the hero can hit back, disengage.
- **Okizeme:** while the hero lies down, wait at range with the swing held; strike
  when they are up. No attack into a knockdown, no loops.
- **Frame traps** (hard via rank, expert): a random 6-19 frame gap after its own
  attack (only longer, never shorter).
- **Conditioning:** per monster, per hero habit scores (blocks, attacks on approach,
  retreats after attacking, fights from range, dodges left or right, uses clones,
  whiffs). Each seen event adds 12-48 by adaptability; they decay every second; a
  habit counts above the level's confidence (220 easy ... 90 expert). One event is
  never a habit. Effects: more patience against blockers, baits against rushers and
  whiffers, chasing backers, getting in on shooters, less commitment against clones.
- **Coordination:** pressure tokens per hero (hard 1, expert 2) counted only from
  monsters within 96 px of that hero on the same layer; the rest flank (a side
  chosen per monster) or wait in reserve. Tokens last 150 frames and are dropped
  on death, slot reuse, map change or state load.
- **Blocked movement:** no progress for 8 frames while steering, then it tries the
  other side; still stuck, it gives up the action for 90 frames and plays native.

## 5. Difficulty and rank

`FLXTR_SOM_FIGHT_DIFFICULTY=classic|easy|normal|hard|expert` (or the gambit menu
line "Enemy AI", saved in `som_ai_fight_difficulty`; the variable wins).
Independent of the HP/damage scaling.

| level | evaluate every | reaction | punish | mistakes | habit confidence | pressure | tactics |
|---|---|---|---|---|---|---|---|
| classic | - | - | - | - | - | - | none: native AI only, nothing written |
| easy | 24 f | 30 f | 25 % | 35 % | 220 | none | footsies, zoning, defence, punish |
| normal | 14 f | 18 f | 55 % | 18 % | 160 | none | + rush, hit-and-run, bait, conditioning, okizeme |
| hard | 10 f | 13 f | 80 % | 8 % | 120 | 1 per hero | all but frame traps |
| expert | 7 f | 10 f | 92 % | 3 % | 90 | 2 per hero | all |

Ranks (Common, Veteran, Elite, Captain, General, Nemesis) add tactics only inside
the level's envelope and shorten reactions by 2 frames per rank, never below 8.
A General's rank 1-5 maps to Veteran..General, and to Nemesis at rank 4+ once it
has a nemesis weapon. Classic ignores ranks.

## 6. Memory

- Encounter / individual: per monster record, tied to species, max HP and continuous
  observation. A new species in the slot, HP going up (a respawn), a gap of more
  than 2 frames (state load), a map change or a frame counter going backwards starts
  a new record with no memory.
- Persistent: Generals keep habits per hero in `som_generals.txt`, now
  `SOMGENERALS 2` with an `h` field of 24 hex bytes. Version 1 files still load
  (no habits). At election the general starts with 3/4 of what it remembered; at
  the end of an encounter the larger of (3/4 old, new) is kept.

## 7. Verification

Synthetic (`make test-som-enemy-tactics`, `tests/som_enemy_tactics_test.c`): a model
of the native AI on fake WRAM. It covers classic writing nothing; unknown and boss
ids untouched; rushdown approach; zoner keeping range (and closing in without the
layer); whiff only after the attack ends, not during startup, not on a hit, and not
out of reach; reaction delay; the wall; bait completing; bounded action changes;
no swing into a knockdown and swings after it; block habit forming gradually and
decaying; slot reuse, respawn, map change and state load clearing memory and
tokens; coordination limited to nearby monsters and tokens released on death;
non-swingers never punishing; difficulty differences; P1/P2/P3 independence; the
familiar ignored; statuses, death, attacks and the gauge untouched; deterministic
bounded personalities; rank envelope; and cost. Proxy glue
(`make test-som-ai-proxy`): env, menu and file, arena archetype and rank, General
rank mapping, habit carry-over, Classic leaving nothing behind. General file v1/v2
(`make test-som-general`).

Real ROM (Turbo, local snes9x2005 core, no proxy): `tools/som_headless/tactics_bench.sh`.
Silktail (map 52, slot 2) against one hero who swings every 0/35/60/90 frames, from
8 starting positions, 1800 frames each, the monster kept alive, the companions away
and the hero healed. Totals for 32 runs per level:

| level | monster attacks | damage to the hero | damage taken | dealt / taken |
|---|---|---|---|---|
| native | 175 | 5768 | 53336 | 0.108 |
| easy | 187 | 6386 | 57227 | 0.112 |
| normal | 184 | 6674 | 54591 | 0.122 |
| hard | 186 | 7075 | 54126 | 0.131 |
| expert | 173 | 7300 | 51945 | 0.141 |

An earlier version that retreated from every swing and held the swing while
retreating measured 12 -> 2 attacks and 253 -> 0 damage on the same state. The
readiness rules above came from that.

Cost: 0.6-0.7 µs per frame on x86 for three heroes and three monsters (synthetic
test, including the model); state 1.6 KB, no allocation. The proxy reports it as
its own perf phase `tactics` (`/tmp/flxtr-som-perf-enable`). It has not been
measured on the Steam Link's A9 yet; at a few hundred integer operations per monster
per evaluation it is expected to stay far below 0.1 ms.

## 8. Limits and next steps

- Only the Silktail was run on the ROM. Other species use the same levers but their
  scripts differ: a monster that turns in place (Polter Chair, Lullabud, Specter at
  range) issues no moves, so it cannot be steered. That is by design, but those
  species gain little.
- Charging, spell targeting and summons are not observed as separate habits yet;
  the familiar is excluded, and clones count through the proxy's Kage Bunshin.
- Hit attribution is shared: any monster losing HP during a hero's swing marks
  that swing as a hit.
- Bosses: no adapter yet (the plan is opt-in, verified per boss).
- Arena training: `FLXTR_SOM_FIGHT_ARENA=<archetype>` and `FLXTR_SOM_FIGHT_RANK=<rank>`
  apply only on map 361. They are environment variables, not arena menu entries.

## 9. Things to try in the game

Set `FLXTR_SOM_FIGHT_DEBUG=1` to see archetype, rank, personality, action, target,
role and reason over each monster, plus `[som-ai] FIGHT` lines in the log.

1. Rabites west of Potos (map 130), Normal: they come straight in; swing early at
   one and miss, and it should jump on your recovery (the overlay says "Whiff Punish").
2. Same place, Classic in the menu: native hopping again; the overlay disappears.
3. Chobin Hood: walk at it. It should back off to about 50-70 px and keep shooting,
   not let you stand next to it.
4. Werewolf (Expert): stand still with a spear. It should hover just outside the
   spear's reach and come in when you whiff.
5. Block (R2) a lot against one monster for 10-20 seconds: the overlay's last line
   shows "blocks NN%" and it waits longer before committing.
6. Three Rabites on Hard: one presses (role Pressure), the others flank or wait.
7. Rabbite Elite (Generals on): it shows its rank as Veteran/Elite...; after the
   fight `som_generals.txt` has an `h...` field; next visit it starts with those habits.
8. Load a save state mid-fight: no monster keeps a stale action or role.
