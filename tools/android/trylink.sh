#!/bin/sh
# Does the Android build LINK?
#
# build/android/census.sh answers "would this file compile", which is a weaker
# question than it looks: a tree can be 100% syntactically valid and still have
# nothing to link, because a file that fails to compile takes its symbols with
# it and every caller shows up as an undefined reference.
#
# So this compiles everything that compiles, links it, and groups the undefined
# references by frequency. The number is the honest distance to a binary. It
# falls in steps, because each drop is a FILE or a subtree starting to compile,
# not one symbol at a time.
#
# Adapted from tools/kindle/trylink.sh. The hard-won lessons (C wrappers,
# archiving per library, invalidation on header change) all came from there and
# are commented where they matter.
set -u

NDK="${ANDROID_NDK_HOME:-/opt/homebrew/share/android-ndk}"
API=${ANDROID_API:-28}
TC="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin"
CXX="$TC/aarch64-linux-android$API-clang++"
CC="$TC/aarch64-linux-android$API-clang"
AR="$TC/llvm-ar"
OUT=build/android/link
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
[ -x "$CXX" ] || { echo "NDK not found at $NDK" >&2; exit 1; }
mkdir -p "$OUT"

INC="-Ithird_party -Ilib/hal/posix/arduino-shim -Ilib/hal/posix -Ilib/hal/android -Ilib/hal"
for d in $(find freeink-sdk/libs -type d -name include); do INC="$INC -I$d"; done
for d in lib/*/; do INC="$INC -I${d%/}"; done
INC="$INC -Ilib/uzlib/src -Ilib/miniz/src -Isrc -Ilib"
INC="$INC -Isrc/components -Isrc/activities -Isrc/util -Isrc/network"
INC="$INC -Ifreeink-sdk/libs/book/FreeInkBook/third_party/libunibreak"
INC="$INC -Ifreeink-sdk/libs/book/FreeInkBook/third_party/tjpgd"
VER=${CROSSPOINT_VERSION:-android-dev}
DEF="-D${FREEINK_DEVICE:-FREEINK_DEVICE_HIBREAK}=1 -DANDROID=1"
mkdir -p build/android
cat > build/android/defines.h <<DEFS
#pragma once
#define CROSSPOINT_VERSION "$VER"
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
DEFS
DEF="$DEF -include build/android/defines.h"
# expat is configured by defines rather than a config header, and xmlparse.c
# refuses to build without them.
CDEFS="-DXML_GE=0 -DXML_CONTEXT_BYTES=1024"

# A stale object carrying a stale constant is the worst shape a build error
# takes, because nothing fails. The timestamp rule below only looks at the
# source itself and says nothing about the headers it includes. So: any header
# newer than the stamp discards ALL objects.
NEWEST_HEADER=$(find lib src freeink-sdk third_party \
    \( -name '*.h' -o -name '*.hpp' \) -newer "$OUT/.stamp" 2>/dev/null | head -1)
if [ ! -f "$OUT/.stamp" ] || [ -n "$NEWEST_HEADER" ]; then
    echo "--- a header changed (${NEWEST_HEADER:-first run}); discarding objects ---"
    rm -f "$OUT"/*.o
fi
touch "$OUT/.stamp"

cat > "$OUT/cc-one.sh" <<HELPER
#!/bin/sh
f="\$1"
o="$OUT/\$(echo "\$f" | tr '/' '_' | sed 's/\.[cp]*\$/.o/')"
if [ -f "\$o" ] && [ "\$o" -nt "\$f" ]; then exit 0; fi
case "\$f" in
  *.c) $CC -Os -ffunction-sections -fdata-sections -c $INC $CDEFS "\$f" -o "\$o" 2>/dev/null ;;
  *)   $CXX -std=c++20 -Os -ffunction-sections -fdata-sections -c $INC $DEF "\$f" -o "\$o" 2>/dev/null ;;
esac
# A file that does not compile leaves no object, and the link reports its
# symbols as undefined. That IS the measurement, not a failure to handle.
exit 0
HELPER
chmod +x "$OUT/cc-one.sh"

# FreeInkDisplay/src is the panel driver stack: the PanelDrivers and the
# EpdBus. There is no raw panel here; compiling it only yields objects
# referencing a bus that cannot exist.
{ find src lib freeink-sdk/libs -name '*.cpp' 2>/dev/null
  find third_party -name '*.cpp' 2>/dev/null
  echo tools/android/main_android.cpp
} | grep -vE '/test/|/tools/kindle|lib/hal/kindle/' \
  | grep -vE 'FreeInkDisplay/src/' \
  | grep -v 'tools/android/main_android.cpp$' > "$OUT/sources.txt"
echo tools/android/main_android.cpp >> "$OUT/sources.txt"

{ find lib -name '*.c' -not -path 'lib/expat/*' 2>/dev/null
  find freeink-sdk/libs -name '*.c' 2>/dev/null
  find third_party -name '*.c' 2>/dev/null
} > "$OUT/csources.raw"

# Some vendored .c files are not standalone: a wrapper applies a symbol prefix
# and then includes the raw file. There is more than one copy of miniz in this
# tree, each with its own prefix, and compiling the raw sources as well gives
# every symbol two definitions. Rather than hardcoding names, read the wrappers
# and exclude exactly what they include: that stays correct if the vendoring
# changes.
: > "$OUT/wrapped.txt"
ROOT=$(pwd -P)
for w in $(grep -rl '#include.*\.c"' lib freeink-sdk/libs --include='*.c' 2>/dev/null); do
    dir=$(dirname "$w")
    grep -oE '#include "[^"]*\.c"' "$w" | sed 's/#include "//;s/"//' | while read -r inc; do
        (cd "$dir" && python3 -c "import os,sys;print(os.path.relpath(os.path.realpath(sys.argv[1]),'$ROOT'))" "$inc" 2>/dev/null) || true
    done >> "$OUT/wrapped.txt"
done
if [ -s "$OUT/wrapped.txt" ]; then
    grep -vFf "$OUT/wrapped.txt" "$OUT/csources.raw" > "$OUT/csources.txt" || cp "$OUT/csources.raw" "$OUT/csources.txt"
    echo "--- excluding $(wc -l < "$OUT/wrapped.txt" | tr -d ' ') C sources a wrapper already includes ---"
else
    cp "$OUT/csources.raw" "$OUT/csources.txt"
fi

echo "--- compiling ($(wc -l < "$OUT/sources.txt" | tr -d ' ') C++, $(wc -l < "$OUT/csources.txt" | tr -d ' ') C, -j$JOBS, incremental) ---"
cat "$OUT/sources.txt" "$OUT/csources.txt" | xargs -P "$JOBS" -n1 "$OUT/cc-one.sh"
echo "objects: $(ls "$OUT"/*.o 2>/dev/null | wc -l | tr -d ' ')"

# Group into .a archives per library rather than linking loose objects. This is
# not cosmetic: there is more than one copy of miniz in the tree, each with a
# config that prefixes only some of its symbols, and the rest collide.
# PlatformIO never sees it because it archives each library and the linker then
# pulls a member only when it resolves something still undefined: duplicates
# across archives are "first wins", not an error. Linking loose forces every
# definition in.
rm -f "$OUT"/*.a
for o in "$OUT"/*.o; do
    base=$(basename "$o")
    case "$base" in
        src_*|tools_*)   lib=app ;;
        lib_hal_*)       lib=hal ;;
        lib_*)           lib=$(echo "$base" | cut -d_ -f1-2) ;;
        freeink-sdk_*)   lib=$(echo "$base" | cut -d_ -f1-4) ;;
        *)               lib=thirdparty ;;
    esac
    "$AR" rcs "$OUT/lib$lib.a" "$o" 2>/dev/null
done

# App and HAL go in loose: they define main() and the globals nothing
# references by name, which an archive would drop.
objs=$(ls "$OUT"/src_*.o "$OUT"/tools_*.o "$OUT"/lib_hal_*.o 2>/dev/null | tr '\n' ' ')
archives=$(ls "$OUT"/*.a 2>/dev/null | grep -vE 'libapp\.a|libhal\.a' | tr '\n' ' ')
echo "archives: $(echo $archives | wc -w | tr -d ' ')"

echo "--- attempting a link ---"
# --gc-sections is what the firmware build uses, and it is not only about size:
# helpers declared and called but never defined in the vendored subset show up
# as undefined without it, even in code that never runs.
# Archives last and repeated (--start-group): the libraries reference each other
# and a single pass would miss symbols pulled in by a later member.
# There is no -lrt on Android: bionic puts it all in libc. Same for pthread.
"$CXX" -o "$OUT/crosspoint" $objs \
    -static-libstdc++ \
    -Wl,--gc-sections \
    -Wl,--start-group $archives -Wl,--end-group \
    -llog -lz -landroid 2>"$OUT/link.err"
# -landroid: ANativeWindow_lock/unlockAndPost/release, the lower half of
# AndroidPanel.
# -lz: PNGdec uses zlib, e o Android tem libz no sistema. No firmware ESP32
# inflation comes from the vendored miniz; here there is no reason to carry a
# copy when bionic already ships one.
echo "link exit: $?"
echo
echo "--- undefined symbols, by frequency ---"
grep -oE "undefined (reference to|symbol:) .?[^'\"]*" "$OUT/link.err" \
  | sed -E "s/undefined (reference to|symbol:) //" | sort | uniq -c | sort -rn | head -20
echo
echo "distinct undefined references: $(grep -oE "undefined (reference to|symbol:) .?[^'\"]*" "$OUT/link.err" | sort -u | wc -l | tr -d ' ')"
