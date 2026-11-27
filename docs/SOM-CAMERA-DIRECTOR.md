# Camera director (2026-11-27)

`src/som_director.h` plans the shots, `src/som_camera.h` plays them (player: "this game must
look like its cinematics were planned, use a WFC state machine for camera?", then "add in
fighting game camera techniques for fights").

## Planning: wave function collapse over time

- Beats of 24 frames (0.4 s); the plan covers 8 beats (3.2 s).
- A tile is (shot, beats it has run so far, 0..4+): 12 shots x 5 = 60 tiles, one 64-bit set
  per beat.
- Rules: what the scene allows (talk, fight, peace, menus; what fits on screen at its zoom;
  1.5x only while hd is affordable), pins from events, and film grammar between beats
  (`som_dir_follow`: which shot may follow which; `som_dir_min` / `som_dir_max`: how long a
  shot holds). Arc consistency is propagated, the most constrained beat collapsed by weight
  (scene, the old plan kept x3, variety: recent shots weigh less, long holds tire except the
  steady wide / idle / two-shot), propagated again. A contradiction drops the scene rules,
  then falls back to wide. Solved every beat and at once on an event: ~12 us on x86.

## Shots

| shot | zoom | subject | notes |
|---|---|---|---|
| wide | 1x | the game's view | |
| establish | 2x -> 1x | the hero | arriving on a map: cut in close, slow pull-out, letterbox |
| medium | 1.5x | hero, foes, party | fights |
| lead room | 1.5x | hero + nearest foe | the hero on a third, room toward the foe |
| close | 2x | the hero | after a hard hit on him |
| foe | 2x | the nearest foe / the combo's foe | |
| party | 1.5x | the party | peace, after a kill |
| two-shot | 1.5x (2x without hd) | hero + speaker | dialogue |
| speaker | 2x | the speaker | alternates with the two-shot |
| punch-in | 2x | whack / finisher / counter spot | fast, shake, letterbox |
| pull-out | -> 1x | | quick; slow for a K.O. with letterbox |
| idle | 2x | the hero | standing still 10 s |

## Fighting-game techniques

- Impact shake by the hit's weight (`som_cam_impact`, from every observed HP loss).
- Heat: hits and damage heat the fight, the director cuts tighter (lead room, foe) when hot.
- Combo push-in: a combo level reached pins the foe close-up (`som_cam_combo`).
- Finishers (string levels 3+, combo finishers) punch in on the target (`som_cam_finisher_near`).
- Counters (Iai counter, counter palm): crash zoom, faster and with a harder shake.
- K.O.: the last foe down is a slow pull-out under letterbox bars.
- Not done: hit-stop (freezing the game) would gap the audio stream the music is mixed into.

## Bullet time (Monk, Samurai, Ninja)

A blow landing on the guard (R2) of a Monk, Samurai or Ninja may start bullet time: 6 %,
20 % under half HP, 40 % under a quarter; 20 s between two. For 2.5 s the monsters move and
charge at half speed (`som_bullet_world`: every other frame they are put back where they
stood and their attack gauge +0x1ED does not count down); the game, the heroes and the music
run at full speed ("but monk and the classes would be fast"). The director cuts close on the
hero (event `SOM_DIR_EV_BULLET`) with letterbox bars. The monsters' own animations still play
at full speed; projectiles already flying are not slowed. Unit-tested; not yet seen on the
real ROM (the Steam Link was offline).

## Events

arrive, fight start, kill, K.O., hero hurt (1/8 of max HP in one go), whack, finisher, combo,
dialogue line. Tests: `make test-som-director test-som-camera`. Real ROM: the monk's string on
map 130 cut establish -> lead room -> medium -> foe -> punch-in (finisher) -> pull-out.
