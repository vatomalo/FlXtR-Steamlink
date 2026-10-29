#!/usr/bin/env bash
# Optional network integration test. Prepare the Android build first.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
: "${ANDROID_HOME:?Set ANDROID_HOME to the SDK used for the Android build}"
json=$(find "${GRADLE_USER_HOME:-$HOME/.gradle}/caches/modules-2/files-2.1/org.json/json/20240303" -name '*.jar' -print -quit)
build="$repo/build/android-resolver-smoke"
mkdir -p "$build/include" "$build/classes"
cp "$ANDROID_HOME/ndk/28.0.13004108/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/jni.h" "$build/include/"
cc -shared -fPIC -Os -std=c11 -D_POSIX_C_SOURCE=200809L -DNDEBUG -DWASM_RT_USE_MMAP=0 -DWASM_RT_MEMCHECK_BOUNDS_CHECK=1 \
    -I"$build/include" -Ibuild/resolver -Isrc build/resolver/img_data.c src/resolver_bridge.c \
    build/resolver/wasm-rt-impl.c build/resolver/wasm-rt-mem-impl.c build/resolver/wasm-rt-exceptions-impl.c \
    android/app/src/main/cpp/decoder_jni.c -o "$build/libflxtr_resolver.so" -lm -lpthread
java --add-modules jdk.compiler com.sun.tools.javac.Main -cp "$json" -d "$build/classes" \
    android/app/src/main/java/org/flxtr/android/{CatalogItem,MediaCaptions,StreamProtocol,StreamDecoder,StreamResolver}.java tests/android/StreamResolverSmoke.java
java -Djava.library.path="$build" -cp "$build/classes:$json" org.flxtr.android.StreamResolverSmoke
java -Djava.library.path="$build" -cp "$build/classes:$json" org.flxtr.android.StreamResolverSmoke episode
