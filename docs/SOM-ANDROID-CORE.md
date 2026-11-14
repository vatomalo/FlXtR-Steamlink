# Secret of Mana Turbo AI: Android proxy core (first port)

This branch ports the existing `src/som_ai_proxy.c` **Libretro core wrapper**
to FlXtR's Android NDK build. It does **not** port the SkyLoft Python controller
or bundle an offline LLM. Normal controller play should remain available.

## Building on SkyLoft

Use the build bot without publishing to the update channel:

```sh
flxtr-build --android --branch feature/som-ai-android-core-20261113 --no-publish
```

The existing Android CMake setup builds `libflxtr_som_ai.so` for
`armeabi-v7a` and `arm64-v8a`, alongside `libflxtr_snes9x.so`.
The APK must package **both libraries for the same ABI**. The SkyLoft build now
checks the signed APK payload and fails before handoff if either library is
missing for any requested ABI.

The proxy uses `dlopen("libflxtr_snes9x.so")` on Android. This is
different from the Steam Link's Snes9x 2005 Plus backend and is **not yet
validated with live Turbo WRAM**. Do not assume the RAM snapshot layout,
save-state layout, input timing or NPC movement are compatible until tested.

## Selecting the experimental core

With a ROM named **exactly** `Secret of Mana, Turbo.smc`, open that ROM's
existing per-game **CORE** selector, and choose `Secret of Mana AI` (core
ID `som_ai`). All other SNES ROMs continue to use `snes9x`; the proxy
does not become the default SNES emulator. Controller left/right now cycles
Automatic, Snes9x and Secret of Mana AI instead of only toggling Automatic.

The existing Android frontend loads `libflxtr_som_ai.so` as a normal
Libretro core. It creates the private command directory before loading:

`/data/user/0/org.flxtr.android/files/som-ai/`

The core reads/writes its existing file commands there (WRAM snapshot,
actions, overlays, NPC commands). Do not use `/tmp` or Steam Link's
`/home/apps` paths on Android. Do not grant storage permissions for them.

The opt-in NPC walk marker is also inside that private directory and remains
absent by default. No NPC RAM write should occur unless explicitly enabled
and the existing ROM/live-state gates approve it. The existing ROM patch file
is also absent by default.

## Connecting the Android core to SkyLoft

The Android core now uses the same SkyLoft planner as Steam Link through a
small HTTP bridge carried entirely over the app's embedded Tailscale network.
Both devices must be signed into the same tailnet.

1. In FlXtR on Android, open **Network and devices**, then
   **Secret of Mana AI bridge**.
2. Enter SkyLoft's Tailscale IPv4 address (the default is `100.69.7.81`) and
   press **Enable and copy endpoint**.
3. On SkyLoft, paste the copied value into
   `~/.config/flxtr/som-ai.env` as `SOM_AI_HTTP_ENDPOINT=...`.
4. Restart `flxtr-som-ai.service`, then launch Turbo with the SoM AI core.

Setting the Android endpoint does not replace Steam Link. SkyLoft keeps two
independent planner sessions alive, and either device—or both devices—can use
AI whenever its own in-game enable signal is active. Both planners share
learned world knowledge, but keep their current goals and live combat state
separate.

The endpoint contains a private random token. Do not post or share it. The
Android listener additionally rejects every device except the selected
SkyLoft Tailscale address. Disabling the core or handing control back removes
the enable marker and makes WRAM reads fail closed.

## Scope of this port

Implemented:
- Android NDK native target for the shared proxy source
- App-private IPC and patch/enable paths
- Android frontend core selection for exact Turbo filename
- Native SNES gamepad layout and basic compatibility tests

Still required for autonomous Android AI:
- Test the core against a real Android device/ROM (video, audio, input,
  128-KiB WRAM export and save states)
- Verify original WRAM assumptions against the bundled Snes9x core
- Validate the Android-to-SkyLoft Tailscale bridge during real gameplay
- Add controlled UI enable/disable for autonomous play and NPC schedules

The Android native build itself has **not** been executed by this commit.
Keep the original Android APK installed until the build and smoke tests pass.

## Initial device checks

1. Build with `--no-publish` and check both ABI libraries in the APK.
2. Launch Turbo with normal Snes9x, note video/audio/input.
3. Select SoM AI, verify video/audio/input and normal saves still work.
4. Confirm the WRAM file is produced with **exactly 131072 bytes** and
   contains the expected Turbo map/actor fields, then compare both cores.
5. Verify the absence of the NPC enable marker prevents all NPC writes.
6. Confirm non-Turbo ROMs cannot choose the experimental core.
7. Only after those checks, proceed to the Android AI controller port.
