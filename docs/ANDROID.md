# Android 0.2 — touch catalogue preview

This update replaces the main Android screen with a native touch browser.
The app package and launcher entry stay the same so the local APK can update
0.1 without uninstalling. Android 9+; both ARM ABIs are included. Primary test
device: Samsung Galaxy Tab A SM-T510 on Android 11.

## What is available

- Movies and series: popular pages, search, posters, ratings and descriptions.
- Tap a series to open seasons; tap a season to open its episodes.
- Tap a movie/episode for details. Hold any poster for details and favorites.
- Favorites remain available offline. Successfully visited pages and posters
  are cached; an older page used after a connection failure is marked Saved page.
- Scroll vertically; swipe horizontally or tap Next/Previous to change pages.
- Android Back returns through seasons/pages. Page, scroll and search survive
  navigation and rotation. Portrait and landscape are supported.
- More offers direct HTTPS video, the Big Buck Bunny demo, refresh and poster size.
- A movie/episode can remember a direct media URL through Link stream. Playback
  position is saved on leaving the player and restored on the next visit.

**Catalogue metadata is not a playable stream. Automatic provider/stream
resolution is still unported.** Link stream accepts a direct HTTPS media URL
you provide. A saved link can be changed with Change saved stream in its
details dialog. The catalogue uses the same metadata endpoint as the desktop
application, with Android HTTPS and image decoding. No account key is needed.

MTV, music helpers, games, downloads, subtitle selection and embedded playback
menus still need Android implementations. The older SDL activity remains internal;
the Android launcher now opens the touch catalogue. Bluetooth scanning, microphone
and broad shared-storage permissions are not requested. Video playback uses
Android VideoView/MediaPlayer; supported formats depend on the device.

## Tablet test

1. Open `FlXtR-Android-preview-0.2.apk` on the tablet and update FlXtR Preview.
   Use the local APK from the same builder; CI debug signing keys may differ.
2. Tap Movies and Series. Check posters, search, next page and horizontal swipes.
3. Open a series, season and episode; use Android Back to return to the same page.
4. Hold a poster, save a favorite, then open Favorites. Test removing it there.
5. Rotate the tablet on a search result page and inside a season.
6. More → Play Big Buck Bunny demo. Check picture/audio and playback controls.
   Leave partway through, then reopen it to check resume.
7. Optionally Link stream on a movie/episode, then reopen its saved stream.
8. Disconnect Wi-Fi and revisit a cached page; check the Saved page message.

A failed request must leave navigation and Retry usable. Rapidly switch tabs
while loading to check that an old response does not replace the current screen.

## Reproduce

Linux/WSL, JDK 17+, Python 3, curl, tar, and an SDK with accepted licenses:

```sh
sdkmanager 'platforms;android-35' 'build-tools;34.0.0' 'ndk;28.0.13004108' 'cmake;3.22.1' 'platform-tools'
ANDROID_HOME=/path/to/android-sdk bash scripts/build-android.sh
```

The script downloads pinned SDL2, SDL2_image, mbed TLS and curl sources, then runs
`assembleDebug testDebugUnitTest lintDebug`. Output: `dist/FlXtR-Android-preview.apk`.
Gradle 8.9, Android Gradle Plugin 8.7.3, NDK r28; native libraries support 16 KB pages.
Third-party notices and demo attribution are packaged in `runtime/licenses`.
The sample is Big Buck Bunny / Blender Foundation / CC BY 3.0:
https://peach.blender.org/about/

## Validation

- Both ARM native variants compile; Android build and lint pass.
- Seven unit tests cover movie/series parsing, Unicode, parent show identity,
  season zero, episode identity, malformed data, paging limits, favorites and URLs.
- Live requests for movies, series, seasons and episodes returned valid data.
- APK signing, ABI metadata and 16 KB ZIP alignment are checked before delivery.
- The user reported that 0.1 works on the SM-T510. The new touch UI and 0.2's
  rotation/navigation/resume behavior still need the tablet test above.

SDL's Java glue uses AndroidX receiver registration for Android compatibility.
Permission lint suppressions remain scoped to unused SDL capture and optional
Bluetooth helpers; application code is checked normally.
