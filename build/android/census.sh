#!/bin/sh
# Portability census for Android (arm64-v8a).
#
# Runs the NDK clang with -fsyntax-only over every .cpp in the tree and counts
# how many compile. It does not link and produces no objects. It is the port's
# compass: attack whatever cause appears most often, re-measure, repeat.
#
# Inherited from build/kindle/census.sh in the Kindle port.
set -u
cd "$(dirname "$0")/../.." || exit 1
ROOT=$(pwd)
OUT=build/android

# --- locate the NDK -------------------------------------------------------
NDK="${ANDROID_NDK_HOME:-}"
if [ -z "$NDK" ]; then
  for c in /opt/homebrew/share/android-ndk \
           /opt/homebrew/Caskroom/android-ndk/*/AndroidNDK*.app/Contents/NDK \
           "$HOME"/Library/Android/sdk/ndk/*; do
    [ -d "$c" ] && NDK="$c" && break
  done
fi
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
  echo "NDK not found. Set ANDROID_NDK_HOME." >&2
  exit 1
fi
API=${ANDROID_API:-28}
CC="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android$API-clang++"
if [ ! -x "$CC" ]; then
  echo "clang++ not found at $CC" >&2
  ls "$NDK/toolchains/llvm/prebuilt/" 2>/dev/null >&2
  exit 1
fi
echo "NDK:  $NDK"
echo "CC:   $(basename "$CC")"
echo "API:  $API   ABI: arm64-v8a"
echo

# --- include path ----------------------------------------------------------
INC="-Ithird_party -Ilib/hal/posix/arduino-shim -Ilib/hal/posix -Ilib/hal/android -Ilib/hal"
for d in $(find freeink-sdk/libs -type d -name include); do INC="$INC -I$d"; done
for d in lib/*/; do INC="$INC -I${d%/}"; done
INC="$INC -Ilib/uzlib/src -Ilib/miniz/src -Isrc -Ilib -Isrc/components -Isrc/activities -Isrc/util -Isrc/network"
INC="$INC -Ifreeink-sdk/libs/book/FreeInkBook/third_party/libunibreak"
INC="$INC -Ifreeink-sdk/libs/book/FreeInkBook/third_party/tjpgd"
VER=${CROSSPOINT_VERSION:-android-dev}
DEF="-D${FREEINK_DEVICE:-FREEINK_DEVICE_HIBREAK}=1 -DANDROID=1"
# ArduinoJson only registers the String converter when it knows it is in an
# Arduino environment. Here String comes from our shim, so its automatic
# detection (which looks for ARDUINO) never fires and it falls back to
# std::string. The flag turns the converter on and makes it include <WString.h>.
mkdir -p build/android
cat > build/android/defines.h <<DEFS
#pragma once
#define CROSSPOINT_VERSION "$VER"
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
DEFS
DEF="$DEF -include build/android/defines.h"

mkdir -p "$OUT"
: > "$OUT/census-errors.txt"
: > "$OUT/fails.txt"
ok=0; fail=0
for f in $(find src lib freeink-sdk/libs -name '*.cpp' 2>/dev/null \
           | grep -vE 'expat|miniz|uzlib|/test/|/tools/|lib/hal/kindle/|FreeInkDisplay/src/'); do
  if "$CC" -std=c++20 -fsyntax-only $INC $DEF "$f" > "$OUT/one.err" 2>&1; then
    ok=$((ok+1))
  else
    fail=$((fail+1))
    echo "$f" >> "$OUT/fails.txt"
    grep -m1 -E "fatal error|error:" "$OUT/one.err" | sed "s|.*error: ||" >> "$OUT/census-errors.txt"
  fi
done
total=$((ok+fail))
[ "$total" -eq 0 ] && { echo "no files found"; exit 1; }
echo "CENSUS: $ok of $total files compile ($(( ok * 100 / total ))%)"
echo
echo "--- failure causes, by frequency ---"
sed -E "s/'[^']*' file not found/HEADER/; s/([a-zA-Z0-9_\/.]+\.h): No such file.*/missing header: \1/" "$OUT/census-errors.txt" \
  | cut -c1-72 | sort | uniq -c | sort -rn | head -20
