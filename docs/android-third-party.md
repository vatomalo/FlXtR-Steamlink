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


Embedded Moonlight UI/JNI: GPL-3.0, upstream 12.1 commit
1b1b100e63e8ffcf1496e324ff7b57ee9f2db2a9:
https://github.com/moonlight-stream/moonlight-android/tree/1b1b100e63e8ffcf1496e324ff7b57ee9f2db2a9
moonlight-common-c 8af4562af672dd6b9ed28553ead172984fd9a683 and ENet
 d3a323fc8b9559786ee059205db74e8027c756f7 are pinned submodules.
Native OpenSSL 4.0.2 (Apache-2.0) and Opus (BSD) payloads come from Moonlight 12.2
commit b48494cb96bff23d8886c4775cc4f39a1075495d; libssl is not linked.
The generated native source tree overlays those crypto/audio payloads onto the
12.1 JNI interface. Build inputs and transformation are in prepare-android-games.py.

Gambatte Libretro: GPL-2.0-or-later, commit
77f2567d718b8973d02caa2daa8dce4953faf811:
https://github.com/libretro/gambatte-libretro/tree/77f2567d718b8973d02caa2daa8dce4953faf811
FlXtR's native host implements Libretro video, audio, input and memory/state APIs.
No game ROMs, commercial BIOS files or external core downloads are bundled.

Moonlight dependencies: Bouncy Castle bcprov 1.85.2/bcpkix 1.85 (MIT), OkHttp 4.12.0
(Apache-2.0), JCodec 0.2.5 (BSD), JmDNS 3.6.3 (Apache-2.0),
ShieldControllerExtensions 1.0.1 (Apache-2.0). Android desugar_jdk_libs 2.1.5 retains
its upstream notices. Full license texts are packaged under assets/runtime/licenses
and assets/licenses. Android distribution terms remain GPL-3.0-compatible.
