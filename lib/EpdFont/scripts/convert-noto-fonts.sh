#!/bin/bash
# Regenerate the built-in fonts of the multilingual CrossPoint Bible firmware:
#   reader  noto_serif_{14,16,18,22,28}_regular.h  Noto Serif, 2-bit, DEFLATE (16 KB groups)
#   UI      noto_sans_10_regular.h                 Noto Sans, 2-bit, uncompressed (Settings)
#           noto_sans_{14,16,22}_regular.h         Noto Sans, 2-bit, DEFLATE (Bible menus and chrome)
# Coverage: fontconvert.py's default Latin/Cyrillic blocks plus Greek, Latin Extended
# Additional, spacing modifier letters (U+02BE ...) and the 27 Hebrew letters
# (from Noto Sans Hebrew, which backs up Noto Serif in the font stack). Bold and
# italic are synthesized by the firmware, so only regular is built.
#
# Requires scripts/requirements.txt (a venv at scripts/venv is used when present).
# Usage:  lib/EpdFont/scripts/convert-noto-fonts.sh [SIZE...]

set -e
cd "$(dirname "$0")"
PY="${PYTHON:-python3}"
[ -x venv/bin/python ] && [ -z "$PYTHON" ] && PY=venv/bin/python
SIZES=("${@:-14 16 18 22 28}")
SERIF="../builtinFonts/source/NotoSerif/NotoSerif-Regular.ttf"
SANS="../builtinFonts/source/NotoSans/NotoSans-Regular.ttf"
HEBREW="../builtinFonts/source/NotoSansHebrew/NotoSansHebrew-Regular.ttf"

# Added on top of fontconvert.py's defaults (which have no Greek).
INTERVALS=(
  --additional-intervals 0x02B0,0x02FF   # modifier letters: ʾ ʿ ˈ ˌ ...
  --additional-intervals 0x0370,0x03FF   # Greek
  --additional-intervals 0x05D0,0x05EA   # Hebrew letters
  --additional-intervals 0x1E00,0x1EFF   # Latin Extended Additional: ṭ ḥ ẹ ...
)

pids=()
for size in ${SIZES[@]}; do
  name=noto_serif_${size}_regular
  echo "Generating $name.h ..."
  $PY fontconvert.py "$name" "$size" "$SERIF" "$HEBREW" --2bit --compress --max-group-bytes 16384 \
    "${INTERVALS[@]}" > "../builtinFonts/$name.h" &
  pids+=($!)
done

for size in 14 16 22; do
  name=noto_sans_${size}_regular
  echo "Generating $name.h ..."
  $PY fontconvert.py "$name" "$size" "$SANS" "$HEBREW" --2bit --compress --max-group-bytes 16384 \
    "${INTERVALS[@]}" > "../builtinFonts/$name.h" &
  pids+=($!)
done

echo "Generating noto_sans_10_regular.h ..."
$PY fontconvert.py noto_sans_10_regular 10 "$SANS" --2bit \
  --additional-intervals 0x02B0,0x02FF \
  --additional-intervals 0x0370,0x03FF \
  > ../builtinFonts/noto_sans_10_regular.h &
pids+=($!)

for pid in ${pids[@]}; do wait "$pid"; done
echo "Done."
