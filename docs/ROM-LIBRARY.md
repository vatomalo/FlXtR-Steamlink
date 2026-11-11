# ROM library repairs

Games opens the console folders. Triangle/Y renames the selected ROM; Start
opens its existing core, cover, cheat, save-state and audio options. PC GAMEZ
retains its own Game options menu. Renames are display aliases in rom-names.json,
keyed by the ROM path identity. ROM files, extensions, saves and per-ROM core
preferences are preserved. Rescanning retains names.

The ROM Downloads folder opens downloaded ZIP archives, not movie playback.
Its item type now fits the catalog/UI buffer. Completed ROM downloads and
extractions return to the console folders.

The updated importer rescans the old index once, preserves explicitly imported
paths, detects installed fallback
cores (including catsfc for SNES and older FBA/MAME arcade cores), and retains
recognized ROMs whose engine is missing. Such entries explicitly say CORE
MISSING. Unknown/unsupported file extensions and BIOS ZIPs remain excluded. L2 returns
to folders and requests a rescan; device transfers invalidate cached ROM pages.
ROM pages expire from the RAM cache after a minute. Manual filesystem copies
are discovered with L2; downloads/transfers mark the index dirty automatically.
Unchanged large ROM collections are not repeatedly rescanned on a timer.

PPSSPP is a separate optional ARMv7/GLES2 core with ARM dynarec, built against
the Steam Link SDK rather than a modern generic Linux ABI. Its pinned upstream
source is libretro/ppsspp commit 7b4ddb426bbe9e287bb7f19b0cfaebb4ea0d41d8.
This is an older compatibility build, not the latest PPSSPP release. Build using
scripts/build-ppsspp-core.sh on SkyLoft. FFmpeg is rebuilt with the same SDK;
the C++ runtime is linked statically and GLES entrypoints are loaded dynamically.
The recipe applies the required headless EGL build fixes and deduplicates the
upstream object list. Patched corresponding sources accompany the binary release.

The optional core/assets archive has its own pinned SHA-256 manifest so it does
not exceed the legacy main updater's 32 MiB limit. First PSP launch checks the
installed optional-core receipt and downloads it only if absent or outdated.
Existing unrelated cores are untouched. Core assets use system/PPSSPP.

Older app updaters had a fixed tool allowlist that omitted newer ROM helpers.
The new launcher restores those packaged helpers from the verified Python
runtime, and the new updater carries them explicitly for future updates.

Validation includes catalog import/rename tests, UI routing, ZIP safety and
checksum tests, core ABI loading on a physical Steam Link, and separate PSP
homebrew rendering checks. Commercial PSP title compatibility and performance
must be checked per game; a successful core build is not a gameplay benchmark.

BIOS discovery uses the selected SCPH5500/5501/5502 model in Archive item
identifiers/titles rather than a generic PlayStation search. GBA searches target
firmware titles, excluding full-text matches from unrelated source-code archives.
Typing an exact .bin/.zip filename removes the extension for discovery.
Downloads retain their metadata SHA-1 check; PlayStation payloads must be 512 KiB,
GBA payloads 16 KiB. Existing installed BIOS files are not replaced.
