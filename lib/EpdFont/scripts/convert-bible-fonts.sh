#!/bin/bash
# Regenerate the CrossPoint Bible reader fonts: KoPub Batang Medium at five sizes,
# Hangul limited to KS X 1001 (2,350 syllables; the 개역개정 text uses 1,234, all
# inside it), no Hanja, DEFLATE-compressed. See docs/bible-plan.md.
#
# Requires scripts/requirements.txt (a venv at scripts/venv is used when present).
# Usage:  lib/EpdFont/scripts/convert-bible-fonts.sh [SIZE...]

set -e
cd "$(dirname "$0")"
PY="${PYTHON:-python3}"
[ -x venv/bin/python ] && [ -z "$PYTHON" ] && PY=venv/bin/python
pids=()
SIZES=("${@:-14 17 20 24 28}")
SRC="../builtinFonts/source/KoPub-Batang/KoPub Batang_Pro Medium.otf"
KSX_INTERVALS=($($PY ks_x_1001_intervals.py))

for size in ${SIZES[@]}; do
  name=bible_${size}_regular
  echo "Generating $name.h ..."
  $PY fontconvert.py "$name" "$size" "$SRC" --2bit --compress --max-group-bytes 16384 \
    --additional-intervals 0x3000,0x303F \
    --additional-intervals 0x3130,0x318F \
    "${KSX_INTERVALS[@]}" > "../builtinFonts/$name.h" &
  pids+=($!)
done
for pid in ${pids[@]}; do wait "$pid"; done

# fontconvert.py records its whole argv in the header comment (~56 KB with the
# KS X 1001 intervals); point at this script instead.
for size in ${SIZES[@]}; do
  $PY - "../builtinFonts/bible_${size}_regular.h" <<'PYEOF'
import re, sys
p = sys.argv[1]
t = open(p, encoding="utf-8").read()
t = re.sub(r"^ \* Command used: .*$", " * Command used: lib/EpdFont/scripts/convert-bible-fonts.sh\n * Hangul coverage: KS X 1001 (2,350 syllables), no Hanja.", t, count=1, flags=re.M)
open(p, "w", encoding="utf-8").write(t)
PYEOF
done
echo "Done."
