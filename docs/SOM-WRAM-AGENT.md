# Secret of Mana RAM-aware Qwen bridge (experimental)

Branch: `feature/som-ai-memory-agent-20261112`.

## What works in this checkpoint

- Read **raw 131072-byte SNES WRAM**, starting at SNES address `7E0000`.
- Decode documented enemy slots, party attributes, and weapon-use flag.
- Decode the hero's pixel position, current map, active 16×16 tile layer, and
  a compact 7×7 nearby tile/collision grid.
- Provide a compact observation to Ollama alongside the objective.
- Export a live, read-only 128-KiB WRAM image atomically from the opt-in
  Libretro proxy every 30 emulated frames.
- Optionally persist observations/actions in SQLite.
- Stop after 4 identical actions instead of looping forever.
- Preserve the existing SSH allowlisted action protocol.

Tile mapping fails closed: the grid is published as runtime-verified only when
the tile under the hero agrees with the actor's separately maintained tile ID,
ground-effect byte, and priority field. **Not yet implemented:** automated
movement feedback, pathfinding, or party member spawning. No claim of autonomous
exploration is made.

## Run tests

```sh
PYTHONPATH=tools python3 -m unittest discover -s tools -p 'test_som_*.py' -v
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
When the opt-in `som_ai_proxy` core is running the exact Turbo filename, it
copies exactly 128 KiB from `retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM)`
to `/tmp/flxtr-som-wram.bin` every 30 emulated frames. Publication uses a
temporary file plus atomic rename, and unload removes the snapshot so the
bridge does not consume stale state. Use `--wram-remote` to read it via SSH.

This makes the bytes live; it does **not** make unverified fields trustworthy.
The observation now includes nearby tile IDs and a compact passability grid.
Solid cells are marked `#`, open cells `.`, conditional slopes/layer switches
and moving ground `?`, and the actor `@`. Runtime cross-checking protects the
AI from consuming a shifted or incompatible Turbo layout, but real hardware
testing is still required before this grid drives autonomous movement.

Qwen uses six-frame `step_*` movement commands rather than the older 30-frame
manual movement pulses. A deterministic guard changes a proposed step to
`wait` unless the runtime-verified adjacent cell is open. Blocked (`#`),
conditional (`?`), malformed, and unverified grids all fail closed.

## Start from the existing Action Grid ring menu

Run the SkyLoft bridge with `--ring-control --wram-remote`, then open the
game's existing Action Grid, open its 4×4 selector, and confirm the currently
selected square without changing it. The proxy observes that harmless commit
transition and publishes `/tmp/flxtr-som-ai-enabled`; it does not alter the
ring, personality value, ROM, or WRAM. Closing the ring releases the start
signal. Any physical controller input after the short menu-close grace period,
game unload, or core shutdown removes the signal and the bridge stops.

```sh
python3 tools/som_qwen_bridge.py --ring-control --wram-remote \
  --model qwen3:0.6b --steps 30 --objective "Follow open tiles and explore safely"
```

The bridge process must already be running (or supervised as a SkyLoft
service). The Steam Link ring cannot itself launch a process on SkyLoft.

## Keep the SkyLoft bridge running

Install the provided user service from the AI checkout as the normal SkyLoft
user (not with `sudo`):

```sh
git switch feature/som-ai-memory-agent-20261112
git pull --ff-only
bash scripts/install-som-ai-service.sh
sudo loginctl enable-linger "$USER"
```

The service waits without controlling the game until the Action Grid gesture
creates the marker. It then runs continuously (`--steps 0`) until physical
takeover, game exit, or a safety failure clears the marker. `Restart=always`
restarts the waiting bridge after a process crash, but a safety stop cannot
resume movement because the marker is removed first.

Configuration is kept outside Git at `~/.config/flxtr/som-ai.env`. After
changing the model or objective, restart with:

```sh
systemctl --user restart flxtr-som-ai.service
journalctl --user -u flxtr-som-ai.service -f
```

The service needs passwordless BatchMode SSH to the Steam Link and the selected
Ollama model already installed locally. Check those independently with
`ssh -o BatchMode=yes root@100.124.74.13 true` and `ollama list`.

Mapping basis: the live work-map and collision-cache layout follows the
read-only reverse engineering in `cidwel/som-decomp` (`docs/maps.md` and
`docs/hero-movement.md`), while the 16×16 block/collision organization is
also documented by the Secret of Mana map-editor project. The runtime
cross-check remains mandatory because this target is the Turbo ROM hack.

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
