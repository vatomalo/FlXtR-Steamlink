# PSP experimental device-test beta

This is a separate native PSP application, not the Linux executable. It now
cross-compiles to `EBOOT.PBP` using `garden-gaiden-psp-sdk`. It has **not yet been
booted or played on a physical PSP or emulator**. Do not present it as a verified
classroom demo. Real-device firmware, Wi-Fi, audio/video and updater tests remain.

## Included

- Black background, green text and D-pad menu, up to 64 playlist entries.
- Direct online MP4 streaming using bounded 256 KiB HTTP byte-range requests;
  no PC helper, offline conversion or complete-file download is required.
- Tilefinch firmware AVC/AAC backend, bounded demuxer and 12 MiB media budget.
- Cross pauses/resumes; Circle cancels network activity or stops playback.
- PSP-only HTTPS update check on first playback connection, plus Square to check
  manually. A newer build is SHA-256 checked and its PBP/MIPS header validated
  before replacing the EBOOT. Relaunch after updating; it never installs a Linux
  or Steam Link executable. PSP installation itself remains untested on hardware.

The included Internet Archive *Popeye for President* MP4 was probed as 320x240
Constrained Baseline H.264, AAC-LC stereo at 48 kHz. This verifies the source's
properties, not successful PSP playback. Unsupported codec profiles and geometry
are rejected by the backend before firmware decoding.

## Install and controls

Use a PSP with homebrew/custom firmware and the KUBridge/SystemCtrl firmware
interfaces used by the decoder. PSP Go is the initial intended test device.
Copy the whole `FlXtR` folder into `ef0:/PSP/GAME/` on Go internal storage, or
`ms0:/PSP/GAME/` on Memory Stick. Keep `cacert.pem` beside `EBOOT.PBP` and set the
PSP clock correctly for HTTPS. Configure a working Wi-Fi connection in the PSP
system settings first. L/R selects the saved connection profile (default 1).
Launch FlXtR under Game; select a title and press Cross. HOME exits.

Edit `streams.tsv` using one `Title<TAB>https://...mp4` line per entry. Triangle
reloads it. Each URL must be a direct MP4 with HTTP byte-range support, not a
website/embed page. This beta does not yet provide KissAnime/HLS extraction,
movie/series search, subtitle selection, seeking, coverflow, or TV scheduling.
The Steam Link's 720p default is not appropriate for PSP; start with 240p
Baseline/Main H.264 and AAC-LC. No all-model compatibility claim is made.
PSP-1000 memory and firmware behavior are untested; PSP Street cannot use Wi-Fi.

A successful update keeps `EBOOT.OLD` beside `EBOOT.PBP` and preserves the
playlist. FAT storage cannot atomically exchange two files: if power is lost
between the renames, use USB to rename `EBOOT.OLD` back to `EBOOT.PBP`.
Keep sufficient free storage for both binaries. The beta channel must exist for
updates to be available; a failed check does not prevent playback.

## Build and validation

```sh
bash scripts/build-psp.sh
bash scripts/test-psp-host.sh
```

The build uses the installed PSP compiler or launches the existing Docker image
`garden-gaiden-psp-sdk` automatically. Override it with `FLXTR_PSP_IMAGE`.
Output: `dist/psp/FlXtR/EBOOT.PBP`, supporting files and `dist/psp/psp.json`.
The build refuses firmware-import fixup warnings. The SDK's user import archive
is linked whole to keep each firmware library's stubs contiguous.

Host tests exercise the actual MP4 demuxer, allocation limits, PBP validation,
and HTTP range cache including rejected incorrect range responses, using ASan
and UBSan. They cannot validate PSP firmware calls, Wi-Fi or display output.

## Attribution and integration

Media-only MIT-licensed Tilefinch sources are pinned at
`32bb34f05d637088bc230db9226a307f976565db` under `third_party/tilefinch-media`.
Its browser, QuickJS and Lexbor are not linked. FlXtR supplies a bounded allocator,
HTTP range transport, menu, scanout copy and updater. See the upstream license
and source inventory there. Runtime license notices are bundled with the app.

The earlier full-browser build failures are bypassed by extracting only the
media stack, not by disabling its integrity checks or changing its decoder.
The lowest observed KissAnime sample was actually 854x480 despite a 640x360
manifest label; it is not an established PSP-compatible source.
