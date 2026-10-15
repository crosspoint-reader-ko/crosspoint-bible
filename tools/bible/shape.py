#!/usr/bin/env python3
"""Pre-shape Hindi and Malayalam Bible text into private-use glyphs.

The firmware draws glyphs by code point and has no shaping engine, so Indic
conjuncts and vowel signs would come out wrong. This tool shapes the text on the
host with HarfBuzz and turns each shaped cluster (a syllable with its marks, as
HarfBuzz groups it) into one code point in plane 15: cluster n of the table is
U+F0000+n. `build_cluster_fonts` then rasterises every cluster into one glyph of
an .epdfont, so the device only draws code points left to right.

Text rules (the same for verses, headings and UI strings):
  - Text is split into words on ASCII spaces. Spaces stay ASCII spaces.
  - Printable ASCII (U+0020-U+007E: digits, Latin letters, basic punctuation)
    passes through unchanged. The fonts carry those glyphs as normal code points,
    so verse numbers and stray Latin render without the table.
  - Everything else is shaped. Inside a word, runs are cut at ASCII characters,
    at U+200B (zero width space, dropped) and where the font changes.
  - Fonts are a stack: the language's font first, fallbacks after (Hebrew for
    the Psalm 119 acrostic letters). Each character uses the first font that has
    it. Characters no font has are dropped and reported.

Cluster table `data/shaping/<lang>.txt`: one cluster per line, line n (0-based)
is U+F0000+n. A line is

    <source text> TAB <font index> TAB <gid>,<advance>,<x offset>,<y offset> ...

in font units (unscaled, so the table does not depend on the point size). The
glyph run, not the text, is the key: the same text can shape differently in
another word. New clusters are appended in the order they are first met; lines
are never reordered or removed, so a build with the committed table is
reproducible. `<lang>.fonts.json` next to it fingerprints the font stack the
glyph ids belong to; shaping with a different font is refused.

Setup (uharfbuzz is not in the font converter venv):

    python3 -m venv build/shaping-venv
    build/shaping-venv/bin/pip install uharfbuzz freetype-py fonttools pillow
    build/shaping-venv/bin/python tools/bible/shape.py fetch

usage:
    shape.py fetch [--dir build/fonts-cache]
    shape.py shape --lang hi --font F.ttf [--font FALLBACK.ttf] --in OTB/lang/hi-IN
                   --clusters data/shaping/hi.txt [--frozen] [--names-only]
    shape.py fonts --clusters data/shaping/hi.txt --font F.ttf [--font ...] --out DIR
                   [--sizes 14,16,18,22,28] [--name NAME]
    shape.py check --lang hi --font F.ttf [--font ...] --clusters data/shaping/hi.txt
                   --fonts-dir DIR --size 18 --text "..." --out check.png
"""

# freetype-py must load before uharfbuzz (fontTools imports it too): uharfbuzz's
# wheel carries its own FreeType, and if it loads first freetype-py's calls bind
# to those symbols and crash in FT_Load_Glyph. Import shape.py before uharfbuzz.
try:
    import freetype  # noqa: F401
except ImportError:  # shaping alone does not need it
    freetype = None

import argparse
import contextlib
import copy
import glob
import hashlib
import importlib.util
import io
import json
import math
import os
import re
import struct
import sys
import unicodedata
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

PUA_BASE = 0xF0000
PUA_LIMIT = 0xFFFFE - PUA_BASE  # plane 15 private use ends at U+FFFFD
ASCII_FIRST, ASCII_LAST = 0x20, 0x7E
ZWSP = "​"
JOINERS = {"‌", "‍"}  # ZWNJ/ZWJ steer shaping; they stay in the run
DPI = 150  # same as lib/EpdFont/scripts/fontconvert.py and ttf_to_epdfont.py
DEFAULT_SIZES = (14, 16, 18, 22, 28)
SCRIPTS = {"hi": "Deva", "ml": "Mlym"}  # ISO 15924, for the reference rendering

