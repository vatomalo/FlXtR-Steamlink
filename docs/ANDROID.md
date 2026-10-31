# FlXtR Android preview 0.6

Minimum supported system: Android 8.0 (API 26). Phone/tablet layout is automatic.
Hardware tested: Samsung SM-T510 running Android 11 (32-bit armeabi-v7a).
The APK also includes arm64-v8a for other devices. Update over the earlier APK;
keep the same signing key to retain favorites and progress.

## Library and controls

Movie and episode details use **Play** to resolve and start playback. Manual links
are available only through More → Open a direct video URL.

Library offers Movies, Series, KissAnime, Music, MTV, TV channels, Internet Archive,
Continue, History, Favorites, Downloads and Local media. Movies/series support
search, seasons, episodes, genres and ordering. Anime supports search, genres,
ordering and SUB/DUB selection under More. Archive folders contain individually
playable files. Music accepts searches, YouTube links and public Spotify playlist
links; Spotify entries become music searches and do not play Spotify audio.

More → Choose theme selects Original White Metal, Original Green/Black, or the
previous Touch theme. The original themes use FlXtR's texture and pixel font, with a sidebar and cover
carousel in tablet layout; Touch retains the native poster grid. The theme persists. More → Screen layout offers Automatic, Phone/compact, or
Tablet/carousel. Narrow and short windows use the compact poster grid without the
wide sidebar. The keyboard stays hidden until search is requested.
Touch, Android Back and controllers can be used together.

| Controller | Library | Player |
| --- | --- | --- |
| D-pad / left stick | Navigate | Show/navigate playback controls |
| A | Select | Play/pause |
| B | Back | Return to library |
| X | Search | Show playback controls |
| Y | Details | Playback options |
| LB / RB | Previous/next page | Seek backward/forward |
| Start | More | Play/pause |

Controller keys have been exercised through ADB; physical controller mappings and
stick behavior still need a real controller test.

## Playback and offline media

The player resolves movies, episodes and anime, retains source headers through
HLS requests, tries alternative servers, and saves progress/history. Available
embedded/provider text captions can be selected in the player. More → Playback
settings controls preferred caption language/size and episode autoplay. Player
Options includes speed, picture fit, subtitles, next episode/video and downloading.
Episode autoplay can cross seasons; music/local queues follow the visible list.
MTV plays a continuous video mix. TV channels support a ten-block Oslo schedule,
genres, selection weights and marathon mode. MTV weights, custom search and fusion
style are configurable under More.

Downloads are app-private and survive process restarts. Choose Download for offline
playback on a playable title; visit Library → Downloads to refresh status, retry,
remove, or play completed media. The default is unmetered/Wi-Fi downloads. HLS
selection is capped at 720p with H.264 preferred; separate music audio is also
queued. Signed stream addresses are retained only inside private download metadata
while required for the transfer. Failed/expired transfers may require Retry.

More → Add local media opens Android's document picker for videos/audio; the app
retains access to selected documents without broad storage permission. Moving or
deleting a document invalidates that entry. Music audio can continue in a media
service while browsing, with system playback controls.

This remains a preview, not complete desktop feature parity. Desktop phone-upload/remote/quiz tools, API-key
setup, external subtitle search/delay and advanced TV airing/history editors have
not been ported. Download recovery and background music need longer device tests.

## Build and checks

Linux/WSL, JDK 17+, Python 3, curl, tar, C/C++ build tools and an Android SDK:

```sh
sdkmanager 'platforms;android-35' 'build-tools;34.0.0' 'ndk;28.0.13004108' 'cmake;3.22.1' 'platform-tools'
ANDROID_HOME=/path/to/sdk bash scripts/build-android.sh
```

The script prepares pinned Moonlight/Gambatte sources, builds native media, streaming and emulation libraries for both ARM variants, and
runs assembleDebug, testDebugUnitTest and
lintDebug. Gradle 8.9, AGP 8.7.3, NDK r28, Media3 1.5.1, jsoup 1.18.3 and
Android yt-dlp runtime 0.18.1 are pinned. Music uses packaged Android Python/QuickJS;
no Linux worker is needed. More → Update music engine updates yt-dlp if a provider
change requires it. The optional pixel font regeneration script uses fontTools;
the generated font is already checked in.

The native movie resolver/WASM preparation is hash-verified as before. FlXtR's own
native libraries are built for 16 KB pages; this preview's primary device target is
Android 11. No blanket 16 KB compatibility claim is made for third-party runtimes.
See android-third-party.md and packaged assets/licenses for distribution notices.

Validation: 35 unit tests cover catalogue parsing, request signing, source/header
handling, anime parsing/AES/token format, captions, archive filtering, controller
repeat policy, responsive layout selection, finite media ranges, media persistence, TV scheduling and MTV/Spotify mapping. Live host
tests fetch movie/episode/anime manifests and media fragments, and archive listings.
Debug-only MediaSmokeInstrumentation can probe and render known sample media on a
connected tablet without printing signed stream URLs:

```sh
adb shell am instrument -w -e mode anime org.flxtr.android/.MediaSmokeInstrumentation
adb shell am instrument -w -e mode play-anime org.flxtr.android/.MediaSmokeInstrumentation
# Also: music, movie, play-music, play-movie, play-seek, play-offline, controls
```

Provider availability varies by title and region. A successful sample verifies
that path, not every catalogue item or every subtitle format.

## Device results (2026-10-29)

The final 0.4 APK was installed over 0.3 through Windows ADB on the user's
Android 11 / armeabi-v7a tablet. All four device playback checks returned success:

