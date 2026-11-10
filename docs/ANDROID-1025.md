# Android 0.10.25: Continue on TV

Adds Continue on TV to the video playback and Libretro menus. Pair by scanning
the QR under Steam Link Settings > Services > Continue on TV, then confirm the
pairing link on your phone. Pasting the full TV address also works.

Movies, series and anime hand off their current video timestamp and subtitle
preference. The TV confirms a video frame before the phone offers to pause.
Local-only files and separate audio/video sources are not supported.

Exact game continuation uses save states, never normal save files. Initial
support is Game Boy and Game Boy Color with the matching bundled Gambatte core.
The identical ROM must already be scanned on the TV. Other cores are rejected
until a matching portable receiver build is verified. The source is retained
on errors; successful restoration is confirmed before offering to exit here.

Includes Turkish subtitles and all engines from 0.10.24. Older Android previews
with the 128 MB updater limit need one direct APK installation. Existing libraries
and settings are preserved when installing over the existing app.

Verification includes paired HTTP transfer tests and 64-bit-to-ARMv7 restored
Game Boy and Game Boy Color fixtures: matching frame output and all active
CPU/RAM/graphics state fields after continuation. Physical TV/controller checks
are not represented by the headless tests.

Full instructions and limitations:
https://github.com/vatomalo/FlXtR-Steamlink/blob/main/docs/CONTINUE-ON-TV.md
