# Secret of Mana AI Player 2: first proof-of-concept

## Scope of this PR

This is a **headless test of controller-policy decisions**, **not** proof that
an actual SNES ROM can already be controlled. It does not load, bundle,
redistribute, modify, or launch a ROM. Run:

```sh
make test-som-ai-player2
```

Expected: `PASS: Secret of Mana Player 2 follow/attack and fail-closed states`.

The test simulates a verified Secret of Mana observation and checks that a
deterministic C policy generates SNES joypad inputs for following the party
leader, approaching an enemy, and using a basic weapon attack. Invalid
observations, menus, dead characters, or a player-2 slot not joined cause
**no injected input**, so the existing game-controlled companion stays intact.

## Next hardware experiment (not implemented by this test)

1. Use a legally obtained, correctly identified Secret of Mana SNES ROM
   (record region/revision and checksum privately, not in GitHub).
2. Load a save state **after Primm joins**. Make sure the second controller
   can actually take over Primm: press Start manually on a physical pad.
3. Confirm which SNES core FlXtR launches. The current FlXtR Games launcher
   starts external RetroArch: it is **not** already intercepting
   `retro_input_state_t`.
4. Add an opt-in input bridge in the RetroArch/libretro frontend that can
   substitute the joypad mask for **port 1 (second controller)**, with a
   physical-controller override and a hard disable switch.
5. Add a read-only bridge from `RETRO_MEMORY_SYSTEM_RAM` or equivalent,
   but **do not** assume coordinates, health or enemy offsets. Those depend
   on the exact ROM revision/core and must be independently verified.
6. Feed those validated observations to `som_ai_decide()` once per frame,
   and map returned mask bits to libretro SNES joypad IDs.
7. Validate on real gameplay: idle/menu fallback, following, attacking,
   getting stuck, no phantom Start/menu presses, and disconnect recovery.

## Boundaries

- `SomAiObservation` is a contract, not a RAM map. Never pass unverified
  memory into the policy. `verified_state` is a trust gate supplied by the
  adapter, not automatic inference.
- `som_ai_decide()` runs without network, GPU, or model inference; an
  optional SkyLoft LLM may propose *high-level strategy* later.
- The current code does not call into Libretro or RetroArch. It does not
  enable AI Player 2 in installed builds or alter existing ROM launches.
- SNES `B` is used for a simple weapon action. Spell menus, cooperative
  pathfinding, weapon charge, coordinate scrolling, and game mode detection
  remain for the actual adapter.
- The generic future design should reuse the controller/state interface,
  while storing game-specific observation mappings per verified title.
