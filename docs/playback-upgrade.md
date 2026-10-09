# Playback update (October 2026)

Based on `fed4a10`, including the recent Archive, BIOS, library cache and full-package updater changes.

- Steam Link D-pad left/right seeks by 10 seconds without exiting the player or resolving another server. Repeated presses within 250 ms combine into one target. Seeking resumes at a server-provided keyframe, with at most five seconds of initial refill; normal read-ahead continues in the bounded disk ring.
- Seek flushes the audio queue, resampler, H.264 filter, hardware stream and embedded subtitle cues. File start timestamps are removed from the displayed playback position and included in seek requests.
- Audio pauses during buffer refill to keep it aligned with the stalled video. PCM storage is reused between decoded audio frames.
- Linux uses mpv native seeks and retains one quarter of the configured cache budget for backward seeks.
- Embedded text subtitles and provider-supplied external SRT, WebVTT and ASS tracks are supported. Resolver subtitle manifests are bound to the exact media URL, and language selection supports English and Norwegian labels. New settings default to Automatic; existing Off settings are preserved.
- The playback menu retains subtitle language, size and delay controls and reports NONE FOUND when no supported track was loaded. A direct file can also be supplied with `--subtitle-file PATH_OR_URL --subtitles auto`.
- External files are limited to 2 MiB, ten seconds of loading and 8,192 cues. Failed subtitle loading does not prevent video playback. HTTP(S) subtitles cannot open nested local files; TLS verification remains enabled.

Subtitles require an actual track from the source. Burned-in captions cannot be turned off. Bitmap subtitles and full ASS styling are unsupported; the compact Steam Link renderer uses its existing pixel font. Sources must support seeking (HTTP byte ranges for MP4, or seekable HLS); seeks cannot repair a host that advertises ranges but ignores them.

Validation: host suite (menu, buffer, resolver, catalog and updater), Linux backend compilation, Steam Link cross-compilation, and a controlled H.264/AAC hardware playback test with HTTP subtitles plus forward/backward control input. Emulator-core compilation is intentionally deferred to the next task.
