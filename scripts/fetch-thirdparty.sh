#!/bin/sh
# Fetches the third-party dependencies platformio.ini declares and PlatformIO
# would normally resolve on its own. Outside PlatformIO nobody fetches them, and
# 83 of the 91 failures in the first census were exactly that.
#
# Everything is version-pinned. No "latest": the port has to be reproducible and
# the versions are the ones platformio.ini names.
set -eu
cd "$(dirname "$0")/.." || exit 1
DST=third_party
mkdir -p "$DST"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

fetch() { # url destino
  echo "  fetching $(basename "$2")"
  curl -fsSL "$1" -o "$2"
}

# --- ArduinoJson 7.4.2, header unico, MIT --------------------------------
if [ ! -f "$DST/ArduinoJson.h" ]; then
  echo "ArduinoJson 7.4.2"
  fetch "https://github.com/bblanchon/ArduinoJson/releases/download/v7.4.2/ArduinoJson-v7.4.2.h" \
        "$DST/ArduinoJson.h"
fi

# --- PNGdec 1.1.6, MIT ----------------------------------------------------
if [ ! -f "$DST/PNGdec.h" ]; then
  echo "PNGdec 1.1.6"
  fetch "https://github.com/bitbank2/PNGdec/archive/refs/tags/1.1.6.tar.gz" "$TMP/pngdec.tgz"
  tar -xzf "$TMP/pngdec.tgz" -C "$TMP"
  cp "$TMP"/PNGdec-1.1.6/src/*.h "$TMP"/PNGdec-1.1.6/src/*.inc "$TMP"/PNGdec-1.1.6/src/*.inl "$DST"/ 2>/dev/null || true
  cp "$TMP"/PNGdec-1.1.6/src/*.cpp "$DST"/ 2>/dev/null || true
fi

# --- JPEGDEC, commit preso, Apache 2.0 ------------------------------------
JPEGREV=86282979224c8a32fd51e091ed5a35b0c699a52b
if [ ! -f "$DST/JPEGDEC.h" ]; then
  echo "JPEGDEC @ $JPEGREV"
  fetch "https://github.com/bitbank2/JPEGDEC/archive/$JPEGREV.tar.gz" "$TMP/jpegdec.tgz"
  tar -xzf "$TMP/jpegdec.tgz" -C "$TMP"
  cp "$TMP"/JPEGDEC-$JPEGREV/src/*.h "$TMP"/JPEGDEC-$JPEGREV/src/*.inc "$TMP"/JPEGDEC-$JPEGREV/src/*.inl "$DST"/ 2>/dev/null || true
  cp "$TMP"/JPEGDEC-$JPEGREV/src/*.cpp "$DST"/ 2>/dev/null || true
fi

# --- QRCode 0.0.1, MIT ----------------------------------------------------
if [ ! -f "$DST/qrcode.h" ]; then
  echo "QRCode 0.0.1"
  fetch "https://github.com/ricmoo/QRCode/archive/refs/tags/v0.0.1.tar.gz" "$TMP/qrcode.tgz" \
    || fetch "https://github.com/ricmoo/QRCode/archive/refs/tags/0.0.1.tar.gz" "$TMP/qrcode.tgz"
  tar -xzf "$TMP/qrcode.tgz" -C "$TMP"
  find "$TMP" -path '*QRCode*/src/*' \( -name '*.h' -o -name '*.c' \) -exec cp {} "$DST"/ \;
fi

# --- stb_truetype, dominio publico / MIT ----------------------------------
# Not from platformio.ini: an internal FreeInkBook dependency (TtfFont.cpp).
if [ ! -f "$DST/stb_truetype.h" ]; then
  echo "stb_truetype"
  fetch "https://raw.githubusercontent.com/nothings/stb/f0569113c93ad095470c54bf34a17b36646bbbb5/stb_truetype.h" \
        "$DST/stb_truetype.h"
fi

# --- pngle, MIT -----------------------------------------------------------
# Same: an internal FreeInkBook dependency (ImageRenderer.cpp).
if [ ! -f "$DST/pngle.h" ]; then
  echo "pngle"
  fetch "https://raw.githubusercontent.com/kikuchan/pngle/master/src/pngle.h" "$DST/pngle.h"
  fetch "https://raw.githubusercontent.com/kikuchan/pngle/master/src/pngle.c" "$DST/pngle.c"
  fetch "https://raw.githubusercontent.com/kikuchan/pngle/master/src/miniz.h" "$DST/pngle_miniz.h" 2>/dev/null || true
fi

echo
echo "--- third_party ---"
ls -1 "$DST" | head -40
echo "total: $(ls -1 "$DST" | wc -l | tr -d ' ') files, $(du -sh "$DST" | cut -f1)"
