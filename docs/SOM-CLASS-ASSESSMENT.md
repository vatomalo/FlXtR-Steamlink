# Secret of Mana classes: assessment (2026-11-16)

What each of the 12 disciplines does on every button, what the AI does with
it, and what is still missing. Source of truth: `src/som_disciplines.h`
(dodge, X role, third weapon), `src/som_ai_proxy.c` `som_disc_weapons`
(B/Y), `src/som_magic.h` (spells), `tools/som_class_director.py` (AI
class choice), `tools/som_class_arts.py`, `tools/som_jutsu.py`,
`tools/som_weapon_tactics.py`, `tools/som_weapon_ranges.py`.

Shared by every class: B primary weapon, Y secondary, R2 + D-pad seals +
B/Y for instant charge levels, combo finisher (hold B/Y), Start for class
progress. Short-range weapons: glove, sword, axe, spear. Long-range: whip,
bow, boomerang, javelin.

## Roles at a glance

| # | Class | Role | B / Y | X | A | AI uses |
|---|-------|------|-------|---|---|---------|
| 0 | Swordsmanship | Front-line blade enchanter | sword / axe | Sabers: Flame, Thunder, Ice | Evade hop; R1+A bash | Swings, charges, guard-counter; **casts a saber as a fight starts (new)**; bash not used |
| 1 | Martial Arts | Fast close fighter | glove / spear | Speed Up, Lunar Boost, Defender | Evade hop | Swings; **casts a body buff as a fight starts (new)** |
| 2 | Ninjutsu | Trickster | sword / boomerang | Fire Ball, Freeze, Air Blast, Sleep Flower | Evade; Kage Bunshin with the boomerang | Attack spells, Kawarimi before hits, Shunshin explosive clones |
| 3 | Magic | Ranged caster | whip / boomerang | Attack spell of every spirit | Native A | Attack spells at range |
| 4 | Divine Arts | Healer | spear / whip | Cure Water, Remedy, light | Native A | Heals under 40 % HP, Lucent Beam |
| 5 | Dark Arts | Drainer | whip / javelin | Shade, Energy Absorb, hexes | Native A | Attack spells |
| 6 | Summoning | Summoner | boomerang / whip | Moon and forest | Native A | Familiars; attack spells |
| 7 | Support & Dance | Buffer / hexer | whip / glove | Speed Up, Defender, Cure Water, hexes | Native A | Heals; **casts a buff as a fight starts (new)**; attack hexes |
| 8 | Weapon Flow | Weaponsmaster | sword / spear | **Third weapon: bow (new, no magic)** | Native A | **Picks sword, spear or bow by distance (new)** |
| 9 | Roguecraft | Skirmisher | sword / bow | Hexes, Sleep Flower | Evade hop | Attack hexes; sword up close, bow at range |
| 10 | Bushido | Samurai | katana (sword) / yumi (bow) | **Iaijutsu: charge level 3, hold X level 4, every 3 s (new, no magic)** | Evade hop | **Draws Iaijutsu instead of a plain swing: level 4 against tough enemies or crowds (new)** |
| 11 | Archery | Ranged marksman | bow / javelin | Wind and stone shots | Evade hop | Lines up and shoots from range, kites |

## Changes made in this pass

- **Bushido (samurai) loses magic.** X is Iaijutsu: on release the katana goes
  straight to charge level 3 (hold X 0.5 s: level 4) and the power attack
  fires, capped at the level the sword has learned. One per 3 s, shown as
  "Iaijutsu not ready" before then. AI: `iai_3` / `iai_4` replace a planned
  swing.
- **Weapon Flow (weaponsmaster) loses magic.** X equips the bow and attacks
  with it (the game sees B, so holding X charges the bow). Sword, spear and
  bow cover short, reach and long range. AI: the weapon tactics choose among
  all three by distance, HP and levels.
- **Swordsmanship keeps its sabers.** The class has no other way to deal
  elemental damage, and a saber is a weapon enchantment, not standing back
  to cast. The AI used to never cast any self buff; it now casts one when a
  fight starts and renews it every 30 s. The same applies to Martial Arts
  and Support & Dance.

## Gaps and recommendations (not done yet)

1. **Swordsman bash (R1+A) for the AI.** The bash lunge closes 3 px a frame;
   the AI could use it to close on a ranged enemy. Needs an `bash` command.
2. **Dodge classes without native A dash.** Classes 0, 1, 2, 9, 10, 11 lost
   the game's A dash to the evade; the AI's long trips are slower for them.
3. **Martial Arts identity.** Glove + spear is the same reach band as the
   swordsman; consider a counter stance (guard then auto-strike) on X hold.
4. **Magic / Dark Arts / Summoning overlap.** All three are "cast attack
   spells"; the director tells them apart only by spell list. Summoning
   could lean on familiars more (summon before engaging).
5. **Roguecraft.** Sword/bow is the samurai's pair now that both use it;
   consider dagger-style speed (combo bonus) or a steal on hit.
6. **Archery vs Weapon Flow at range.** Archery keeps the bigger range bonus
   so the director still picks it for far enemies.
7. **Kawarimi / Shunshin on a button.** AI-only for now; L1 was suggested.
8. **Third weapon equip needs the art check.** Direct equip of the bow uses
   the same verified weapon-art path as B/Y; if the bow's art row is not
   verified on a save, X shows the ring notice instead.
