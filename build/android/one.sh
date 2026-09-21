#!/bin/sh
# Compila UM arquivo com exatamente o mesmo include path do censo e mostra o
# erro inteiro. Equivalente ao whichfailed.sh do porte Kindle.
set -u
cd "$(dirname "$0")/../.." || exit 1
[ $# -ge 1 ] || { echo "uso: build/android/one.sh <arquivo.cpp> [flags extras]"; exit 1; }
F=$1; shift
NDK="${ANDROID_NDK_HOME:-/opt/homebrew/share/android-ndk}"
API=${ANDROID_API:-28}
CC="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android$API-clang++"
INC="-Ithird_party -Ilib/hal/posix/arduino-shim -Ilib/hal/posix -Ilib/hal/android -Ilib/hal"
for d in $(find freeink-sdk/libs -type d -name include); do INC="$INC -I$d"; done
for d in lib/*/; do INC="$INC -I${d%/}"; done
INC="$INC -Ilib/uzlib/src -Ilib/miniz/src -Isrc -Ilib -Isrc/components -Isrc/activities -Isrc/util -Isrc/network"
INC="$INC -Ifreeink-sdk/libs/book/FreeInkBook/third_party/libunibreak"
INC="$INC -Ifreeink-sdk/libs/book/FreeInkBook/third_party/tjpgd"
VER=${CROSSPOINT_VERSION:-android-dev}
DEF="-D${FREEINK_DEVICE:-FREEINK_DEVICE_HIBREAK}=1 -DANDROID=1"
DEF="$DEF -DCROSSPOINT_VERSION=\"$VER\" -DARDUINOJSON_ENABLE_ARDUINO_STRING=1"
exec "$CC" -std=c++20 -fsyntax-only $INC $DEF "$@" "$F"
