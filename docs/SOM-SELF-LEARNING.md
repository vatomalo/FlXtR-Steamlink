# Outcome learning and native doorway memory

Baseline: `07f1abe209cccaad4478c7787a481955b502a8fe` on
`feature/som-ai-memory-agent-20261112`. Work and validation: 2026-11-13.

This extends the existing CombatLearner and map memory. It does not implement
discipline XP, new techniques, model-weight training, RAM writes or ROM patches.
Qwen remains asynchronous and optional; the deterministic planner executes inputs.

## Combat learning

Only live, ring-controlled sessions with persistent history instantiate the
learner. Offline dumps and dry runs cannot train it. Ordinary attacks require a
ready weapon, controller-bound Boy, no companions, exactly one valid occupied
enemy slot, normal status, uncharged verified weapon, settled verified field
state and plausible numeric data. Unavailable characters' stale equipment does
not qualify them as attackers.

A sent command is an attempt, not a success. Learning requires an observed
ready-to-recovery weapon-gauge transition and a changed outcome sample. Hits
correlate enemy HP loss with that isolated attempt; misses require the existing
0.8-second observation window. Identical samples cannot mature into misses.
Timeout (1.6 seconds), map/identity/weapon/max-HP changes, healing, disappearance,
menus, charge/block actions, connection failure and takeover-marker loss discard
the attempt. A disappearing slot is never scored as a kill. Pending attempts
are not resumed across service restarts.

SQLite migrations use a savepoint and component version table
`combat_learning_schema` (version 1), leaving the history schema and
`PRAGMA user_version` intact. `combat_learning_attempts` retains a command ID,
before/after evidence, elapsed time and hit/miss/discard reason. Outcome evidence
and the existing `learned_attack_ranges` aggregate update transactionally.
Reusing an explicit attempt ID cannot score twice. Old range statistics are
preserved; they have no retroactive evidence ledger.

Practice now counts eligible planning opportunities in persistent
`combat_learning_choices`, rather than counting outcomes that cannot occur when
the planner always chooses to close distance. Every eighth eligible opportunity
may retry a poorer range when healthy. Low HP suppresses exploration; survival,
Candy, inn and shop overrides remain ahead of training. Recovery and planned
charged attacks do not consume normal-attack practice opportunities.

These remain conservative statistical correlations. Raw WRAM has no publication
sequence or actor-generation counter. A same-type slot replacement entirely
between observations, reload/replay provenance and unobserved environmental
effects cannot be conclusively excluded. Do not feed these records into
discipline XP as verified engine events.

## Learning doors and choosing the right room

Native reference: `cidwel/som-decomp`, `docs/maps.md` sections 7 and 8:
https://github.com/cidwel/som-decomp/blob/main/docs/maps.md

Tile attribute byte 1 bit 4 identifies a native return door; it invokes the
saved return place rather than a plain ROM transition. Some separate house
rooms share map ID 257. Routing to a shop by map ID alone therefore sent the
hero toward an unreachable counter in another room.

`som_doors.py` scans the active layer of a settled, verified live work map.
Candidates are stored as `places.kind=return_door`, with source
`attribute_b1_bit4`. Only an observed map crossing near that candidate changes
its source to `observed_map_transition`. Confirmed lessons survive revisits and
restarts. An ordinary tile with the same graphic/tile ID is not a door.

When a destination in the same map has no connected route, the planner follows
a verified route to a reachable native return door. It then uses ROM exit
arrival coordinates and learned destination-room connectivity to select the
correct entrance. A confirmed connected arrival is preferred to an ambiguous
old map edge. Without a verified route it waits. No blind Confirm presses or
special teleport inputs are used.

## Validation and deployment

204 Python regression tests pass on SkyLoft, including 21 combat-learning
tests and 9 doorway tests. Python syntax and diff-whitespace checks pass.
The existing facing test was updated to put its facing-only cases inside the
planner's existing 28-pixel axial / 14-pixel lateral strike envelope; an
out-of-range case now explicitly asserts that it does not attack.

Live Turbo observation verified the native layer-2 tile 0x7F at (14,18) and
(15,18) in map 257 has attributes `[16,16,0,0]`. The AI walked out of the
disconnected room, crossed to Potos (131), selected the entrance at (13,30),
returned to map 257 at the shop's room near (31,17), and reached the taught
counter at (31,15). SQLite records (257, layer 2, 15,18) as a return door to 131
with source `observed_map_transition`. This is live behavior, not a synthetic
test or static-dump claim.

Python changes were deployed to the existing SkyLoft checkout and user service.
The installed proxy core, ROM file and save files were not replaced. No proxy
rebuild was needed.

The shop-room return door at (257, layer 2, 31,20) was subsequently confirmed
back to map 131 as well. Both confirmed lessons survived a service restart;
SQLite `quick_check` returned `ok`.

Live combat on map 130 contained three occupied Rabite slots, each initially
40 HP. Eligibility stayed false and the new attempt ledger remained empty,
as required for ambiguous multi-enemy encounters. The controller lost that
fight; it has not demonstrated an isolated live hit under the stricter gates.
Synthetic tests demonstrate outcome updates and subsequent changed preferences,
but this is not a claim of live tactical improvement.

AI control was stopped by clearing the existing enabled marker. A guarded
RemoteLink read then returned None, and the service logged its safe exit. The
supervised service is left waiting for a new Action Grid start. No human
physical-button takeover test was performed. Remaining live work: an isolated
normal-attack hit/miss trial and improved multi-enemy survival. Discipline XP
remains unimplemented and disabled.
