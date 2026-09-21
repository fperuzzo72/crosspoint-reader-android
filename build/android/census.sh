#!/bin/sh
# Censo de portabilidade para Android (arm64-v8a).
# Roda o clang do NDK com -fsyntax-only sobre cada .cpp da arvore e conta
# quantos compilam. Nao linka, nao gera objeto. E a bussola do porte:
# ataque a causa mais frequente, remeça, repita.
#
# Herdado de build/kindle/census.sh do porte Kindle.
set -u
cd "$(dirname "$0")/../.." || exit 1
ROOT=$(pwd)
OUT=build/android

# --- localizar o NDK -------------------------------------------------------
NDK="${ANDROID_NDK_HOME:-}"
if [ -z "$NDK" ]; then
  for c in /opt/homebrew/share/android-ndk \
           /opt/homebrew/Caskroom/android-ndk/*/AndroidNDK*.app/Contents/NDK \
           "$HOME"/Library/Android/sdk/ndk/*; do
    [ -d "$c" ] && NDK="$c" && break
  done
fi
if [ -z "$NDK" ] || [ ! -d "$NDK" ]; then
  echo "NDK nao encontrado. Defina ANDROID_NDK_HOME." >&2
  exit 1
fi
API=${ANDROID_API:-28}
CC="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android$API-clang++"
if [ ! -x "$CC" ]; then
  echo "clang++ nao encontrado em $CC" >&2
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
DEF="-D${FREEINK_DEVICE:-FREEINK_DEVICE_KINDLE}=1 -DANDROID=1"
# O ArduinoJson so registra o conversor de String quando sabe que esta num
# ambiente Arduino. Aqui o String vem do nosso shim, entao a deteccao
# automatica dele (que olha por ARDUINO) nao dispara e ele cai no
# std::string. A flag liga o conversor e faz ele incluir <WString.h>.
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
[ "$total" -eq 0 ] && { echo "nenhum arquivo encontrado"; exit 1; }
echo "CENSO: $ok de $total arquivos compilam ($(( ok * 100 / total ))%)"
echo
echo "--- causas das falhas, por frequencia ---"
sed -E "s/'[^']*' file not found/HEADER/; s/([a-zA-Z0-9_\/.]+\.h): No such file.*/missing header: \1/" "$OUT/census-errors.txt" \
  | cut -c1-72 | sort | uniq -c | sort -rn | head -20
