#!/bin/bash

set -e

cd "$(dirname "$0")"

# 12 is out: at the HiBreak's 300 dpi it is too small to read, measured on the
# device rather than inferred. 20 came in by the same test.
#
# On an ESP32 this list would be a budget decision: the glyphs are most of the
# binary and there is 380KB of RAM. On this target that ceiling does not exist,
# so the list is about reading rather than about memory.
# 22 and 24 are out: tested on the device and too large for real use. 12 went
# earlier for being too small. What remains is 14/16/18/20, the range you
# actually read at.
# Arrows, geometric shapes, dingbats and the mathematical operators that appear
# in running text. NO source font in this repository had them: measured,
# NotoSerif and Ubuntu gave 0 of 112 arrows and 1 of 96 geometric shapes. That
# is why they came out as boxes with "?" in books.
#
# Two fonts because one is not enough: Symbols2 covers geometric shapes (96/96)
# and dingbats (145/192) but only 13 of 112 arrows and lacks U+2192; Math has
# 99 of 112 arrows and has U+2192. The stack is ordered by descending priority,
# so they go in AFTER the main font and only fill what is missing.
SYMBOL_FONTS=(
  ../builtinFonts/source/NotoSansMath/NotoSansMath-Regular.ttf
  ../builtinFonts/source/NotoSansSymbols2/NotoSansSymbols2-Regular.ttf
)

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
NOTOSERIF_FONT_SIZES=(14 16 18 20)
NOTOSANS_FONT_SIZES=(14 16 18 20)

# UI_ONLY=1 skips the reader fonts. They are deterministic (the ID is the sum
# of the headers' SHA-256, and regenerating produces identical files), so
# skipping changes nothing but the time.
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
