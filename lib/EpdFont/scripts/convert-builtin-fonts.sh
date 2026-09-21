#!/bin/bash

set -e

cd "$(dirname "$0")"

# 12 saiu: a 300 dpi do HiBreak ele fica pequeno demais para ler, medido no
# aparelho e nao inferido. 20, 22 e 24 entraram pelo mesmo teste.
#
# Num ESP32 esta lista seria uma decisao de orcamento: os glifos sao a maior
# parte do binario e ha 380KB de RAM. Neste alvo esse teto nao existe, entao a
# lista e sobre leitura e nao sobre memoria.
# 22 e 24 sairam: testados no aparelho e grandes demais para uso real. 12
# saiu antes por ser pequeno demais. Sobra 14/16/18/20, que e a faixa que se
# usa lendo.
# Setas, formas geometricas, dingbats e os operadores matematicos que aparecem
# em texto corrido. NENHUMA fonte de origem deste repositorio os tinha: medido,
# NotoSerif e Ubuntu davam 0 de 112 setas e 1 de 96 formas geometricas. Por
# isso saiam como quadrado com "?" nos livros.
#
# Duas fontes porque uma nao basta: a Symbols2 cobre formas geometricas (96/96)
# e dingbats (145/192) mas so 13 de 112 setas e nao tem a U+2192; a Math tem
# 99 de 112 setas e a U+2192. A pilha e ordenada por prioridade decrescente,
# entao elas entram DEPOIS da fonte principal e so preenchem o que falta.
SYMBOL_FONTS=(
  ../builtinFonts/source/NotoSansMath/NotoSansMath-Regular.ttf
  ../builtinFonts/source/NotoSansSymbols2/NotoSansSymbols2-Regular.ttf
)

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
NOTOSERIF_FONT_SIZES=(14 16 18 20)
NOTOSANS_FONT_SIZES=(14 16 18 20)

# UI_ONLY=1 pula as fontes de leitura. Elas sao deterministicas (o ID e a soma
# dos SHA-256 dos cabecalhos, e regerar produz arquivo identico), entao pular
# nao muda nada alem do tempo.
if [ -z "${UI_ONLY:-}" ]; then

for size in ${NOTOSERIF_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notoserif_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSerif/NotoSerif-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path "${SYMBOL_FONTS[@]}" --2bit --compress --pnum --zopfli > $output_path
    echo "Generated $output_path"
  done
done

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/NotoSans/NotoSans-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path "${SYMBOL_FONTS[@]}" --2bit --compress --pnum --zopfli > $output_path
    echo "Generated $output_path"
  done
done

fi  # UI_ONLY

UI_FONT_SIZES=(10 12 14 16)
UI_FONT_STYLES=("Regular" "Bold")

# Arabic glyphs for UI text (menus, file browser titles). The built-in fonts
# must cover the *output* of MiniBidi's do_shape() — contextual presentation
# forms — not base letters, or shaped UI text silently drops glyphs.
# Curated for firmware-size budget: core Arabic (Presentation Forms-B,
# incl. the Lam-Alef ligature forms) plus the Farsi/Urdu extra letters'
# Presentation Forms-A blocks, the few characters shaping leaves at their
# base codepoint, Arabic punctuation, and both digit sets. No harakat and
# no Sindhi/Pashto/Kurdish forms — book text gets those from SD-card fonts.
ARABIC_INTERVALS=(
  --additional-intervals 0x060C,0x060C  # Arabic comma
  --additional-intervals 0x061B,0x061B  # Arabic semicolon
  --additional-intervals 0x061F,0x061F  # Arabic question mark
  --additional-intervals 0x0621,0x0621  # hamza (non-joining, never shaped)
  --additional-intervals 0x0640,0x0640  # tatweel
  --additional-intervals 0x0654,0x0654  # Persian/Urdu ezafe hamza, Arabic hamza carriers
  --additional-intervals 0x0660,0x0669  # Arabic-Indic digits
  --additional-intervals 0x06BA,0x06BA  # noon ghunna base (initial/medial keep base cp)
  --additional-intervals 0x06D4,0x06D4  # Urdu full stop
  --additional-intervals 0x06D5,0x06D5  # ae (isolated; Kurdish/Uyghur/Ottoman) — has no presentation form
  --additional-intervals 0x06F0,0x06F9  # extended Arabic-Indic digits (Farsi/Urdu)
  --additional-intervals 0xFB56,0xFB59  # peh (Farsi)
  --additional-intervals 0xFB66,0xFB69  # tteh (Urdu)
  --additional-intervals 0xFB7A,0xFB7D  # tcheh (Farsi)
  --additional-intervals 0xFB88,0xFB95  # ddal, jeh, rreh (Urdu), keheh, gaf (Farsi/Urdu)
  --additional-intervals 0xFB9E,0xFB9F  # noon ghunna isolated/final (Urdu)
  --additional-intervals 0xFBA6,0xFBB1  # heh goal, heh doachashmee, yeh barree(+hamza) (Urdu)
  --additional-intervals 0xFBFC,0xFBFF  # farsi yeh (Farsi/Urdu)
  --additional-intervals 0xFE80,0xFEFC  # Presentation Forms-B: core Arabic + Lam-Alef
)

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Ubuntu/Ubuntu-${style}.ttf"
    # Ubuntu lacks the Latin Extended Additional block (U+1EA0-U+1EF9) used for
    # Vietnamese tone marks. Append a Vietnamese-only Ubuntu cut so those glyphs
    # are filled from it while every glyph Ubuntu already has stays unchanged
    # (fontstack is ordered by descending priority).
    viet_path="../builtinFonts/source/Ubuntu/Ubuntu-Vietnamese-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path $viet_path "${SYMBOL_FONTS[@]}" \
      --2bit --compress --pnum --zopfli > $output_path
    echo "Generated $output_path"
  done
done

python fontconvert.py notosans_8_regular 8 \
  ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf "${SYMBOL_FONTS[@]}" \
  > ../builtinFonts/notosans_8_regular.h

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
