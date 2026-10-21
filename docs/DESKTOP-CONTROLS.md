# Themes, keyboard controls and music-video TV

## Themes

Open Settings with F10, select Menu theme, and use Left/Right or Enter. F4 cycles
themes outside text entry. Palettes: White Metal, Classic Monochrome, Crimson
Skulls, Midnight Blue, Amber CRT and Custom. The selection is saved.

Selecting Custom creates themes/custom/theme.ini in the writable data directory.
Edit accent, text, muted, canvas and panel using #RRGGBB values. Optional artwork
is themes/custom/background.png (at most 8 MB and 4096×4096). Re-select Custom or
restart to reload. Malformed colors retain the palette defaults. Themes contain
data and images; no theme scripts run.

Windows defaults to %LOCALAPPDATA%/FlXtR. Linux/Deck use the package's data folder.
The original three themes and all controller mappings remain available.

## Keyboard

| Key | Action |
| --- | --- |
| Arrow keys, Enter | Navigate, select |
| Escape | Back; stop video or cancel a video lookup |
| Space or K | Pause/resume during playback |
| J / L; Left / Right | Seek back/forward ten seconds during video |
| F / M during video | Toggle fullscreen / mute |
| Tab during video | Open/close playback menu |
| F1 | Keyboard help |
| F2 | Next library |
| F3 or Ctrl+F | Search in a searchable library or music browser |
| Tab while typing | Complete a suggested title |
| F10 | Settings |
| F4 | Next theme |
| M, T, G outside typing | Music, TV, Games |
| Ctrl+P | Phone setup |
| Ctrl+Q | Quit |

Letters remain text during search. Controller Y still accepts a suggested title.
In the music browser, B/Escape returns to the library while music continues.

## MTV / music videos

In TV, press F6 (controller Y) to select MTV immediately, then Enter/A to start. F6 again restores
the normal schedule. Or edit a TV schedule block and select the appended
MTV / MUSIC VIDEOS genre. Scheduled indices for the original genres are unchanged.

The channel resolves public music videos using the pinned yt-dlp/QuickJS runtime,
then streams through the native player. The next clip starts after natural EOF;
Stop ends the channel. Music videos are limited to 20 minutes, live streams are
excluded from the TV channel, and recent IDs are skipped. Lookup failures stop
with a diagnostic rather than retrying forever. There are no ad breaks between
MTV clips.

Create mtv-query.txt in the writable data directory to choose a YouTube playlist
URL or a search, such as 90s alternative rock official music videos.
The default is official music videos 80s 90s pop rock. Quality follows Settings.

Launch directly with FlXtR.exe --mtv or ./FlXtR.sh --mtv. Steam Deck uses
./FlXtR-SteamDeck.sh --mtv. --theme 0 through 5 and --keyboard-help are also accepted.

## Deliberate phone playback and Cast

The phone remote's video form accepts public YouTube links as well as direct
media URLs. Resolution runs on FlXtR and controls stay with its native player.
Keep phone setup open. This is link playback; FlXtR does not advertise itself as
a Google Cast/Chromecast receiver or intercept casts sent to other devices.
Google's supported receiver SDK paths require Cast hardware or Android TV:
https://developers.google.com/cast/docs/overview

## Build Steam Link from regular WSL/Linux

Use a separate source folder to preserve changes in your existing Steam Link
checkout or container. Docker must already have the Steam Link SDK image.

    git clone --branch port/windows-platform --single-branch https://github.com/vatomalo/FlXtR-Steamlink.git ~/src/FlXtR-current
    cd ~/src/FlXtR-current
    bash scripts/build-steamlink-package.sh

For subsequent builds in that new clean checkout:

    git pull --ff-only
    bash scripts/build-steamlink-package.sh

The host helper prepares the pinned ARM music runtime, then starts a disposable
steamlink-sdk:latest container to compile the player, resolver, UI and catalog.
Set FLXTR_STEAMLINK_IMAGE=steamlink-sdk-wsl:latest if that is your SDK image.
The experimental manual package has auto updates disabled to retain this build.\nYou can enable them later in Settings. It uses a named build cache volume; it does not enter or modify a running
container. Output: dist/greenlink.tgz. This does not deploy to the device.

For an existing SDK shell with this source already checked out, use:

    bash scripts/build-player.sh
    bash scripts/build-resolver.sh
    bash scripts/build-steamlink.sh

Prepare build/music-runtime.tgz with the host helper first to include music.
Windows, Linux and Deck packages are separate builds and must not be installed
on the ARM Steam Link. Physical Steam Link/Deck playback still needs testing.