# Fonts for the shaped languages. Variable fonts from google/fonts pinned to one
# commit, instantiated at wght=400 (wdth=100) by `shape.py fetch`.
GOOGLE_FONTS_COMMIT = "67bc647ce1910e92f461bcc82aae30f21d21a9cc"
FONT_SOURCES = {
    "NotoSerifDevanagari": ("ofl/notoserifdevanagari/NotoSerifDevanagari[wdth,wght].ttf",
                            "1191e07bfeb062d80e252eb85b0eafdfbda1e350707a2a60628668e8f677dbbb"),
    "NotoSansDevanagari": ("ofl/notosansdevanagari/NotoSansDevanagari[wdth,wght].ttf",
                           "14ec4af41f27482216d1c2229f417ff9b1425e1babb014e57d1d40d03229853e"),
    "NotoSerifMalayalam": ("ofl/notoserifmalayalam/NotoSerifMalayalam[wght].ttf",
                           "eed354cadb35992df6e8b32fdb7edb399989b165f0f25e7a921009683db26e8e"),
    "NotoSansMalayalam": ("ofl/notosansmalayalam/NotoSansMalayalam[wdth,wght].ttf",
                          "312e0e7c3cc15fa09eb42a8f749eeb246b593ed420e3c81aafe8d910c3a6fb56"),
    "NotoSerifHebrew": ("ofl/notoserifhebrew/NotoSerifHebrew[wdth,wght].ttf",
                        "93caef921360788dc3b0e32136bb26f16bc57717ec482d48fc7fd43820617165"),
    "NotoSansHebrew": ("ofl/notosanshebrew/NotoSansHebrew[wdth,wght].ttf", None),
}
# Font stacks per language and role. The order is part of the cluster table's
# meaning (the font index in each line): append fallbacks, never reorder.
FONT_STACKS = {
    "hi": {"reader": ["NotoSerifDevanagari", "NotoSerifHebrew"], "ui": ["NotoSansDevanagari"]},
    "ml": {"reader": ["NotoSerifMalayalam", "NotoSerifHebrew"], "ui": ["NotoSansMalayalam"]},
}


class ShapingError(Exception):
    pass


def font_cache_path(name, cache_dir=None):
    return os.path.join(cache_dir or os.path.join(REPO, "build", "fonts-cache"), f"{name}-Regular.ttf")


def default_font_stack(lang, role="reader", cache_dir=None):
    """Paths of the static fonts for a language (run `shape.py fetch` first)."""
    return [font_cache_path(n, cache_dir) for n in FONT_STACKS[lang][role]]


# --------------------------------------------------------------------------
# Cluster table


class Cluster:
    """One shaped cluster: glyphs = ((gid, advance, x_offset, y_offset), ...) in font units."""

    __slots__ = ("text", "font", "glyphs")

    def __init__(self, text, font, glyphs):
        self.text, self.font, self.glyphs = text, font, tuple(glyphs)

    @property
    def key(self):
        return (self.font, self.glyphs)

    @property
    def advance(self):
        return sum(g[1] for g in self.glyphs)

    def line(self):
        text = self.text.replace("\t", " ").replace("\n", " ")
        return f"{text}\t{self.font}\t" + " ".join(",".join(str(v) for v in g) for g in self.glyphs)

    @classmethod
    def parse(cls, line):
        text, font, glyphs = line.rstrip("\n").split("\t")
        return cls(text, int(font), (tuple(int(v) for v in g.split(",")) for g in glyphs.split(" ")))

    def __repr__(self):
        return f"Cluster({self.text!r}, {self.font}, {self.glyphs})"


def font_fingerprint(path):
    from fontTools.ttLib import TTFont

    with TTFont(path, lazy=True) as f:
        names = f["name"]
        glyph_order = f.getGlyphOrder()
        return {
            "file": os.path.basename(path),
            "family": str(names.getDebugName(4) or ""),
            "version": str(names.getDebugName(5) or ""),
            "glyphs": len(glyph_order),
            "glyph_order_sha256": hashlib.sha256(",".join(glyph_order).encode()).hexdigest(),
        }


def _meta_path(cluster_file):
    root, _ = os.path.splitext(cluster_file)
    return root + ".fonts.json"


