# Secret of Mana Turbo: experimental Libretro input proxy

This branch introduces a **side-by-side** Libretro proxy core. It forwards
the installed `snes9x2005_plus_libretro.so` core's calls and optionally
substitutes a RIGHT button on **controller port 0 / Player 1** for exactly
30 `retro_run()` calls. No ROM patch, RAM write, save modification, or
persistent override is performed.

## Reference

- ROM on Steam Link: `/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.smc`
- SHA-256: `455e90a2c37534630d90eff7ad7abce8f9346d63280ef2c70ec9aa829f4041f4`
- Running core observed: `/home/apps/retroarch/cores/snes9x2005_plus_libretro.so`
- Normal RetroArch remains `/home/apps/retroarch/retroarch.exec`
- Steam Link Tailscale IP observed: `100.124.74.13`

## Multi-action controller interface (experimental)

The proxy now accepts one-shot action commands through
`/tmp/flxtr-som-ai-action`. These actions use the normal P1 Libretro
controller and have **not** all been verified against the Turbo menus.

Available commands: `right`, `left`, `up`, `down`, `attack`,
`open_ring`, `confirm`, `cancel`, `next_ring`, `prev_ring`,
`step_right`, `step_left`, `step_up`, `step_down`, `wait`, and `snapshot`.
Full movement lasts 30 emulated frames, tile-aware steps last six, and buttons
pulse for two frames. The legacy `touch /tmp/flxtr-som-ai-go` still sends Right.

To send an action while the AI proxy is running:

```sh
ssh root@100.124.74.13 'printf "open_ring\\n" > /tmp/flxtr-som-ai-action'
ssh root@100.124.74.13 'printf "snapshot\\n" > /tmp/flxtr-som-ai-action'
```

The `snapshot` action logs the *raw* candidate menu bytes at WRAM
offsets `0x180B`, `0x1846`, `0x1847` if the core's Libretro system
RAM buffer is large enough. These are **unverified for Turbo** and must
not be treated as reliable state signals or used for writes.

While the opt-in proxy is active, it also publishes a read-only raw WRAM image
to `/tmp/flxtr-som-wram.bin` every 30 emulated frames. Each 128-KiB image is
written under a temporary name and atomically renamed, so a remote observer
never reads a half-written frame. The files are removed when the game unloads.
This feed exposes bytes only; Turbo state mappings remain unverified.

The existing Action Grid can also start a waiting SkyLoft AI bridge without a
ROM or ring-data patch. Open Action Grid, enter its 4×4 selector, and confirm
the already-selected cell without moving it. The proxy preserves the native
personality and creates `/tmp/flxtr-som-ai-enabled` only after the ring closes.
Run `som_qwen_bridge.py` with `--ring-control --wram-remote` to wait for that
signal. Physical controller takeover, game unload, and core shutdown clear it.

Do not send commands concurrently; wait for each
`[som-ai] COMPLETE` log. The command file is consumed once and only
checked every six frames. Selection still must be explicitly set to
`som_ai_proxy`. No automatic Candy selection or HP-based healing
exists yet: those require validation of game RAM and menu operations.

## FlXtR per-ROM Core selection

After building and installing this **experimental branch of FlXtR**, open
**Games → Secret of Mana, Turbo → ROM Options → Core** and choose
`som_ai_proxy`. The option appears only when
`/home/apps/retroarch/cores/experimental/som_ai_proxy_libretro.so`
exists. **AUTOMATIC** or the previously selected standard core is still
the default; this is per-ROM, not a platform-wide setting.

Choosing `som_ai_proxy` makes FlXtR use that core for this specific ROM.
The proxy listens for the one-shot trigger automatically when the exact
Turbo filename loads; it does not depend on an environment variable. There is no longer a
need to start a competing RetroArch instance via SSH. Once gameplay has
started normally, run on SkyLoft:

