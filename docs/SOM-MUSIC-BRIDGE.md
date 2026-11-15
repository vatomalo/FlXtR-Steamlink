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
  Ambient and Original. The six existing AI priority weights are unchanged.
- src/som_ai_proxy.c: saves the genre number independently to
  /home/apps/retroarch/cores/experimental/som_music_genre.
- tools/som_music_bridge.py: reads existing [som-ai] SONG 0xNN log events,
  imports FlXtR tools/music.py to reuse its yt-dlp integration and its
  URL/header file format, ranks search results by composition and genre,
  caches YouTube video IDs per (song, genre), refreshes expiring stream URLs,
  and launches the existing greenlink-audio binary.
- Cache directory: som-music-cache, isolated from normal FlXtR music queues.
- Original mode stops external playback; it does NOT restore SPC music that
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

With the installed yt-dlp-enabled FlXtR music runtime and greenlink-audio
available, omit --dry-run to attempt playback. Stop with Ctrl+C.

Copy the catalog to the proxy path only after checking the running
binary's TSV input grammar:

    install -m 644 data/som_song_names.tsv \
      /home/apps/retroarch/cores/experimental/som_song_names.tsv

## Critical deployment warning

The GitHub branch's src/som_ai_proxy.c source, as retrieved for this
change, did not contain the song logging or retro_flxtr_skip_music_voices
extension observed in the actual installed 2026-11-15 proxy binary.
Do not replace that live proxy with a build of this branch until the
installed music-specific changes are merged back into the source.
The new in-game Gambits music row requires building this updated proxy.

Tests:

    PYTHONPATH=tools python3 -m unittest tools/test_som_music_bridge.py
    make test-som-ai-proxy test-som-ai-player2

The Python tests do not use the network. Full end-to-end live playback
is not yet verified on Steam Link.