class ClusterTable:
    """Append-only cluster table backed by a text file."""

    def __init__(self, path, font_paths=None):
        self.path = path
        self.clusters = []
        self.index = {}
        self.saved = 0
        if os.path.exists(path):
            with open(path, encoding="utf-8") as f:
                for line in f:
                    if line.strip("\n"):
                        self._add(Cluster.parse(line))
        self.saved = len(self.clusters)
        self.fonts = None
        if font_paths is not None:
            self._check_fonts(font_paths)

    def _add(self, c):
        self.index.setdefault(c.key, len(self.clusters))
        self.clusters.append(c)

    def _check_fonts(self, font_paths):
        fps = [font_fingerprint(p) for p in font_paths]
        meta = _meta_path(self.path)
        old = None
        if os.path.exists(meta):
            with open(meta, encoding="utf-8") as f:
                old = json.load(f)["fonts"]
        if old is not None and self.clusters:
            strip = lambda fp: {k: v for k, v in fp.items() if k != "file"}  # noqa: E731
            used = max(c.font for c in self.clusters) + 1
            if len(fps) < used or [strip(a) for a in old[:used]] != [strip(b) for b in fps[:used]]:
                raise ShapingError(
                    f"{self.path}: font stack differs from {meta} (glyph ids would not match). "
                    "Use the recorded fonts, or start a new table."
                )
        self.fonts = fps

    def lookup(self, cluster):
        """Index of the cluster, appending it if new."""
        i = self.index.get(cluster.key)
        if i is None:
            i = len(self.clusters)
            if i >= PUA_LIMIT:
                raise ShapingError("cluster table is full (plane 15 private use)")
            self._add(cluster)
        return i

    @property
    def new(self):
        return self.clusters[self.saved:]

    def save(self):
        os.makedirs(os.path.dirname(os.path.abspath(self.path)), exist_ok=True)
        if self.new:
            with open(self.path, "a", encoding="utf-8") as f:
                for c in self.new:
                    f.write(c.line() + "\n")
            self.saved = len(self.clusters)
        if self.fonts is not None:
            meta = _meta_path(self.path)
            data = {"fonts": self.fonts, "clusters": len(self.clusters)}
            text = json.dumps(data, ensure_ascii=False, indent=1) + "\n"
            old = None
            if os.path.exists(meta):
                with open(meta, encoding="utf-8") as f:
                    old = f.read()
            if text != old:
                with open(meta, "w", encoding="utf-8") as f:
                    f.write(text)


def load_clusters(cluster_file):
    return ClusterTable(cluster_file).clusters


# --------------------------------------------------------------------------
# Shaping


def _as_list(font_path):
    return [font_path] if isinstance(font_path, (str, os.PathLike)) else list(font_path)


