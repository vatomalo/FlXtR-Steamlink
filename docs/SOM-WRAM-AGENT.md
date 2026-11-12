# Secret of Mana RAM-aware Qwen bridge (experimental)

Branch: `feature/som-ai-memory-agent-20261112`.

## What works in this checkpoint

- Read **raw 131072-byte SNES WRAM**, starting at SNES address `7E0000`.
- Decode documented enemy slots, party attributes, and weapon-use flag.
- Provide a compact observation to Ollama alongside the objective.
- Optionally persist observations/actions in SQLite.
- Stop after 4 identical actions instead of looping forever.
- Preserve the existing SSH allowlisted action protocol.

**Not yet implemented:** a live Libretro memory exporter, verified player
coordinates/map IDs/collision geometry, automated movement feedback, pathfinding,
or party member spawning. No claim of autonomous exploration is made.

## Run tests

```sh
PYTHONPATH=tools python3 -m unittest discover -s tools -p 'test_som_memory.py' -v
python3 -m py_compile tools/som_memory.py tools/som_qwen_bridge.py
```

## Use an existing raw WRAM dump

```sh
python3 tools/som_qwen_bridge.py \
  --wram-file /path/to/wram.bin \
  --history-db /tmp/som-observations.sqlite \
  --model qwen3:0.6b --steps 5 --dry-run
```

A dump is reread once per step. A static file is **not live perception**.
For Steam Link, an emulator-side adapter must copy exactly 128 KiB from
`retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)` while the game is running.
Only once such a producer exists should you use `--wram-remote`, which
reads the fixed path `/tmp/flxtr-som-wram.bin` via SSH.

**Do not feed serialized savestates** to `--wram-file`. Libretro
`retro_serialize()` outputs core-specific state, not plain WRAM.

## Known constraints

The public memory map documents enemies at `7EE600`, `7EE800`,
`7EEA00` and character structures at `7EE000` in 0x200-byte slots.
It does not establish a reliable enemy-slot-active flag, so the agent
is told `active: null`; it must not treat every slot as an active enemy.
Player position and map ID remain unknown until verified against the ROM/core.

Reference: https://wiki.superfamicom.org/seiken-densetsu-2

## P2/P3 recruitment experimentation

Girl and Sprite can potentially be injected through a **separate, opt-in
WRAM write interface** after their initialization and party/event flags
are mapped. This is not equivalent to selecting graphic 0x81/0x82 or
setting companion availability to 1. A working implementation needs
coherent character stats, slot activation, AI/control configuration,
sprite loading, and event state. Avoid writes against your only save.
Use disposable savestates, compare before/after naturally recruiting
each member, and only then introduce a reversible experimental command.
