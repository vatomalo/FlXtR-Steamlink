# Secret of Mana Turbo genre soundtrack bridge

This adds song ID -> composition title -> genre -> yt-dlp -> FlXtR audio playback.
It DOES NOT change SNES audio voices or the existing game-effects audio.

## Sources

Original USA song table: 64 IDs, 59 unique sequences. The supplied title
mapping is based on:
https://cosomenterprise.com/snes/secret-of-mana/music/manifest.json
https://github.com/DoctorDan1986/SecretOfMana-MSU1/blob/master/README.txt
https://wiki.superfamicom.org/seiken-densetsu-2

Original song IDs may differ if Secret of Mana Turbo changed its song table.
The live reported ID 0x2D maps in the original to Squaresoft Logo /
Mysterious Moaning, an 8-second one-shot cue. The bridge ignores one-shot
music cues so the background track continues uninterrupted.

## Changes

- data/som_song_names.tsv: 64-entry hex song-index -> English title catalog.
- src/som_gambit.h: Music genre row before Exit, using Left/Right/B.
  Options: Lo-Fi, Jazz, City Pop, Orchestral, Piano, Synthwave, Rock,
  Ambient and Off. The six existing AI priority weights are unchanged.
- src/som_ai_proxy.c: saves the genre number independently to
  /home/apps/retroarch/cores/experimental/som_music_genre.
- tools/som_music_bridge.py: reads existing [som-ai] SONG 0xNN log events,
  imports FlXtR tools/music.py to reuse its yt-dlp integration and its
  URL/header file format, ranks search results by composition and genre,
  caches YouTube video IDs per (song, genre), refreshes expiring stream URLs,
  and launches the existing greenlink-audio binary.
- Cache directory: som-music-cache, isolated from normal FlXtR music queues.
- Off mode stops external playback; it does NOT restore SPC music that
  your custom underlying core already suppresses.

## Safely testing without a Steam Link rebuild

From a FlXtR checkout containing this branch:

    printf '[som-ai] SONG 0x0E Into the Thick of It (map 123)\n' |
      python3 tools/som_music_bridge.py --dry-run

To follow an actively written live RetroArch stderr log:

    python3 tools/som_music_bridge.py --log /path/to/live-retroarch.log

Or:

    tail -F /path/to/live-retroarch.log |
      python3 tools/som_music_bridge.py

Provide the actual current RetroArch log, not an old /tmp/pt3.log from a
one-shot test. The watcher uses the existing song logger and does not
guess a Turbo RAM music address.

For live playback on Steam Link, use FlXtR's bundled music runtime so yt-dlp
and its dependencies are available, and run from the FlXtR directory:

    sh scripts/music-runtime.sh tools/som_music_bridge.py --log /path/to/live-retroarch.log

The log must receive live song events while RetroArch runs. Stop with Ctrl+C.
The original SPC suppression is intentionally untouched.

Copy the catalog to the proxy path only after checking the running
binary's TSV input grammar:

    install -m 644 data/som_song_names.tsv \
      /home/apps/retroarch/cores/experimental/som_song_names.tsv

## Status on hardware (2026-11-15)

Source parity: the song logging (`[som-ai] SONG`), `retro_flxtr_skip_music_voices` and the
genre row are in this branch; the installed proxy is a build of it (see
`docs/SOM-DEVICE-INVENTORY.md`). The old warning about a source tree without them is
obsolete.

`packaging/greenlink.sh` starts the bridge with every game (it exits for other games and
with the game). On the Steam Link the chain works up to the player: song event -> title ->
genre -> yt-dlp search -> stream URL -> `greenlink-audio` started (verified twice, once with
a stand-in player). **The real player then fails:** `MV_PE_StreamCreate() failed:
0x80007065` (`som-music.log`): RetroArch already holds the Steam Link's audio stream and the
Marvell pipeline refuses a second one. FlXtR's per-ROM music avoids this only because its
player is started before RetroArch. So "soundtrack + native effects together" is
**not achieved on HDMI**; native effects are unaffected (RetroArch keeps its stream).
The bridge now notices a player that dies with an error within 10 s; after two in a row it
stops trying for that session (log: "audio output unavailable") instead of searching and
relaunching on every song. Offline / yt-dlp errors are logged and the bridge keeps running.

**Mixing into the game's audio (2026-11-15, built and tested, not yet installed).** The
bridge now runs `greenlink-audio --pcm <rate> /tmp/flxtr-som-music.pcm <stream>`: the
same player decodes to raw stereo s16 at the game's sample rate (published by the proxy in
`/tmp/flxtr-som-audio-rate`, 32040 Hz) into a named pipe. The proxy reads the pipe without
blocking before each frame into a ~1 s ring (`src/som_music_mix.h`) and adds it to every
audio batch of the core at 176/256 under the effects, saturating. One audio stream, so the
Steam Link has nothing to refuse; the pipe paces the decoder to the emulator (it stops
when RetroArch pauses). No pipe, no writer or an empty ring: game audio passes through
untouched. Launcher flags: `--mix-fifo /tmp/flxtr-som-music.pcm --rate-file
/tmp/flxtr-som-audio-rate` (`packaging/greenlink.sh`). With the old installed
`greenlink-audio` (no `--pcm`) the player exits at once and the bridge gives up for the
session after two tries: game audio unaffected.
Verified headless on the Steam Link with the real ROM: game-only output rms 0 on the title;
with the pipe fed, rms ~2980 throughout, no clipping; the decoder produced only ~417k frames
for ~321k consumed (paced) and exited when the game closed the pipe.
Hardware listening test: pending (needs the new `greenlink-audio`, proxy and launcher).

Tests:

    PYTHONPATH=tools python3 -m unittest tools/test_som_music_bridge.py
    make test-som-ai-proxy test-som-ai-player2

The Python tests do not use the network. Full end-to-end live playback
is not yet verified on Steam Link.