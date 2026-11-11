# Frankenstein integration, 2026-11-11

Branch: `experiment/frankenstein-20261111`, based on `2bdffca` plus importer
commits and PSP experiment `d3bee6f`.

The Android experiment adds Sega and PC Engine formats, folders, artwork,
touch buttons and core pins. Reconciliation retains newer PSP, Dolphin,
ScummVM, TV handoff, artwork cache and Turkish caption handling. The desktop
subtitle fix waits for MPV file loading before attaching the selected track.

MTV/YouTube weighted genres, fusion selection, settings and MOV handling were
already present in the baseline. Its two untracked UI/test files match exactly.
The SDL queued audio adapter, diagnostics, scripts and tests were also already
present, including both untracked Python helpers. Reapplying those older patches
produces no changes after preserving newer README and shell functionality.

Build using `bash scripts/build-frankenstein.sh` inside the SkyLoft builder with
the writable Steam Link SDK mount. It produces `dist/flxtr-frankenstein.tgz`
with the separate app directory `flxtr-frankenstein` and display name
`FlXtR Frankenstein`. Automatic updates are disabled and its updater refuses
stable-channel updates. Install only under `/home/apps/flxtr-frankenstein`.
The stable `/home/apps/greenlink` must remain untouched.

Host MTV, YouTube recovery, settings, SDL queue, queued frontend isolation and
audio test-core checks passed. The ARM baseline-plus-integration build passed.
Physical device playback and TV audibility require on-device validation.