| Sample path | Video | Audio decoded | Playback | Additional check |
| --- | --- | --- | --- | --- |
| Movie 550 | Rendered | 64 buffers | Advanced past 1 s | Automatic stream resolution |
| Naruto episode 7417 | Rendered | 64 buffers | Advanced past 1 s | Two provider caption tracks resolved |
| YouTube / Blender | Rendered | 81 buffers | Advanced past 61 s | Seek to 60 s rendered successfully |
| Downloaded Blender demo | Rendered | 70 buffers | Advanced past 1 s | Download completed; cache-only player used |

Controller A opened Play; B closed the dialog; Start opened the library menu.
Original White Metal, Green/Black and Touch themes were visually checked on the
tablet, including switching between the carousel and grid. No app crash was
reported during these checks. Physical gamepads, extended playback, long download
recovery, TV scheduling over clock boundaries, and background music still need
broader testing. These are sample checks, not a guarantee for every provider item.

## Version 0.5: phones, Android 8 and fullscreen playback

The same ARM APK adapts to phones and tablets; a separate app installation is not
required. The old internal SDL preview launcher is no longer bundled. Android
media features use their native Android implementations, and the shared response
decoder remains in place. Removing the obsolete launcher also removes its Android
9-only process dependency and reduces the APK from about 54 MiB to 39 MiB.

Video starts edge to edge, with Android's system bars and the playback header
hidden. Tap the video to reveal controls; they disappear after 2.5 seconds. The
Back and Options buttons are transparent overlays, and Next server is under
Options. Fill screen crops edges by default; Options → Picture size → Fit whole
picture preserves the complete image. Swipe from an edge for Android system
navigation. The old permanent bar/menu no longer covers the picture.

Android 8.0/API 26 was exercised on a 720×1280 x86_64 AOSP phone emulator at
360 dp width. The compact library loaded 20 titles in two columns. Movie, anime,
music with a seek to one minute, and cache-only offline playback all rendered
video, decoded audio, and advanced. The player header and system bars were hidden
in each playback check. Phone layout and borderless playback were visually
inspected. The same source builds the shipped armeabi-v7a/arm64-v8a APK; the x86_64
build is a test artifact. On the physical Galaxy S7 edge / Android 8, version 0.5 movie, anime and offline video passed. Music extraction was blocked by a Samsung system utility/library conflict; version 0.6 includes a Python platform-probe fix. That fix still needs a hardware retest.


## Version 0.6: app updates and embedded games

The same universal ARM APK updates the Android 8 phone and Android 11 SM-T510
without removing favorites or settings. Phone/tablet layout is automatic, with a
manual override under More → Screen layout. The tablet download is the same
signed universal package, named for convenience; it is not a different app.

More → App updates provides a manual check and an automatic-check toggle (on by
default). Automatic checks run at most once a day when opening the library.
An available update offers a download, verifies exact size, SHA-256, package,
newer version, Android compatibility and matching installed signing certificate,
then opens Android's installer. Downloads can be cancelled; a failed download
keeps the installed app. Android 8+ may first require “Allow from this source”.
Android always asks before installation. Downloads only begin after tapping
Download; there is no periodic background service or silent installation.

The channel is independent of desktop releases:
`https://github.com/vatomalo/FlXtR-Steamlink/releases/download/android-latest/android.json`.
After committing and building, `bash scripts/publish-android.sh` publishes an
immutable Android prerelease and then advances this channel. Keep the existing
signing key and increment versionCode for every published build. Older previews
need one manual installation of 0.6 before they can use app updates.

Library → Moonlight opens the embedded Moonlight PC browser. Add a PC running
Sunshine or NVIDIA GameStream, pair using the PIN shown by Moonlight, then select
a PC app. Streaming, touch input, controller support and stream settings run in
FlXtR's process/package; a separate Moonlight installation is unnecessary. The
upstream 12.1 UI/JNI interface is used for Android 8 compatibility, with native
OpenSSL/Opus payloads from upstream 12.2 and refreshed Java dependencies. Refs and
integration changes are explicit in scripts/prepare-android-games.py. Moonlight's
own native screens retain their upstream UI.

Library → Libretro games opens the built-in Gambatte player for Game Boy/Game Boy
Color. Add uncompressed .gb/.gbc ROMs through Android's file picker. Games are
copied into private storage; no games are bundled or downloaded. Touch controls,
D-pad/stick, A/B, Start/Select are supported. Back or R3 opens Resume, Save state, Load
state, Reset and Exit. Battery RAM and an automatic resume state are saved locally
when leaving; manual state slots are separate. This initial core does not run NES,
SNES, arcade or other systems, and is not an embedded copy of RetroArch.

On an Android 8 x86_64 emulator, the original synthetic ROM rendered patterned
video, generated stereo audio, advanced past 120 frames, serialized/restored state,
and accepted controller A. Embedded Moonlight's activity and native library loaded.
Update menu and library A/Start controls passed. Music playback and a seek to one
minute passed with hidden video chrome/system bars. These are sample checks;
Moonlight pairing/live streaming, physical controller mappings, long game sessions,
and final ARM hardware checks still require devices/host access.

Debug-only game checks:
```sh
adb shell am instrument -w -e mode retro org.flxtr.android/.GamesSmokeInstrumentation
adb shell am instrument -w -e mode moonlight org.flxtr.android/.GamesSmokeInstrumentation
adb shell am instrument -w -e mode updates org.flxtr.android/.GamesSmokeInstrumentation
```
