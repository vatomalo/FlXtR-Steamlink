# Android preview

The Android branch builds the existing `src/shell.c` SDL2 interface as an APK.
The first preview targets Android 9+ and includes 32-bit and 64-bit ARM, covering
the Samsung Galaxy Tab A SM-T510 on Android 11.

## Test on the tablet

1. Copy `FlXtR-Android-preview.apk` to the tablet and open it in My Files.
2. Allow My Files to install this app when Android prompts, then install.
3. Open **FlXtR Preview**. Use the bottom touch buttons or a controller.
4. Select **Big Buck Bunny** and press **OK** to exercise network video and audio.
5. Tap the video for playback controls. Android Back returns to FlXtR.
6. Try **Settings**, navigate with the arrows, then Back. Relaunch to check persistence.
7. The **URL** button accepts a direct HTTPS media URL, including compatible HLS.

This is a working interface/playback foundation, not feature parity. Online
catalogue workers, YouTube/MTV, music helpers, game launchers, downloads, Samba,
USB setup and preview decoding still need Android implementations. Unsupported
Linux worker launches are disabled. APK updates replace the desktop updater.
No permissions for shared storage, microphone or contacts are requested.
Settings/favorites stay in Android app-private storage between APK updates.
Uninstalling clears that data. Debug APKs built on different machines may use
different signing keys and require an uninstall before switching builders.

The preview uses Android's platform VideoView/MediaPlayer backend; support varies
with device codecs. Media3, separated audio/video streams, caption selection and
FlXtR playback menu integration are later work. HTTPS certificate validation stays
enabled; cleartext streams are not enabled in this preview.

## Reproduce the build

Use Linux/WSL, JDK 17+, Python 3, curl, tar, and an Android SDK with accepted licenses:

```sh
sdkmanager 'platforms;android-35' 'build-tools;34.0.0' 'ndk;28.0.13004108' 'cmake;3.22.1' 'platform-tools'
ANDROID_HOME=/path/to/android-sdk bash scripts/build-android.sh
```

The script downloads pinned SDL2, SDL2_image, mbed TLS and curl releases into the
ignored `android/build/deps` directory. Gradle 8.9 and Android Gradle Plugin 8.7.3
build and lint the app. Output: `dist/FlXtR-Android-preview.apk`.
SDL_image uses built-in BMP support in this first preview; optional image codecs
are disabled. The native libraries use NDK r28's 16 KB page alignment.

To compile for an emulator after preparing dependencies/assets:

```sh
cd android
./gradlew assembleDebug -PflxtrAbis=x86_64
```

ADB install (USB debugging must be enabled and the computer authorized):

```sh
adb install -r dist/FlXtR-Android-preview.apk
adb shell am start -n org.flxtr.android/.MainActivity
adb logcat -s SDL SDL/APP AndroidRuntime MediaPlayer
```

Third-party notices are included in the APK's `runtime/licenses` assets.
The sample movie is Big Buck Bunny, Blender Foundation, CC BY 3.0:
https://peach.blender.org/about/

## First-preview validation

- Both `armeabi-v7a` and `arm64-v8a` native libraries compile and link.
- Gradle `assembleDebug` and `lintDebug` pass. Lint retains non-blocking warnings.
- APK signing and 16 KB ZIP alignment are verified.
- The shared desktop shell still compiles with warnings treated as errors and
  completes a one-frame SDL dummy-driver smoke test.
- No tablet is attached to the build host. Android startup, touch, video/audio,
  Back navigation and settings persistence still require the SM-T510 test above.

SDL's pinned Java glue receives a small receiver-registration compatibility patch
using AndroidX Core. Permission lint suppressions apply only to SDL's optional
Bluetooth backend (which checks permissions dynamically) and unused microphone
capture helper. No microphone or Bluetooth scanning permissions are requested.
