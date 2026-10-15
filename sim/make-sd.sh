#!/bin/sh
# Build SD card folders for the language scenarios from the language bundles.
# usage: sim/make-sd.sh [bundles dir, default dist/bundles]
#   sim/out/sd-<code>   one bundle, booting in that language (/.crosspoint/bible-lang.txt)
#   sim/out/sd-all      every bundle, booting in English
set -eu
repo=$(cd "$(dirname "$0")/.." && pwd)
bundles=${1:-$repo/dist/bundles}
out=$repo/sim/out
mkdir -p "$out"
rm -rf "$out/sd-all"; mkdir -p "$out/sd-all/.crosspoint"
for zip in "$bundles"/crosspoint-bible-*-*.zip; do
  code=${zip##*-}; code=${code%.zip}
  dir=$out/sd-$code
  rm -rf "$dir"; mkdir -p "$dir/.crosspoint"
  python3 -c "import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$zip" "$dir"
  printf 'lang\t%s\n' "$code" > "$dir/.crosspoint/bible-lang.txt"
  python3 -c "import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$zip" "$out/sd-all"
done
echo "SD folders in $out: $(ls -d "$out"/sd-* | wc -l | tr -d ' ')"
