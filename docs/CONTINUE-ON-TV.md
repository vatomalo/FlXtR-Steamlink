# Continue on TV

This implementation sends from Android FlXtR to the Steam Link FlXtR receiver.
It does not use Google Drive, screen mirroring or an external emulator app.

## Pair and send

1. Install the updated full Steam Link package. Open **Settings > Services >
   Continue on TV** and leave the QR screen open.
2. Scan its QR code with your phone's camera. Open the page's **Pair with FlXtR**
   link and confirm pairing. Alternatively, choose **Continue on TV** in Android's
   playback menu and paste the complete address displayed on the TV.
3. For movies, series or anime, open **Playback > Continue on TV**. For Game Boy
   or Game Boy Color, open the Libretro game menu and choose **Continue on TV**.
4. Wait for the TV to confirm a decoded video frame or a frame after successful
   save-state restoration. Then choose **Pause / exit here**. A queued upload is
   not treated as successful playback.

The pairing address contains a random bearer token. Share it only with your own
devices. Transfers require the receiver screen to be open, expire after ten
minutes, and are disabled when that screen closes or a session is accepted.
Restarting FlXtR rotates the token. Pairing uses HTTP on a trusted private LAN;
the Android client rejects public addresses, DNS names and redirected requests.
An already configured Tailscale private address can also be used; this feature
does not install or configure Tailscale itself.

## Videos

The package contains the title/episode identity, timestamp in milliseconds,
subtitle preference and current HTTPS source. Movies and series attempt fresh
source resolution on the TV before falling back to the supplied link. Anime
currently uses the resolved link; if it expires, resolve it again on the phone
and retry. Available external subtitle URLs are included for the chosen language.

Local-only files, offline downloads, separate audio/video sources, sources tied
to the sender's IP, DRM and arbitrary authentication headers are not supported.
The TV's existing player codec limits still apply. No provider API keys or
account cookies are transferred. The sender remains available on errors.

## Exact game continuation

Only **save states** are used. There is no normal-save fallback. The source
freezes at capture, sends the raw core state and waits for actual restoration.
The same ROM must already be present and scanned on the Steam Link. ROM files
are not uploaded automatically.

The initial supported portable core is **Gambatte, Game Boy / Game Boy Color**,
source revision `77f2567d718b8973d02caa2daa8dce4953faf811` on both devices.
The receiver checks ROM SHA-256, state SHA-256, installed core provenance,
reported core version, serialization restrictions and state size. It restores
the sender's core options and checks the core's unserialize result. The bundled
isolated frontend acknowledges only a frame produced after successful loading.
Other cores are rejected before capturing a state until a matching portable
receiver build has been verified. ScummVM has no frontend save-state fallback.

This is Android-to-Steam-Link handoff. Reverse transfer and Android TV receivers
are not implemented in this change. Existing user save files are never replaced.
On normal TV exit, progress is saved to a separate state in the received session.
Open the receiver screen and press **X** to resume the last received game on TV.

## Installation and verification

Older Steam Link updater scripts do not copy the new receiver files. Install the
complete package once, preserving ROMs and settings; the updated updater includes
the new helper and core on subsequent updates. The device needs free storage for
the state and runtime. Save states are capped at 128 MiB and transfers are bounded.

`tests/handoff_test.py` checks real HTTP pairing, expiry, origin rejection,
acknowledgement timing, ROM/core compatibility, checksum validation, unsafe ZIP
entries and preservation of existing saves. `tests/handoff_cross_arch.py` creates
an original Game Boy fixture and a 64-bit reference state for comparison against
the 32-bit runner's `--verify` mode. Physical TV playback is a separate check.
