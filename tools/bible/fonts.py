#!/usr/bin/env python3
"""Fetch and prepare the fonts named in languages.json (reader families and UI fonts).

Every font comes from a pinned URL checked against its sha256 in fonts.lock.json
(google/fonts at a fixed commit, Pretendard at its v1.3.9 tag commit, a pinned
mirror commit for RIDIBatang), together with its license text. KoPub Batang comes
from this repository. Variable fonts are instanced to a static weight with fontTools.

The cache (default build/fonts-cache/lock) holds:
  <FontId>/<downloaded file>        exactly the locked bytes
  <FontId>/<FontId>.static.ttf|.otf  the static instance bundle.py renders from
  <FontId>/<license file>           OFL.txt / LICENSE

usage:
  fonts.py fetch [--cache DIR] [FontId ...]   download, verify, instance (default: all)
  fonts.py check [--cache DIR]                coverage report (Arabic Presentation Forms etc.)
  fonts.py lock  [FontId ...]                 re-download and write sha256 into fonts.lock.json

Needs fontTools (lib/EpdFont/scripts/venv has it).
"""

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, os.pardir, os.pardir))
LOCK = os.path.join(HERE, "fonts.lock.json")
DEFAULT_CACHE = os.path.join(ROOT, "build", "fonts-cache", "lock")
BUILTIN = {"NotoSerif", "NotoSans"}  # in the firmware; never fetched

# MiniBidi turns Arabic letters into these code points at runtime, so Arabic-script fonts must map them.
ARABIC_PF_A = (0xFB50, 0xFDFF)
ARABIC_PF_B = (0xFE70, 0xFEFF)


def load_lock():
    with open(LOCK, encoding="utf-8") as f:
        return json.load(f)


def save_lock(lock):
    with open(LOCK, "w", encoding="utf-8") as f:
        json.dump(lock, f, ensure_ascii=False, indent=1)
        f.write("\n")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(url, dest):
    tmp = dest + ".part"
    req = urllib.request.Request(url, headers={"User-Agent": "crosspoint-bible-fonts"})
    with urllib.request.urlopen(req, timeout=120) as r, open(tmp, "wb") as f:
        shutil.copyfileobj(r, f)
    os.replace(tmp, dest)


def fonts_in_languages(languages):
    ids = []
    for entry in languages["languages"]:
        for f in [r["font"] for r in entry["reader"]] + [entry["ui"]]:
            if f not in BUILTIN and f not in ids:
                ids.append(f)
    return ids


def _get(item, cache_dir, verify=True):
    """Download (or copy from the repo) one locked file into cache_dir; returns its path."""
    name = item.get("name") or os.path.basename(item.get("path") or item["url"].split("?")[0])
    name = urllib.request.url2pathname(name)
    dest = os.path.join(cache_dir, name)
    if "text" in item:
        with open(dest, "w", encoding="utf-8") as f:
            f.write(item["text"])
        return dest
    if "path" in item:
        src = os.path.join(ROOT, item["path"])
        if not os.path.exists(dest) or sha256(dest) != sha256(src):
            shutil.copyfile(src, dest)
    elif not (os.path.exists(dest) and item.get("sha256") and sha256(dest) == item["sha256"]):
        print(f"  fetch {item['url']}", file=sys.stderr)
        download(item["url"], dest)
    if verify and item.get("sha256"):
        got = sha256(dest)
        if got != item["sha256"]:
            raise SystemExit(f"sha256 mismatch for {dest}: {got} != {item['sha256']}")
    return dest


def instance_path(font_id, cache=DEFAULT_CACHE):
    lock = load_lock()["fonts"][font_id]
    d = os.path.join(cache, font_id)
    ext = os.path.splitext(lock.get("name") or lock.get("path") or lock["url"])[1].lower()
    return os.path.join(d, font_id + ".static" + (ext if ext in (".otf", ".ttf") else ".ttf"))


def fetch(font_id, cache=DEFAULT_CACHE, verify=True):
    """Fetch one font and its licenses. Returns (static font path, [license paths])."""
    lock = load_lock()["fonts"][font_id]
    d = os.path.join(cache, font_id)
    os.makedirs(d, exist_ok=True)
    src = _get(lock, d, verify)
    licenses = [_get(item, d, verify) for item in lock.get("license_files", [])]
    out = instance_path(font_id, cache)
    stamp = out + ".src"
    want = f"{sha256(src)} {json.dumps(lock.get('axes', {}), sort_keys=True)}"
    if not (os.path.exists(out) and os.path.exists(stamp) and open(stamp).read() == want):
        axes = lock.get("axes")
        if axes:
            from fontTools.ttLib import TTFont
            from fontTools.varLib import instancer

            font = TTFont(src)
            fvar = {a.axisTag: a for a in font["fvar"].axes}
            loc = {tag: axes.get(tag, a.defaultValue) for tag, a in fvar.items()}
            static = instancer.instantiateVariableFont(font, loc, updateFontNames=False)
            static.save(out)
        else:
            shutil.copyfile(src, out)
        with open(stamp, "w") as f:
            f.write(want)
    return out, licenses


