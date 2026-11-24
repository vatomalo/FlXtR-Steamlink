# Secret of Mana Ultimate: implementation audit and roadmap (2026-11-15)

Branch `feature/som-disciplines-controls-20261114`. "Headless" = the real Turbo ROM and the
player's SRAM run on the Steam Link through a copy of the proxy (paths redirected to
`/tmp/flxtrtest`), driven by scripted input with screenshots. "Live" = played by a person.

## 1. Audit

### Confirmed working (headless or on the device)
- Proxy build = deployed binary (byte-identical builds; installed `e4cc2ab` = commit `e0a7df8`).
- Class label under the hero; position now read from his own sprite (correct at room edges).
- Kage Bunshin: clones visible (drawn from VRAM), follow, stop at walls, swap with smoke.
- Proxy-driven dodges (swordsman/samurai roll, monk/rogue hop, archer back-step, Shunshin):
  movement, wall stop, status 0x0400 window, after-image trail.
- Swordsman R1+A bash: native run lunge (left the inn through the door in the test).
- Exit auto save state: full 531668-byte state written and loaded back.
- Soundtrack bridge: song event -> title -> genre -> yt-dlp search -> stream URL -> player
  started; jingle ignored; genre change swaps the track; exits with the game.
- Discipline XP: none awarded where no monster can be hit (no free XP).

### Implemented, needs live verification
- Combo ladder counter, hold-to-unleash finisher (`som_special_start` path was live-tested
  earlier for R2 specials), clone explosions and substitution against real monsters, XP from
  hits / near misses / spells / clones / familiar strikes, level-up bubbles.
- Soundtrack audio through `greenlink-audio` beside game sound effects; launcher hook
  (`packaging/greenlink.sh`, installed) on a real launch.
- Clone/dodge position writes (E002/E004) vs. camera, party followers and layer edges.

### Partially implemented
- Progression: XP and levels work; no perks, unlocks or synergies yet (levels only raise
  clone damage). Blocking, stealth, Divine/Dark Arts specifics not trained.
- Gambits music row (Off/genres) present; default genre file is `0` (Lo-Fi), so music
  starts on the next launch unless set to Off.
- Familiar / summon / Player 2: each works alone; no shared companion framework.
- Living world: native NPC walk verified 2026-11-13; schedules exist in tools; no
  relationships or event reactions.
- Hero attributes: STR/AGI/CON/INT/WIS are read from the record (+0x188..+0x18C) by the
  AI and summoning rules; no new attribute systems.

### Missing
- Enemy personality archetypes and adaptive behaviour (monster AI VM is fully decoded in
  som-decomp `monster-ai-opcodes.md`; only the familiar overrides it today).
- Generals / Nemesis system (no persistence, identity or memory).
- Perk/synergy framework, saving throws, stamina, morale, awareness.

### Regressions / technical debt
- Fixed today: pose-based evade/Shunshin/hop never played in field play (`12d7189`);
  `som_disc_award` had no caller (`e0a7df8`); every `.state.auto` was a 4 KB stub
  (`a119537`); clones refused to spawn on raised indoor floors (`ab632f6`).
- Start on the title screen opens the Disciplines overlay instead of the game.
- Discipline levels are per device, not per save slot.
- `tools/som_schedules/potos.json.bak` is an untracked stray file.
- The headless harness lives only in a job temp folder (not in the repo).

## 2. Roadmap (dependencies, impact, risk)

**Phase A - finish verification (low risk, blocks everything).** One live session in a
monster area: combo counter and finisher, clone explosions/substitution, dodge near-miss
XP, music with sound effects. Then fix Start-at-title. Add the headless harness to `tools/`
so these checks can be repeated without a person.

**Phase B - progression (in progress).** Done: verified XP with diminishing returns.
Next: a data-driven perk table (`src/som_perks.h`: prerequisite disciplines + levels ->
effect id), first effects that the proxy already controls (dodge distance/grace, clone
count/damage, finisher level cap, Weapon Flow combo bonus), then hybrid synergies from the
same table (e.g. Ninjutsu 10 + Swordsmanship 10 -> Shunshin ends in a free swing). Block XP
once the Turbo R-block intercept is traced (`pal_trace` during a block).

**Phase C - enemy personalities (medium risk).** Per-species archetype table applied in
the proxy each frame through the fields the familiar already drives safely (target slot
`+0x1AC`, attack cooldown `+0x1ED`, velocity): engagement distance, retreat below an HP
fraction, focus on the weakest hero, hold back while another attacks. No LLM per frame.

**Phase D - one persistent General (medium-high risk).** One named elite of a chosen
species in one region: identity keyed by (map, species), boosted HP via the record,
encounter log (outcome, weapon used, clones/magic seen) in a small versioned file, one
adaptation (e.g. keeps distance from the weapon that beat it, or ignores clones after
being fooled), and a debug line explaining why.

**Phase E - hierarchy, companions, consequences.** Generalise D (bounded roster, ranks,
promotion on events); shared companion framework for familiar/summon/Player 2.

**Phase F - attributes and living world.** Attribute-driven saves/resistances using the
existing stat bytes, stamina/morale, NPC relationships and reactions on top of the
existing schedules.

The weapon renderer validates palette indices, dimensions and duplicate
records before it hides native tiles. Verify visual fallback separately from
tile restoration. A successful unit test of the WRAM buffer does not prove
that every already-rendered frame has a drawable native pose.