class Shaper:
    def __init__(self, lang, font_path, cluster_file, frozen=False):
        import uharfbuzz as hb
        from fontTools.ttLib import TTFont

        self.hb = hb
        self.lang = lang
        self.paths = _as_list(font_path)
        self.table = ClusterTable(cluster_file, self.paths)
        self.frozen = frozen
        self.fonts = [hb.Font(hb.Face(hb.Blob.from_file_path(p))) for p in self.paths]
        self.cmaps = []
        for p in self.paths:
            with TTFont(p, lazy=True) as f:
                self.cmaps.append(set(f.getBestCmap()))
        self.cache = {}
        self.dropped = {}

    def _font_for(self, ch, current):
        if ch in JOINERS and current is not None:
            return current
        cp = ord(ch)
        if current is not None and cp in self.cmaps[current]:
            return current
        for i, cmap in enumerate(self.cmaps):
            if cp in cmap:
                return i
        # Default ignorables (variation selectors, other joiners) ride along.
        if current is not None and unicodedata.category(ch) in ("Mn", "Cf"):
            return current
        return None

    def _runs(self, word):
        """Yield (font index or None for ASCII pass-through, text) runs of a word."""
        run, font = [], None
        for ch in word:
            cp = ord(ch)
            if ASCII_FIRST <= cp <= ASCII_LAST or ch == ZWSP:
                if run:
                    yield font, "".join(run)
                    run, font = [], None
                if ch != ZWSP:
                    yield None, ch
                continue
            f = self._font_for(ch, font if run else None)
            if f is None:
                self.dropped[ch] = self.dropped.get(ch, 0) + 1
                continue
            if run and f != font:
                yield font, "".join(run)
                run = []
            font = f
            run.append(ch)
        if run:
            yield font, "".join(run)

    def _shape_run(self, font, text):
        hb = self.hb
        buf = hb.Buffer()
        buf.add_str(text)
        buf.guess_segment_properties()
        if font == 0:
            buf.language = self.lang
        hb.shape(self.fonts[font], buf)
        infos, poss = buf.glyph_infos, buf.glyph_positions
        # Cluster values index into the run (UTF-32 code points for add_str).
        starts = sorted({i.cluster for i in infos})
        bounds = {s: (starts[k + 1] if k + 1 < len(starts) else len(text)) for k, s in enumerate(starts)}
        out, cur, glyphs = [], None, []
        for info, pos in zip(infos, poss):
            if info.cluster != cur and glyphs:
                out.append(Cluster(text[cur:bounds[cur]], font, glyphs))
                glyphs = []
            cur = info.cluster
            glyphs.append((info.codepoint, pos.x_advance, pos.x_offset, pos.y_offset))
        if glyphs:
            out.append(Cluster(text[cur:bounds[cur]], font, glyphs))
        return out

    def shape_word(self, word):
        got = self.cache.get(word)
        if got is not None:
            return got
        parts = []
        for font, run in self._runs(word):
            if font is None:
                parts.append(run)
                continue
            for c in self._shape_run(font, run):
                before = len(self.table.clusters)
                i = self.table.lookup(c)
                if self.frozen and len(self.table.clusters) != before:
                    raise ShapingError(f"new cluster {c.text!r} in {word!r} but the table is frozen")
                parts.append(chr(PUA_BASE + i))
        got = "".join(parts)
        self.cache[word] = got
        return got

    def shape(self, text):
        return " ".join(self.shape_word(w) for w in text.split(" "))

    def report_dropped(self, out=sys.stderr):
        if self.dropped:
            items = ", ".join(f"U+{ord(c):04X} {c} x{n}" for c, n in sorted(self.dropped.items()))
            print(f"shape.py: dropped {sum(self.dropped.values())} characters no font has: {items}", file=out)

    def save(self):
        self.table.save()


def shape_pack_text(verses, headings, lang, font_path, cluster_file, frozen=False):
    """Rewrite verse and heading texts into cluster code points.

    verses: objects like epub2cpb.Verse (.parts, .text()); each is copied with
    parts = [shaped text]. headings: (verse_index, kind, text) tuples.
    font_path: one path or the font stack (primary first). The cluster table is
    loaded from cluster_file, extended and written back (append only); frozen=True
    raises ShapingError instead of adding clusters.
    Returns (verses, headings, cluster_list) where cluster_list is the whole
    table (cluster i = U+F0000+i), ready for build_cluster_fonts.
    """
    sh = Shaper(lang, font_path, cluster_file, frozen)
    out_v = []
    for v in verses:
        nv = copy.copy(v)
        nv.parts = [sh.shape(v.text())]
        out_v.append(nv)
    out_h = [(vi, kind, sh.shape(text)) for vi, kind, text in headings]
    sh.report_dropped()
    sh.save()
    return out_v, out_h, list(sh.table.clusters)


def shape_strings(strings, lang, font_path, cluster_file, frozen=False):
    """Shape plain strings (UI labels, book names) with the same rules."""
    sh = Shaper(lang, font_path, cluster_file, frozen)
    out = [sh.shape(re.sub(r"\s+", " ", s).strip()) for s in strings]
    sh.report_dropped()
    sh.save()
    return out


def unshape(text, clusters):
    """Map cluster code points back to their source text (for checks and search)."""
    return "".join(
        clusters[ord(ch) - PUA_BASE].text if 0 <= ord(ch) - PUA_BASE < len(clusters) else ch for ch in text
    )


# --------------------------------------------------------------------------
# Fonts


