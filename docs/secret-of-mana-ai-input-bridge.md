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

**Important:** The proxy currently checks the *game filename* and
`FLXTR_SOM_AI_TEST=1`, not a runtime SHA-256. The checksum above must be
verified separately before any live session. The 180-frame delay is counted
from game launch, **not** from reaching gameplay. If a menu/title screen
is still visible after three seconds, the synthetic input will not
demonstrate Randi movement. Never treat this as autonomous play.

## Build and unit tests on SkyLoft

```bash
cd ~/builds/FlXtR-SOM-AI-Test
git fetch origin
git switch experiment/som-turbo-ai-input-bridge-20261111
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
4. Start with `FLXTR_AI_TEST` unset and validate ordinary controller
   input and core loading; capture verbose RetroArch logs.
5. In an opt-in test with `FLXTR_SOM_AI_TEST=1`, the proxy presses RIGHT
   on emulated frames **180–209**, starting frame count at 0.
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
- Future phases: verify native core loading; add a live test-launch
  option; implement observation from verified Turbo RAM mappings;
  eventually replace the scripted input with the standalone AI policy.
