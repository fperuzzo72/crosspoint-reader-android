#!/bin/sh
# O build Android LINKA?
#
# build/android/census.sh responde "este arquivo compilaria", que e pergunta
# mais fraca do que parece: uma arvore pode estar 100% valida sintaticamente e
# nao ter nada para linkar, porque um arquivo que nao compila leva os simbolos
# dele junto e todo chamador vira referencia indefinida.
#
# Entao aqui compila tudo que compila, linka, e agrupa as referencias
# indefinidas por frequencia. O numero e a distancia honesta ate um binario.
# Ele cai em degraus, porque cada queda e um ARQUIVO ou uma subarvore passando
# a compilar, nao um simbolo de cada vez.
#
# Adaptado de tools/kindle/trylink.sh. As licoes duras (wrappers de C,
# arquivamento por biblioteca, invalidacao por header) vieram todas de la e
# estao comentadas onde importam.
set -u

NDK="${ANDROID_NDK_HOME:-/opt/homebrew/share/android-ndk}"
API=${ANDROID_API:-28}
TC="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin"
CXX="$TC/aarch64-linux-android$API-clang++"
CC="$TC/aarch64-linux-android$API-clang"
AR="$TC/llvm-ar"
OUT=build/android/link
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
[ -x "$CXX" ] || { echo "NDK nao encontrado em $NDK" >&2; exit 1; }
mkdir -p "$OUT"

INC="-Ithird_party -Ilib/hal/posix/arduino-shim -Ilib/hal/posix -Ilib/hal/android -Ilib/hal"
for d in $(find freeink-sdk/libs -type d -name include); do INC="$INC -I$d"; done
for d in lib/*/; do INC="$INC -I${d%/}"; done
INC="$INC -Ilib/uzlib/src -Ilib/miniz/src -Isrc -Ilib"
INC="$INC -Isrc/components -Isrc/activities -Isrc/util -Isrc/network"
INC="$INC -Ifreeink-sdk/libs/book/FreeInkBook/third_party/libunibreak"
INC="$INC -Ifreeink-sdk/libs/book/FreeInkBook/third_party/tjpgd"
VER=${CROSSPOINT_VERSION:-android-dev}
DEF="-D${FREEINK_DEVICE:-FREEINK_DEVICE_KINDLE}=1 -DANDROID=1"
mkdir -p build/android
cat > build/android/defines.h <<DEFS
#pragma once
#define CROSSPOINT_VERSION "$VER"
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
DEFS
DEF="$DEF -include build/android/defines.h"
# expat e configurado por defines, nao por header, e xmlparse.c recusa sem eles.
CDEFS="-DXML_GE=0 -DXML_CONTEXT_BYTES=1024"

# Um objeto velho carregando uma constante velha e a pior forma que um erro de
# build toma, porque nada falha. A regra de timestamp abaixo so olha o proprio
# fonte, e nao diz nada sobre os headers que ele inclui. Entao: qualquer header
# mais novo que o carimbo descarta TODOS os objetos.
NEWEST_HEADER=$(find lib src freeink-sdk third_party \
    \( -name '*.h' -o -name '*.hpp' \) -newer "$OUT/.stamp" 2>/dev/null | head -1)
if [ ! -f "$OUT/.stamp" ] || [ -n "$NEWEST_HEADER" ]; then
    echo "--- header mudou (${NEWEST_HEADER:-primeira rodada}); descartando objetos ---"
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
# Um arquivo que nao compila nao deixa objeto, e o link reporta os simbolos
# dele como indefinidos. Isso E a medicao, nao uma falha a tratar.
exit 0
HELPER
chmod +x "$OUT/cc-one.sh"

# FreeInkDisplay/src e a pilha de driver de painel: os PanelDriver e o EpdBus.
# Aqui nao ha painel cru; compilar isso so produz objeto referenciando um
# barramento que nao existe.
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

# Alguns .c vendorizados nao sao autonomos: um wrapper aplica um prefixo de
# simbolo e depois inclui o arquivo cru. Ha mais de uma copia de miniz nesta
# arvore, cada uma com seu prefixo, e compilar tambem os fontes crus da a cada
# simbolo duas definicoes. Em vez de fixar nomes, le os wrappers e exclui
# exatamente o que eles incluem: continua correto se a vendorizacao mudar.
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
    echo "--- excluindo $(wc -l < "$OUT/wrapped.txt" | tr -d ' ') fontes C que um wrapper ja inclui ---"
else
    cp "$OUT/csources.raw" "$OUT/csources.txt"
fi

echo "--- compilando ($(wc -l < "$OUT/sources.txt" | tr -d ' ') C++, $(wc -l < "$OUT/csources.txt" | tr -d ' ') C, -j$JOBS, incremental) ---"
cat "$OUT/sources.txt" "$OUT/csources.txt" | xargs -P "$JOBS" -n1 "$OUT/cc-one.sh"
echo "objetos: $(ls "$OUT"/*.o 2>/dev/null | wc -l | tr -d ' ')"

# Agrupar em arquivos .a por biblioteca, nao linkar objetos soltos. Isto nao e
# cosmetico: ha mais de uma copia de miniz na arvore, cada uma com config que
# prefixa so parte dos simbolos, e o resto colide. O PlatformIO nunca ve isso
# porque arquiva cada biblioteca e o linker puxa um membro so quando ele
# resolve algo ainda indefinido: duplicata entre arquivos e "o primeiro vence",
# nao erro. Linkar solto forca toda definicao para dentro.
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

# App e HAL entram soltos: definem main() e os globais que ninguem referencia
# por nome, que um arquivo .a descartaria.
objs=$(ls "$OUT"/src_*.o "$OUT"/tools_*.o "$OUT"/lib_hal_*.o 2>/dev/null | tr '\n' ' ')
archives=$(ls "$OUT"/*.a 2>/dev/null | grep -vE 'libapp\.a|libhal\.a' | tr '\n' ' ')
echo "arquivos .a: $(echo $archives | wc -w | tr -d ' ')"

echo "--- tentando linkar ---"
# --gc-sections e o que o build de firmware usa, e nao e so tamanho: helpers
# declarados e chamados mas nunca definidos no subconjunto vendorizado
# aparecem como indefinidos sem ele, mesmo em codigo que nunca roda.
# Arquivos por ultimo e repetidos (--start-group): as bibliotecas se
# referenciam e uma passada so perderia simbolos puxados por membro posterior.
# No Android nao existe -lrt: bionic poe tudo em libc. pthread idem.
"$CXX" -o "$OUT/crosspoint" $objs \
    -static-libstdc++ \
    -Wl,--gc-sections \
    -Wl,--start-group $archives -Wl,--end-group \
    -llog -lz 2>"$OUT/link.err"
# -lz: o PNGdec usa zlib, e o Android tem libz no sistema. No firmware ESP32
# a inflacao vem do miniz vendorizado; aqui nao ha motivo para carregar uma
# copia quando o bionic ja traz uma.
echo "saida do link: $?"
echo
echo "--- simbolos indefinidos, por frequencia ---"
grep -oE "undefined (reference to|symbol:) .?[^'\"]*" "$OUT/link.err" \
  | sed -E "s/undefined (reference to|symbol:) //" | sort | uniq -c | sort -rn | head -20
echo
echo "referencias indefinidas distintas: $(grep -oE "undefined (reference to|symbol:) .?[^'\"]*" "$OUT/link.err" | sort -u | wc -l | tr -d ' ')"