def _epdfont_writer():
    path = os.path.join(REPO, "lib", "EpdFont", "scripts", "ttf_to_epdfont.py")
    spec = importlib.util.spec_from_file_location("ttf_to_epdfont", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def pack_2bit(pixels, width, height):
    """8-bit coverage (row major) -> packed 2-bit, as ttf_to_epdfont.py --2bit does."""
    out, px, n = bytearray(), 0, width * height
    for i in range(n):
        g = pixels[i] >> 4
        px = (px << 2) | (3 if g >= 12 else 2 if g >= 8 else 1 if g >= 4 else 0)
        if i % 4 == 3:
            out.append(px)
            px = 0
    if n % 4:
        out.append(px << (4 - n % 4) * 2)
    return bytes(out)


def _bitmap_pixels(bitmap):
    w, rows, pitch = bitmap.width, bitmap.rows, bitmap.pitch
    buf = bitmap.buffer
    if pitch == w:
        return bytes(buf)
    return b"".join(bytes(buf[r * pitch:r * pitch + w]) for r in range(rows))


class ClusterRasterizer:
    """Renders clusters at one point size with FreeType (150 DPI, like the converter)."""

    def __init__(self, font_paths, size):
        import freetype

        self.ft = freetype
        self.size = size
        self.faces = []
        for p in _as_list(font_paths):
            face = freetype.Face(p)
            face.set_char_size(size << 6, size << 6, DPI, DPI)
            self.faces.append(face)
        self.flags = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT

    def units_to_px(self, font, units, axis="x"):
        m = self.faces[font].size
        scale = m.x_scale if axis == "x" else m.y_scale
        return units * scale / 65536.0 / 64.0

    def advance_px(self, cluster):
        return int(round(self.units_to_px(cluster.font, cluster.advance)))

    def render(self, cluster):
        """-> (width, height, left, top, advance, 8-bit pixels)."""
        ft = self.ft
        face = self.faces[cluster.font]
        pieces = []
        pen = 0.0
        for gid, adv, dx, dy in cluster.glyphs:
            x = pen + self.units_to_px(cluster.font, dx)
            y = self.units_to_px(cluster.font, dy, "y")
            ix, iy = math.floor(x), math.floor(y)
            delta = ft.FT_Vector(int(round((x - ix) * 64)), int(round((y - iy) * 64)))
            face.set_transform(ft.FT_Matrix(0x10000, 0, 0, 0x10000), delta)
            face.load_glyph(gid, self.flags)
            bm = face.glyph.bitmap
            if bm.width and bm.rows:
                pieces.append((face.glyph.bitmap_left + ix, face.glyph.bitmap_top + iy, bm.width, bm.rows,
                               _bitmap_pixels(bm)))
            pen += self.units_to_px(cluster.font, adv)
        face.set_transform(ft.FT_Matrix(0x10000, 0, 0, 0x10000), ft.FT_Vector(0, 0))
        advance = self.advance_px(cluster)
        if not pieces:
            return 0, 0, 0, 0, advance, b""
        left = min(p[0] for p in pieces)
        right = max(p[0] + p[2] for p in pieces)
        top = max(p[1] for p in pieces)
        bottom = min(p[1] - p[3] for p in pieces)
        w, h = right - left, top - bottom
        canvas = bytearray(w * h)
        for gx, gy, gw, gh, data in pieces:
            ox, oy = gx - left, top - gy
            for r in range(gh):
                base = (oy + r) * w + ox
                row = data[r * gw:(r + 1) * gw]
                for c, v in enumerate(row):
                    if v > canvas[base + c]:
                        canvas[base + c] = v
        return w, h, left, top, advance, bytes(canvas)


def build_cluster_fonts(cluster_list, font_path, sizes_pt=DEFAULT_SIZES, out_dir=".", name=""):
    """Write one 2-bit .epdfont per size: printable ASCII as normal code points,
    cluster i as U+F0000+i. Files are out_dir/<pt>.epdfont (<name>_<pt>.epdfont
    when name is given). Returns {pt: path}."""
    writer = _epdfont_writer()
    GlyphProps = writer.GlyphProps
    os.makedirs(out_dir, exist_ok=True)
    written = {}
    for size in sizes_pt:
        rast = ClusterRasterizer(font_path, size)
        primary = rast.faces[0]
        glyphs, offset = [], 0

        def add(cp, w, h, adv, left, top, packed):
            nonlocal offset
            for field, v, lo, hi in (("width", w, 0, 255), ("height", h, 0, 255), ("advance", adv, 0, 255),
                                     ("left", left, -32768, 32767), ("top", top, -32768, 32767)):
                if not lo <= v <= hi:
                    raise ShapingError(f"U+{cp:04X} at {size}pt: {field} {v} does not fit the .epdfont glyph")
            glyphs.append((GlyphProps(w, h, adv, left, top, len(packed), offset, cp), packed))
            offset += len(packed)

        # ASCII exactly as ttf_to_epdfont.py renders it (hinted, floored advance).
        for cp in range(ASCII_FIRST, ASCII_LAST + 1):
            primary.load_char(cp, rast.ft.FT_LOAD_RENDER)
            bm = primary.glyph.bitmap
            px = _bitmap_pixels(bm)
            add(cp, bm.width, bm.rows, writer.norm_floor(primary.glyph.advance.x), primary.glyph.bitmap_left,
                primary.glyph.bitmap_top, pack_2bit(px, bm.width, bm.rows))
        for i, c in enumerate(cluster_list):
            w, h, left, top, adv, px = rast.render(c)
            add(PUA_BASE + i, w, h, adv, left, top, pack_2bit(px, w, h))

        primary.load_char(ord("|"), rast.ft.FT_LOAD_RENDER)
        advance_y = writer.norm_ceil(primary.size.height)
        ascender = writer.norm_ceil(primary.size.ascender)
        descender = writer.norm_floor(primary.size.descender)
        intervals = [(ASCII_FIRST, ASCII_LAST)]
        if cluster_list:
            intervals.append((PUA_BASE, PUA_BASE + len(cluster_list) - 1))
        path = os.path.join(out_dir, f"{name}_{size}.epdfont" if name else f"{size}.epdfont")
        with contextlib.redirect_stdout(io.StringIO()):
            writer.write_epdfont(path, intervals, glyphs, advance_y, ascender, descender, True)
        written[size] = path
    return written


# --------------------------------------------------------------------------
# Reading .epdfont files back (tests and the visual check)

HEADER = struct.Struct("<IHBBBbbB5I")
GLYPH = struct.Struct("<4B2h2I")


def read_epdfont(path):
    with open(path, "rb") as f:
        data = f.read()
    (magic, version, is2bit, _r1, advance_y, ascender, descender, _r2, n_int, n_gly, int_off, gly_off,
     bmp_off) = HEADER.unpack_from(data, 0)
    if magic != 0x46445045:
        raise ValueError(f"{path}: not an .epdfont")
    intervals = [struct.unpack_from("<3I", data, int_off + 12 * i) for i in range(n_int)]
    glyphs = {}
    for first, last, off in intervals:
        for cp in range(first, last + 1):
            w, h, adv, _res, left, top, length, doff = GLYPH.unpack_from(data, gly_off + 16 * (off + cp - first))
            glyphs[cp] = (w, h, adv, left, top, data[bmp_off + doff:bmp_off + doff + length])
    return {"version": version, "is2bit": is2bit, "advance_y": advance_y, "ascender": ascender,
            "descender": descender, "intervals": intervals, "glyphs": glyphs, "size": len(data),
            "glyph_count": n_gly}


def render_epdfont_line(font, text, margin=8):
    """Lay out code points left to right with an .epdfont -> PIL image."""
    from PIL import Image

    width = margin * 2 + sum(font["glyphs"].get(ord(ch), (0, 0, 0))[2] for ch in text)
    height = font["advance_y"] + margin * 2
    img = Image.new("L", (max(width, 1), height), 255)
    pix = img.load()
    x, base = margin, margin + font["ascender"]
    shades = (255, 170, 85, 0)
    for ch in text:
        g = font["glyphs"].get(ord(ch))
        if g is None:
            continue
        w, h, adv, left, top, data = g
        for i in range(w * h):
            v = (data[i // 4] >> (6 - 2 * (i % 4))) & 3
            if v:
                px, py = x + left + i % w, base - top + i // w
                if 0 <= px < img.width and 0 <= py < img.height:
                    pix[px, py] = min(pix[px, py], shades[v])
        x += adv
    return img


def render_reference_line(font_paths, size, text, lang, margin=8):
    """HarfBuzz + FreeType rendering of the original text (whole line, fractional pen)."""
    import freetype
    import uharfbuzz as hb
    from PIL import Image

    paths = _as_list(font_paths)
    face = freetype.Face(paths[0])
    face.set_char_size(size << 6, size << 6, DPI, DPI)
    hbf = hb.Font(hb.Face(hb.Blob.from_file_path(paths[0])))
    buf = hb.Buffer()
    buf.add_str(text)
    buf.guess_segment_properties()
    buf.direction = "ltr"  # a leading Hebrew letter must not flip the whole line
    buf.script = SCRIPTS.get(lang, buf.script)
    buf.language = lang
    hb.shape(hbf, buf)
    s = face.size.x_scale / 65536.0 / 64.0
    total = sum(p.x_advance for p in buf.glyph_positions) * s
    height = (face.size.height >> 6) + margin * 2 + 2
    img = Image.new("L", (int(total) + margin * 2 + 2, height), 255)
    base = margin + (face.size.ascender + 63) // 64
    pen = float(margin)
    for info, pos in zip(buf.glyph_infos, buf.glyph_positions):
        x, y = pen + pos.x_offset * s, pos.y_offset * s
        ix, iy = math.floor(x), math.floor(y)
        face.set_transform(freetype.FT_Matrix(0x10000, 0, 0, 0x10000),
                           freetype.FT_Vector(int(round((x - ix) * 64)), int(round((y - iy) * 64))))
        face.load_glyph(info.codepoint, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT)
        bm = face.glyph.bitmap
        data = _bitmap_pixels(bm)
        gx, gy = ix + face.glyph.bitmap_left, base - (iy + face.glyph.bitmap_top)
        for i, v in enumerate(data):
            px, py = gx + i % bm.width, gy + i // bm.width
            if v and 0 <= px < img.width and 0 <= py < img.height:
                img.putpixel((px, py), min(img.getpixel((px, py)), 255 - v))
        pen += pos.x_advance * s
    return img


# --------------------------------------------------------------------------
# CLI


def read_otb(lang_dir, names_only=False):
    """Texts of an Open Translation Bible language folder, in canon order:
    book names and the lang.json NAME/TITLE, then (unless names_only) every
    verse and separator text."""
    texts = []
    books = os.path.join(lang_dir, "books.json")
    if os.path.exists(books):
        with open(books, encoding="utf-8") as f:
            texts.extend(json.load(f).keys())
    lang_json = os.path.join(lang_dir, "lang.json")
    if os.path.exists(lang_json):
        with open(lang_json, encoding="utf-8") as f:
            d = json.load(f)
        texts.extend(d[k] for k in ("NAME", "TITLE") if isinstance(d.get(k), str))
    if names_only:
        return texts

    def order(path):
        book = os.path.basename(os.path.dirname(os.path.dirname(path)))
        num = re.search(r"-(\d+)\.json$", path)
        return (int(book.split(".")[0]), int(num.group(1)) if num else 0, path)

    for path in sorted(glob.glob(os.path.join(lang_dir, "*", "json", "*.json")), key=order):
        with open(path, encoding="utf-8") as f:
            for entry in json.load(f)["verses"]:
                text = "".join(t for t in entry.get("text", []) if isinstance(t, str))
                text = re.sub(r"\s+", " ", text.replace(" ", " ")).strip()
                if text:
                    texts.append(text)
    return texts


def cmd_fetch(args):
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer

    os.makedirs(os.path.join(args.dir, "src"), exist_ok=True)
    names = sorted({n for stacks in FONT_STACKS.values() for s in stacks.values() for n in s})
    for name in names:
        rel, sha = FONT_SOURCES[name]
        src = os.path.join(args.dir, "src", os.path.basename(rel))
        url = f"https://raw.githubusercontent.com/google/fonts/{GOOGLE_FONTS_COMMIT}/" + \
            rel.replace("[", "%5B").replace("]", "%5D")
        if not os.path.exists(src):
            urllib.request.urlretrieve(url, src)
        with open(src, "rb") as fh:
            got = hashlib.sha256(fh.read()).hexdigest()
        if sha and got != sha:
            raise ShapingError(f"{src}: sha256 {got}, expected {sha}")
        out = font_cache_path(name, args.dir)
        if not os.path.exists(out):
            f = TTFont(src, recalcTimestamp=False)
            loc = {a.axisTag: (400 if a.axisTag == "wght" else 100) for a in f["fvar"].axes
                   if a.axisTag in ("wght", "wdth")}
            instancer.instantiateVariableFont(f, loc, inplace=True, updateFontNames=False)
            f.save(out)
        print(f"{out}  {url}  sha256 {got}")


def cmd_shape(args):
    texts = read_otb(args.input, args.names_only)
    sh = Shaper(args.lang, args.font, args.clusters, args.frozen)
    before = len(sh.table.clusters)
    words = 0
    for t in texts:
        sh.shape(t)
        words += t.count(" ") + 1
    sh.report_dropped()
    sh.save()
    print(f"{args.lang}: {len(texts)} texts, {words} words, {len(sh.cache)} distinct words, "
          f"{len(sh.table.clusters)} clusters ({len(sh.table.clusters) - before} new) -> {args.clusters}")


def cmd_fonts(args):
    clusters = load_clusters(args.clusters)
    ClusterTable(args.clusters, args.font)  # refuse a font stack the table was not made with
    sizes = [int(s) for s in args.sizes.split(",")]
    for size, path in build_cluster_fonts(clusters, args.font, sizes, args.out, args.name).items():
        print(f"{size}pt: {path} {os.path.getsize(path)} bytes, {len(clusters)} clusters")


def cmd_check(args):
    from PIL import Image, ImageDraw

    shaped = shape_strings([args.text], args.lang, args.font, args.clusters, frozen=args.frozen)[0]
    path = os.path.join(args.fonts_dir, f"{args.name}_{args.size}.epdfont" if args.name else f"{args.size}.epdfont")
    mine = render_epdfont_line(read_epdfont(path), shaped)
    ref = render_reference_line(args.font, args.size, args.text, args.lang)
    gap = 12
    img = Image.new("L", (max(mine.width, ref.width) + 120, mine.height + ref.height + gap * 3), 255)
    d = ImageDraw.Draw(img)
    d.text((4, gap), "HarfBuzz", fill=0)
    img.paste(ref, (110, gap))
    d.text((4, ref.height + gap * 2), ".epdfont", fill=0)
    img.paste(mine, (110, ref.height + gap * 2))
    img.save(args.out)
    print(f"{args.out}: {len(shaped)} code points, epdfont width {mine.width}, reference width {ref.width}")


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)

    f = sub.add_parser("fetch", help="download and instantiate the Noto fonts")
    f.add_argument("--dir", default=os.path.join(REPO, "build", "fonts-cache"))
    f.set_defaults(func=cmd_fetch)

    s = sub.add_parser("shape", help="shape an Open Translation Bible language folder into the cluster table")
    s.add_argument("--lang", required=True)
    s.add_argument("--font", action="append", required=True, help="font stack, primary first (repeat)")
    s.add_argument("--in", dest="input", required=True)
    s.add_argument("--clusters", required=True)
    s.add_argument("--frozen", action="store_true", help="fail instead of adding clusters")
    s.add_argument("--names-only", action="store_true", help="only book names and the Bible's name (UI tables)")
    s.set_defaults(func=cmd_shape)

    g = sub.add_parser("fonts", help="rasterise the cluster table into .epdfont files")
    g.add_argument("--clusters", required=True)
    g.add_argument("--font", action="append", required=True)
    g.add_argument("--out", required=True)
    g.add_argument("--sizes", default=",".join(str(s) for s in DEFAULT_SIZES))
    g.add_argument("--name", default="")
    g.set_defaults(func=cmd_fonts)

    c = sub.add_parser("check", help="render a line from the .epdfont next to HarfBuzz's rendering")
    c.add_argument("--lang", required=True)
    c.add_argument("--font", action="append", required=True)
    c.add_argument("--clusters", required=True)
    c.add_argument("--fonts-dir", required=True)
    c.add_argument("--name", default="")
    c.add_argument("--size", type=int, default=18)
    c.add_argument("--text", required=True)
    c.add_argument("--out", required=True)
    c.add_argument("--frozen", action="store_true", default=True)
    c.set_defaults(func=cmd_check)

    args = p.parse_args(argv)
    try:
        args.func(args)
    except ShapingError as e:
        sys.exit(f"shape.py: {e}")


if __name__ == "__main__":
    main()
