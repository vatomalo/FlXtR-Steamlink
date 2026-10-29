# Android preview third-party notices

FlXtR source retains its existing MIT notice. The Android distribution containing
youtubedl-android is distributed under GPL-3.0 terms; its license is included in
assets/licenses/youtubedl-android-GPL-3.0.txt. The original MIT grant remains intact.

Corresponding Android application source and build scripts:
https://github.com/vatomalo/FlXtR-Steamlink/tree/feature/android
Build: scripts/build-android.sh (both ARM ABIs, Android SDK/NDK versions pinned).

Music runtime: youtubedl-android 0.18.1 (GPL-3.0), unmodified Maven artifacts
io.github.junkfood02.youtubedl-android:library/common:0.18.1.
Source: https://github.com/yausername/youtubedl-android/tree/0.18.1
Runtime/source build inputs: https://github.com/yausername/youtubedl-android/releases/tag/0.18.1
Bundled Python runtime, QuickJS and yt-dlp retain their respective upstream notices.
Music engine updates replace yt-dlp using its official stable release.

AndroidX Media3 1.5.1 and AndroidX Core 1.15.0: Apache-2.0.
https://github.com/androidx/media/tree/1.5.1
https://android.googlesource.com/platform/frameworks/support/
jsoup 1.18.3: MIT. https://github.com/jhy/jsoup/tree/jsoup-1.18.3
Kotlin standard library: Apache-2.0. https://github.com/JetBrains/kotlin
Jackson 2.11.1: Apache-2.0. https://github.com/FasterXML/jackson
Apache Commons IO 2.5: Apache-2.0. https://commons.apache.org/proper/commons-io/

WABT and FlXtR notices are packaged in assets/runtime/licenses. The former SDL,
curl and mbedTLS preview launcher is no longer included in the Android media APK. The pixel font is generated
from FlXtR src/font.h; the White Metal texture is the original repository asset.
No online catalogue art or streams are embedded in this APK.