# --- .epdfont writer (lib/EpdFont/SdFontFormat.h, version 1, 2-bit, uncompressed)
#
# Same rendering as lib/EpdFont/scripts/ttf_to_epdfont.py --2bit (FreeType, 150 DPI, hinted,
# advance floored to whole pixels, 2-bit = coverage >> 6, pixels packed MSB first across rows),
# but for an explicit set of code points so bundles carry only the glyphs they use.

EPDFONT_MAGIC = 0x46445045
DPI = 150
_Q2 = bytes(v >> 6 for v in range(256))


def _pack2(buf):
    q = buf.translate(_Q2)
    if len(q) % 4:
        q += bytes(4 - len(q) % 4)
    return bytes((q[i] << 6) | (q[i + 1] << 4) | (q[i + 2] << 2) | q[i + 3] for i in range(0, len(q), 4))


def _intervals(cps):
    out = []
    for cp in cps:
        if out and cp == out[-1][1] + 1:
            out[-1][1] = cp
        else:
            out.append([cp, cp])
    return out


# Blocks whose gaps are always filled (subset fonts would otherwise split them into
# hundreds of intervals).
DENSE_BLOCKS = [(0xAC00, 0xD7A3), (0x4E00, 0x9FFF), (0x3400, 0x4DBF), (0x3040, 0x30FF), (0xF0000, 0xFFFFD)]
SMALL_GAP = 64


def _fill_gaps(cps):
    """Code points to write, with the gaps between `cps` filled where cheap: gaps up to SMALL_GAP
    and any gap inside a DENSE_BLOCK. The device's SD font loader binary-searches the interval
    table on the card for every glyph outside the last-used interval, so a subset font with a
    thousand intervals (Hangul) makes every character cost ~10 SD reads. Filled code points get
    empty glyphs (16 bytes each, no bitmap)."""
    out = []
    for cp in sorted(cps):
        if out:
            prev = out[-1]
            gap = cp - prev - 1
            dense = any(a <= prev and cp <= b for a, b in DENSE_BLOCKS)
            if 0 < gap and (gap <= SMALL_GAP or dense):
                out.extend(range(prev + 1, cp))
        out.append(cp)
    return out


def load_flags(path):
    """FreeType load flags for a font from the cache: "hinting": "autohint" in its lock entry forces
    the auto-hinter. The Noto CJK fonts carry no hinting instructions, so their thin horizontal
    strokes land between two pixel rows and come out as two rows of gray that 2-bit drops;
    the auto-hinter snaps them to one dark row."""
    import freetype

    font_id = os.path.basename(os.path.dirname(os.path.abspath(path)))
    entry = load_lock()["fonts"].get(font_id, {})
    flags = freetype.FT_LOAD_RENDER
    if entry.get("hinting") == "autohint":
        flags |= freetype.FT_LOAD_FORCE_AUTOHINT
    return flags


def text_advance(font_path, size_pt, text):
    """Pen advance of text in whole pixels, rendered as render_epdfont does (no kerning)."""
    import freetype

    face = freetype.Face(font_path)
    face.set_char_size(size_pt << 6, size_pt << 6, DPI, DPI)
    total = 0
    for ch in text:
        face.load_char(ch, load_flags(font_path))
        total += face.glyph.advance.x >> 6
    return total


