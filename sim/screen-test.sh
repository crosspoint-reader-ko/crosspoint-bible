#!/bin/sh
# Boot CrossPoint Bible in x4prosim, walk scenarios and check every screenshot.
# usage: sim/screen-test.sh [-e ENV] [-b] [--bless] [SCENARIO...]
#   -e ENV    PlatformIO env (default: default). C3 envs run on the X3 machine.
#   -b        build the firmware first (pio run -e ENV)
#   --bless   save this run's shots as sim/golden/<scenario>/ instead of comparing
#   SCENARIO  names of sim/scenarios/*.steps (default: all)
# The SD card holds /bible/krv.cpb, converted from $BIBLE_EPUB (default: the
# 개역개정 EPUB in iCloud Drive Books/_성경). The pack is copyrighted text and
# stays in sim/out (gitignored).
# Needs the workspace emulator checkout ../../x4prosim and ../../sim/shotdiff.py.
# The X3 ink model leaves ghosts of earlier screens (not calibrated, see
# ../../sim/README.md). Shots use ideal ink unless REAL_INK=1.
set -u
repo=$(cd "$(dirname "$0")/.." && pwd)
ws=$(cd "$repo/.." && pwd)
simsrc=$ws/x4prosim
shotdiff=$ws/sim/shotdiff.py
PATH=$HOME/.platformio/penv/bin:$PATH
env=default build=0 bless=0
while [ $# -gt 0 ]; do
  case $1 in
    -e) env=$2; shift 2 ;;
    -b) build=1; shift ;;
    --bless) bless=1; shift ;;
    -*) sed -n '2,11p' "$0"; exit 2 ;;
    *) break ;;
  esac
done
scenarios=${*:-$(cd "$repo/sim/scenarios" && ls *.steps | sed 's/\.steps$//')}
out=$repo/sim/out
mkdir -p "$out"

if [ "$build" = 1 ]; then
  (cd "$repo" && pio run -e "$env") > "$out/pio-$env.log" 2>&1 \
    || { echo "FAIL firmware build, see sim/out/pio-$env.log"; exit 1; }
fi
bdir=$repo/.pio/build/$env
[ -f "$bdir/firmware.bin" ] || { echo "no $bdir/firmware.bin (run with -b)"; exit 2; }

epub=${BIBLE_EPUB:-$(ls "$HOME/Library/Mobile Documents/com~apple~CloudDocs/Books/_성경/"*.epub 2>/dev/null | head -1)}
sd=$out/sd
if [ ! -f "$sd/bible/krv.cpb" ] || [ "$repo/tools/bible/epub2cpb.py" -nt "$sd/bible/krv.cpb" ]; then
  [ -f "$epub" ] || { echo "no Bible EPUB (set BIBLE_EPUB)"; exit 2; }
  mkdir -p "$sd/bible"
  python3 "$repo/tools/bible/epub2cpb.py" "$epub" "$sd/bible/krv.cpb" > "$out/epub2cpb.log" 2>&1 \
    || { echo "FAIL epub2cpb, see sim/out/epub2cpb.log"; exit 1; }
fi

flash=$out/flash-$env.bin
"$simsrc/x4prosim/mkflash.sh" "$bdir" "$flash" > "$out/mkflash.log" 2>&1 \
  || { echo "FAIL mkflash, see $out/mkflash.log"; exit 1; }
echo "firmware $env ($(git -C "$repo" describe --always --dirty)), x4prosim $(git -C "$simsrc" rev-parse --short HEAD)"

[ "${REAL_INK:-0}" = 1 ] || export QEMU_EXTRA="${QEMU_EXTRA:-} -global uc8279.swing-frames=1 -global uc8279.dead-frames=0"

fail=0
for sc in $scenarios; do
  steps=$repo/sim/scenarios/$sc.steps
  [ -f "$steps" ] || { echo "FAIL $sc: no $steps"; fail=1; continue; }
  dir=$out/screen/$sc
  rm -rf "$dir"; mkdir -p "$dir"
  # "# sd: empty" in a scenario boots without /bible on the card (built-in pack only).
  sdsrc=$sd
  if grep -q '^# sd: empty' "$steps"; then sdsrc=$out/sd-empty; mkdir -p "$sdsrc"; fi
  python3 "$simsrc/x4prosim/mksd.py" "$dir/sd.img" 256 "$sdsrc" > /dev/null \
    || { echo "FAIL $sc: SD image"; fail=1; continue; }

  # Expand shot:NAME into PPM (compared) and PNG (for people) screendumps.
  set --
  shots=""
  while IFS= read -r line; do
    line=${line%%#*}; line=$(echo "$line" | tr -d '[:space:]')
    [ -n "$line" ] || continue
    case $line in
      shot:*) n=${line#shot:}; shots="$shots $n"
              set -- "$@" "hmp:screendump $dir/$n.ppm panel" "hmp:screendump $dir/$n.png -f png panel" ;;
      *) set -- "$@" "$line" ;;
    esac
  done < "$steps"

  start=$(date +%s)
  python3 "$simsrc/x4prosim/drive.py" "$flash" "$dir/sd.img" "$dir/log.txt" "$@" > "$dir/drive.out" 2>&1
  echo "== $sc ($(( $(date +%s) - start ))s)"

  if grep -nE "Guru Meditation|abort\(\) was called|assert failed|Stack canary|Backtrace:" "$dir/log.txt" > "$dir/panic.txt"; then
    echo "FAIL $sc: firmware crashed:"; head -3 "$dir/panic.txt"; fail=1
  fi
  for n in $shots; do
    shot=$dir/$n.ppm
    if [ ! -s "$shot" ]; then echo "FAIL $sc/$n: no screenshot"; fail=1; continue; fi
    python3 "$shotdiff" check "$shot" || fail=1
    gold=$repo/sim/golden/$sc/$n.ppm
    if [ "$bless" = 1 ]; then
      mkdir -p "$repo/sim/golden/$sc"; cp "$shot" "$dir/$n.png" "$repo/sim/golden/$sc/"
      echo "     blessed $sc/$n"
    elif [ -f "$gold" ]; then
      python3 "$shotdiff" diff "$shot" "$gold" "$dir/diff-$n.ppm" || fail=1
    else
      echo "     no golden for $sc/$n (review $dir/$n.png, then --bless)"
    fi
  done
done
[ "$fail" = 0 ] && echo "PASS" || echo "FAILED (details in sim/out/screen/)"
exit "$fail"