```sh
ssh root@100.124.74.13 'touch /tmp/flxtr-som-ai-go'
ssh root@100.124.74.13 'grep "\\[som-ai\\]" /home/apps/greenlink/game.log 2>/dev/null || true'
```

The log location is installation-dependent; use the existing FlXtR game
runtime log (or run `grep -R '\\[som-ai\\]' /home/apps/greenlink/*.log`
where available).

**Save warning:** When launched from FlXtR, RetroArch uses normal game
save/state paths. Before testing, back up your Turbo `.srm` and save
states. The separate SSH test recipe below uses isolated `/tmp` saves
instead. Selecting a proxy core never itself isolates saves.

Rebuild the FlXtR Steam Link package using the established builder workflow
before expecting a new Core option; merely copying a `.so` will not
update the old FlXtR menu. PR remains experimental and unmerged.

## Runtime-triggered movement experiment (revision 2)

The proxy no longer triggers at startup. Select the experimental proxy core when
launching it. The ROM must have the exact Turbo filename, and you must
create a one-shot trigger file **after reaching active gameplay**.

For a test that protects existing saves, copy the ROM (and optionally
SRAM) to a separate folder. The proxy checks the filename, not a cryptographic
hash, so verify the copied ROM hash before launching it.

```sh
# On Steam Link; create a disposable isolated ROM directory
mkdir -p /tmp/flxtr-som-ai-sandbox
cp "/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.smc" \
   "/tmp/flxtr-som-ai-sandbox/Secret of Mana, Turbo.smc"
sha256sum "/tmp/flxtr-som-ai-sandbox/Secret of Mana, Turbo.smc"
# Optional: copy SRAM into the sandbox if you want to resume your existing save
# cp "/home/apps/retroarch/roms/Snes/Secret of Mana, Turbo.srm" /tmp/flxtr-som-ai-sandbox/
```

Launch RetroArch normally with the experimental proxy core, the sandbox
ROM, and selecting `som_ai_proxy`. Do not use `--max-frames`, since you
need to reach gameplay first. Run from the device console or a dedicated
SSH session; capture stderr to `/tmp/som-ai-interactive.log`.

Once Randi is controllable, execute in a **second** SSH session:

```sh
touch /tmp/flxtr-som-ai-go
```

Within 6 emulated frames the proxy consumes the file. It injects RIGHT
for 30 emulated frames, then releases it. Any physical joypad button
cancels the sequence. The log records how many RIGHT queries were actually
answered with injected input:

```sh
grep '\[som-ai\]' /tmp/som-ai-interactive.log
```

The trigger is discarded on game load, so a stale file from a prior
session cannot accidentally trigger takeover. This test is deliberately
single-use per game session; restart RetroArch for another test.

**Still unverified:** whether injected RIGHT causes visible Randi
movement in an actual gameplay state.

**Important:** The proxy currently checks the *game filename* and
selecting `som_ai_proxy`, not a runtime SHA-256. The checksum above must be
verified separately before any live session. The injection is triggered manually, not by a hardcoded startup delay. The reference SHA-256 is checked outside the proxy. This is not autonomous gameplay.

## Build and unit tests on SkyLoft

```bash
cd ~/builds/FlXtR-SOM-AI-Test
git fetch origin
git switch feature/som-ai-memory-agent-20261112
make test-som-ai-player2 test-som-ai-proxy
bash scripts/build-som-ai-proxy.sh
file build/som-ai/som_ai_proxy_libretro.so
```

Expected ARMv7 artifact: `build/som-ai/som_ai_proxy_libretro.so`.
The build script leaves it in that directory. It **does not install,
package, publish, or activate** the artifact.

## Live test plan (after confirming build and dynamic loading)

1. Make an external backup of any save states and the original core.
   Use an isolated test save; never use a valuable in-progress save.
2. Confirm the ROM hash, core path and build architecture.
3. Copy the new proxy **under a different filename** into an experimental
   directory, never overwrite `snes9x2005_plus_libretro.so`.