def render_epdfont(font_path, size_pt, codepoints, out_path):
    """Write a 2-bit .epdfont with the glyphs of `codepoints` the font has. Returns the missing code points.

    font_path may be a list: each code point comes from the first font that has it (fallbacks),
    the line metrics from the first."""
    import freetype
    import struct

    faces = []
    for p in [font_path] if isinstance(font_path, str) else font_path:
        f = freetype.Face(p)
        f.set_char_size(size_pt << 6, size_pt << 6, DPI, DPI)
        f.load_flags = load_flags(p)
        faces.append(f)
    face = faces[0]
    glyphs, bitmaps, have, missing, offset = [], [], [], [], 0
    for cp in sorted(set(codepoints)):
        src = next(((f, f.get_char_index(cp)) for f in faces if f.get_char_index(cp)), None)
        if src is None:
            missing.append(cp)
            continue
        src[0].load_glyph(src[1], src[0].load_flags)
        g = src[0].glyph
        bm = g.bitmap
        w, h, pitch = bm.width, bm.rows, bm.pitch
        buf = bytes(bm.buffer)
        if pitch != w:
            buf = b"".join(buf[r * pitch : r * pitch + w] for r in range(h))
        packed = _pack2(buf) if w and h else b""
        adv = g.advance.x >> 6
        if w > 255 or h > 255 or adv > 255:
            raise ValueError(f"U+{cp:04X} at {size_pt}pt does not fit the .epdfont glyph ({w}x{h}, advance {adv})")
        glyphs.append(struct.pack("<4B2h2I", w, h, adv, 0, g.bitmap_left, g.bitmap_top, len(packed), offset))
        bitmaps.append(packed)
        have.append(cp)
        offset += len(packed)
    metrics = face.size
    advance_y = -(-metrics.height // 64)
    ascender = -(-metrics.ascender // 64)
    descender = metrics.descender // 64
    # Fill gaps with empty glyphs so the interval table stays short (see _fill_gaps).
    real = dict(zip(have, glyphs))
    empty = struct.pack("<4B2h2I", 0, 0, 0, 0, 0, 0, 0, 0)
    have = _fill_gaps(have)
    glyphs = [real.get(cp, empty) for cp in have]
    intervals = _intervals(have)
    intervals_off = 32
    glyphs_off = intervals_off + 12 * len(intervals)
    bitmap_off = glyphs_off + 16 * len(glyphs)
    header = struct.pack("<IHBBBBBB5I", EPDFONT_MAGIC, 1, 1, 0, advance_y & 0xFF, ascender & 0xFF,
                         descender & 0xFF, 0, len(intervals), len(glyphs), intervals_off, glyphs_off, bitmap_off)
    table, index = bytearray(), 0
    for first, last in intervals:
        table += struct.pack("<3I", first, last, index)
        index += last - first + 1
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(header + bytes(table) + b"".join(glyphs) + b"".join(bitmaps))
    return missing


def cmap(path):
    from fontTools.ttLib import TTFont

    return set(TTFont(path, lazy=True).getBestCmap())


def presentation_forms(path):
    cps = cmap(path)
    a = sum(1 for c in range(ARABIC_PF_A[0], ARABIC_PF_A[1] + 1) if c in cps)
    b = sum(1 for c in range(ARABIC_PF_B[0], ARABIC_PF_B[1] + 1) if c in cps)
    return a, b


def arabic_fonts(languages):
    out = []
    for entry in languages["languages"]:
        if entry["script"] == "arabic":
            for f in [r["font"] for r in entry["reader"]] + [entry["ui"]]:
                if f not in out:
                    out.append(f)
    return out


def main():
    sys.path.insert(0, HERE)
    import epub2cpb

    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=("fetch", "check", "lock"))
    ap.add_argument("fonts", nargs="*")
    ap.add_argument("--cache", default=DEFAULT_CACHE)
    args = ap.parse_args()
    languages = epub2cpb.load_languages()
    ids = args.fonts or fonts_in_languages(languages)
    unknown = [f for f in ids if f not in load_lock()["fonts"]]
    if unknown:
        raise SystemExit(f"not in fonts.lock.json: {unknown}")

    if args.command == "lock":
        lock = load_lock()
        with tempfile.TemporaryDirectory() as t:
            for fid in ids:
                entry = lock["fonts"][fid]
                for item in [entry] + entry.get("license_files", []):
                    if "url" in item:
                        p = _get(dict(item, sha256=None), t, verify=False)
                        item["sha256"] = sha256(p)
                        os.remove(p)
                    elif "path" in item:
                        item["sha256"] = sha256(os.path.join(ROOT, item["path"]))
                print(f"{fid}: {entry['sha256']}")
        save_lock(lock)
        return

    for fid in ids:
        path, licenses = fetch(fid, args.cache)
        if args.command == "fetch":
            print(f"{fid}: {os.path.relpath(path, ROOT)} ({os.path.getsize(path)} bytes), "
                  f"licenses {[os.path.basename(x) for x in licenses]}")
    if args.command == "check":
        arabic = set(arabic_fonts(languages))
        for fid in ids:
            path = instance_path(fid, args.cache)
            a, b = presentation_forms(path)
            note = ""
            if fid in arabic:
                note = "  OK" if a > 100 and b > 100 else "  MISSING presentation forms"
            print(f"{fid:24} {len(cmap(path)):6} code points  PF-A {a:4}  PF-B {b:4}{note}")


if __name__ == "__main__":
    main()
