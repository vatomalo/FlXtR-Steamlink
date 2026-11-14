# Secret of Mana AI on Android

Android now packages `libflxtr_som_ai.so` beside the pinned
`libflxtr_snes9x.so`. The proxy loads that sibling core and keeps all emulator
callbacks on the Libretro thread. WRAM, gambits, familiar state, actions and
overlay messages cross an in-process native API; Android does not need Python,
SSH, `/tmp`, or a writable core directory.

The existing SkyLoft Python planner remains the decision engine. Android sends
one 128 KiB WRAM snapshot at a time through FlXtR's embedded Tailscale node to
`100.69.7.81:43928`. SkyLoft replies with one allowlisted action and an optional
overlay message. The request blocks until the planner responds, so decisions
cannot overtake snapshots. The Steam Link build keeps its existing SSH and
atomic-file transport; `SOM_AI_ANDROID=0` remains the default.

## Build the APK

Use Linux or WSL with JDK 17, Android SDK 35, build-tools 35.0.0 and NDK
28.0.13004108. From the repository root:

```bash
export ANDROID_HOME="$HOME/Android/Sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
bash scripts/build-android.sh
```

The build fetches and verifies the pinned Android Snes9x binaries, builds the AI
proxy for `arm64-v8a` and `armeabi-v7a`, runs Android unit tests and lint, and
creates:

```text
dist/FlXtR-Android-preview.apk
```

For an arm64-only development build:

```bash
FLXTR_ANDROID_ABIS=arm64-v8a bash scripts/build-android.sh
```

Install or update without clearing the library or saves:

```bash
adb install -r dist/FlXtR-Android-preview.apk
```

## Start the SkyLoft service

SkyLoft needs Python 3 and the repository's `tools/` modules. They use the
Python standard library, including SQLite; no Python runtime or packages are
installed on Android. Hybrid policy additionally needs the configured local
Ollama model.

Set these values in `~/.config/flxtr/som-ai.env` on SkyLoft:

```bash
SOM_AI_ANDROID=1
SOM_AI_ANDROID_LISTEN=100.69.7.81:43928
SOM_AI_POLICY=hybrid
SOM_AI_MODEL=qwen3:0.6b
```

Then install or refresh the user service and inspect it:

```bash
bash scripts/install-som-ai-service.sh
systemctl --user restart flxtr-som-ai.service
systemctl --user status flxtr-som-ai.service --no-pager
ss -ltn | grep '100.69.7.81:43928'
journalctl --user -u flxtr-som-ai.service -f
```

Binding to the Tailscale address prevents a LAN-facing listener. If SkyLoft's
Tailscale address changes, update both `SOM_AI_ANDROID_LISTEN` and
`AdbSetup.SKYLOFT`, rebuild the APK, and restart the service.

## Run on Android

1. In FlXtR network settings, connect the built-in Tailscale node.
2. Import the ROM with the exact filename `Secret of Mana, Turbo.smc`.
3. Open the ROM's options and choose `SECRET OF MANA AI` as its core.
4. Start the game and reach controllable field play.
5. Hold R2 or R3 alone for one second, then release. The overlay reports AI on.
6. Press a controller button to open gambits; choose Exit to return control.
7. During manual play, L2+R2 still opens the Disciplines/class menu.

If SkyLoft or Tailscale is unavailable, gameplay continues and the overlay says
the AI service is unavailable. The original `SNES9X` core remains selectable as
an immediate per-ROM rollback.

Useful device diagnostics:

```bash
adb logcat -s FlXtR-Libretro AndroidRuntime
adb shell dumpsys package org.flxtr.android | grep versionName
```

## Verification

Run the transport and proxy checks from Linux or WSL:

```bash
make test-som-ai-proxy test-som-ai-player2
PYTHONPATH=tools python3 -m unittest tools.test_som_qwen_bridge -v
(cd android/tailnet && go test ./...)
(cd android && ./gradlew --no-daemon :app:testDebugUnitTest :app:lintDebug)
```

Real-device acceptance requires confirming that the core starts, the service
logs an Android snapshot, an action moves Randi, controller takeover stops the
AI, the Disciplines menu opens, a familiar remains friendly, saves persist, and
launching the normal SNES9X core still works.