4. Start with the original Snes9x core selected and validate ordinary controller
   input and core loading; capture verbose RetroArch logs.
5. In an opt-in test using `som_ai_proxy`, create `/tmp/flxtr-som-ai-go` during actual gameplay to inject RIGHT for 30 frames.
6. If any physical Player 1 SNES joypad button is pressed, the test
   cancels immediately. Normal input always passes through afterward.
7. It logs to RetroArch stderr: armed, injection begin, physical
   cancellation, and completion.
8. Exit and launch the **original core** again to verify rollback.

The shell program currently launches RetroArch as a separate process; it
**does not yet expose a menu toggle to enable this proxy**. Do not click
FlXtR's standard Build & Publish thinking it turns on AI: the special
artifact must be independently cross-built, smoke-tested and selected
for a single test launch.

## Design boundaries

- AI input uses the Libretro `retro_input_poll_t`,
  `retro_input_state_t`, and `retro_run()` functions, not SDL button
  indices. Other Libretro calls are forwarded to the original core.
- All other ports are unchanged. No screen scraping or known RAM offsets.
- A keyboard-only press outside the controller callback may not cancel
  injection; use the controller or exit the emulator.
- The wrapper has **not** been validated against the exact Steam Link
  RetroArch ABI or hardware yet.
- Future phases: verify native core loading and the live WRAM export on
  hardware; map verified Turbo navigation fields; eventually replace the
  scripted input with the standalone AI policy.

## Experimental autonomous mode and local Qwen bridge

Build the experimental core again after commit `c5ffbf4`. Exit Turbo
before updating `/home/apps/retroarch/cores/experimental/som_ai_proxy_libretro.so`,
then reopen Turbo using the selected proxy.

To run a bounded, non-perceptive **ten-second** controller demo from SkyLoft:

```sh
ssh root@100.124.74.13 'printf "auto_on\n" > /tmp/flxtr-som-ai-action'
```

The demo cycles directions and basic attacks, has a 600-emulated-frame
limit and stops if a physical control is pressed. It **does not** know
where enemies, walls, HP, or the player's coordinates are. Do not
run it near hazards or during menus/cutscenes. The `auto_off` command
can also stop it after the current input pulse.

### Qwen connectivity demonstration

The new `tools/som_qwen_bridge.py` talks to an **existing local**
Ollama API on SkyLoft and sends one allowlisted controller action at
a time via SSH to the Steam Link. With `--wram-remote`, it receives the live
raw/partially decoded WRAM observation. It still has no screenshot or verified
navigation state, so this is not yet autonomous world understanding.

Prerequisites: Ollama serving a pulled Qwen model, and passwordless SSH
from SkyLoft to the Steam Link for non-interactive control. Verify
`ssh -o BatchMode=yes root@100.124.74.13 true` first.
You may check `ollama list` for installed models; use that exact model
name instead of assuming `qwen2.5:1.5b` is installed.

```sh
python3 tools/som_qwen_bridge.py --model qwen2.5:1.5b \
  --objective "Wander slowly and attack occasionally" --steps 5 --dry-run
python3 tools/som_qwen_bridge.py --model qwen2.5:1.5b \
  --objective "Wander slowly and attack occasionally" --steps 5
```

### Remaining combat/progression engineering

* Physical input attack-edge detection now logs `COMBO OBSERVED`
  when two or three B presses occur within 30 frames. This recognizes
  input patterns only; no attack animation cancelling is active.
* Real fighting-game finishers require verified Turbo animation,
  attack state, hitbox and recovery fields or a compatible ROM patch.
* Tap/hold Square magic shortcuts and contextual remapping have **not**
  been installed, because the ring/menu context is not yet verified.
* Skyrim-like progression will require validated hit events, per-weapon
  and spell usage, level thresholds, and save migration. No experience
  or ROM save data is modified by this experimental controller.

