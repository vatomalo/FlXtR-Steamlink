# Android 0.3 — catalogue playback preview

This update replaces the main Android screen with a native touch browser.
The app package and launcher entry stay the same so the local APK can update
0.1/0.2 without uninstalling. Android 9+; both ARM ABIs are included. Primary test
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
- A movie/episode can remember a direct media URL through Use a direct link. Playback
  position is saved on leaving the player and restored on the next visit.

Movie and episode details now have **Play**. The app resolves available servers
with the existing native response decoder, probes the media, and opens it with
Media3 ExoPlayer. It carries the media request headers through HLS manifests,
segments and keys. Failed servers are skipped; Next server tries another source.
Retry starts a fresh lookup. Signed stream URLs are not saved to disk.

Use a direct link and Play saved direct link remain optional alternatives.

MTV, music helpers, games, downloads, external subtitle lookup and FlXtR-specific playback
menus still need Android implementations. The older SDL activity remains internal;
the Android launcher now opens the touch catalogue. Bluetooth scanning, microphone
and broad shared-storage permissions are not requested. Video playback uses
Media3 with HLS support, audio focus, resume and a 720p/H.264 preference for the
tablet. Actual codec support still depends on the device.

## Tablet test

1. Open `FlXtR-Android-preview-0.3.apk` on the tablet and update FlXtR Preview.
   Use the local APK from the same builder; CI debug signing keys may differ.
2. Tap Movies and Series. Check posters, search, next page and horizontal swipes.
3. Open a series, season and episode; use Android Back to return to the same page.
4. Hold a poster, save a favorite, then open Favorites. Test removing it there.
5. Rotate the tablet on a search result page and inside a season.
6. Open a movie → Play. Check that server lookup finishes and picture/audio start.
   Try Next server, then Back. Reopen the movie and check resume.
7. Open Series → a show → season → episode → Play. Also test the Big Buck Bunny
   demo under More and a direct media URL if available.
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
Gradle 8.9, Android Gradle Plugin 8.7.3, NDK r28 and Media3 1.5.1. Native libraries
support 16 KB pages. The resolver preparation uses a host C/C++ compiler, CMake,
Python and wget; WABT 1.0.36, its auxiliary header and the decoder module are hash
verified. A matching installed wasm2c can be reused. No Linux child executables
run on Android.
Third-party notices and demo attribution are packaged in `runtime/licenses`.
The sample is Big Buck Bunny / Blender Foundation / CC BY 3.0:
https://peach.blender.org/about/

## Validation

- Both ARM variants compile. Android build, lint and all 16 unit tests pass.
- Protocol tests cover request signing/fingerprint compatibility, movie/episode
  identities (including specials), server validation, HTTPS source selection,
  media-header isolation and cancellation. Catalogue tests remain in place.
- The same Java resolver and JNI library were exercised on the Linux host against
  live movie and episode lookups. Both resolved HLS playlists and fetched 64 KiB
  media fragments. Invalid decoder input was rejected and a later lookup worked.
- APK signing, ABI metadata and 16 KB alignment are checked before delivery.
- Actual MediaCodec rendering and audio on the SM-T510 still require the tablet
  tests above. A provider can be temporarily unavailable for a particular title.

To repeat the optional live integration test after building on Linux/WSL:

```sh
ANDROID_HOME=/path/to/android-sdk bash tests/test-android-resolver-live.sh
```

SDL's Java glue uses AndroidX receiver registration for Android compatibility.
Permission lint suppressions remain scoped to unused SDL capture and optional
Bluetooth helpers; application code is checked normally.
