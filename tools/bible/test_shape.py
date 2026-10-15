#!/usr/bin/env python3
"""Tests for shape.py (HarfBuzz pre-shaping into private-use clusters).

Needs the shaping venv and the fonts:
    build/shaping-venv/bin/python tools/bible/shape.py fetch
    build/shaping-venv/bin/python tools/bible/test_shape.py
Tests that need uharfbuzz/freetype or the fonts are skipped without them.
"""

import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shape  # noqa: E402  (loads freetype before uharfbuzz)

try:
    import uharfbuzz  # noqa: F401
    HAVE_HB = shape.freetype is not None
except ImportError:
    HAVE_HB = False

SERIF = shape.font_cache_path("NotoSerifDevanagari")
HEBREW = shape.font_cache_path("NotoSerifHebrew")
SANS = shape.font_cache_path("NotoSansDevanagari")
HAVE_FONTS = all(os.path.exists(p) for p in (SERIF, HEBREW, SANS))
DATA = os.path.join(shape.REPO, "data", "shaping")

needs_shaping = unittest.skipUnless(HAVE_HB and HAVE_FONTS, "needs uharfbuzz, freetype-py and `shape.py fetch`")


class Verse:
    __slots__ = ("book", "chapter", "verse", "parts", "flags")

    def __init__(self, text):
        self.book, self.chapter, self.verse, self.flags = 1, 1, 1, 0
        self.parts = [text]

    def text(self):
        return " ".join("".join(self.parts).split())


class TableFormatTest(unittest.TestCase):
    def test_line_round_trip(self):
        c = shape.Cluster("र्मी", 0, [(459, 475, 0, 0), (518, 0, -96, 1)])
        d = shape.Cluster.parse(c.line())
        self.assertEqual((d.text, d.key), (c.text, c.key))
        self.assertEqual(c.advance, 475)

    def test_committed_tables_parse(self):
        for name in ("hi", "ml"):
            path = os.path.join(DATA, name + ".txt")
            if not os.path.exists(path):
                continue
            table = shape.ClusterTable(path)
            self.assertGreater(len(table.clusters), 0, name)
            self.assertEqual(len(table.index), len(table.clusters), f"{name}: duplicate clusters")
            with open(path, encoding="utf-8") as f:
                self.assertEqual(sum(1 for _ in f), len(table.clusters), f"{name}: line n must be cluster n")

    def test_pack_2bit_matches_converter_thresholds(self):
        # 4-bit >= 12 -> 3, >= 8 -> 2, >= 4 -> 1, MSB first, padded.
        self.assertEqual(shape.pack_2bit(bytes([0xFF, 0x80, 0x40, 0x00, 0xC0]), 5, 1), bytes([0b11100100, 0b11000000]))


@needs_shaping
class ShapingTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.table = os.path.join(self.tmp, "hi.txt")

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_round_trip(self):
        text = "आदि में, परमेश्वर ने “उजाला हो,” 3:16 धर्मी।"
        (out,) = shape.shape_strings([text], "hi", [SERIF, HEBREW], self.table)
        self.assertNotEqual(out, text)
        for word, shaped in zip(text.split(" "), out.split(" ")):
            for ch in shaped:  # every non-ASCII character became a cluster
                self.assertTrue(0x20 <= ord(ch) <= 0x7E or ord(ch) >= shape.PUA_BASE, (word, hex(ord(ch))))
        self.assertEqual(out.count(" "), text.count(" "))
        self.assertIn("3:16", out)
        self.assertEqual(shape.unshape(out, shape.load_clusters(self.table)), text)

    def test_zero_width_space_dropped_and_hebrew_fallback(self):
        (out,) = shape.shape_strings(["א अलिफ़​।"], "hi", [SERIF, HEBREW], self.table)
        clusters = shape.load_clusters(self.table)
        self.assertEqual(shape.unshape(out, clusters), "א अलिफ़।")
        self.assertEqual(clusters[ord(out[0]) - shape.PUA_BASE].font, 1)

    def test_table_is_append_only(self):
        first = shape.shape_strings(["परमेश्वर ने"], "hi", SERIF, self.table)
        with open(self.table, encoding="utf-8") as f:
            before = f.read()
        shape.shape_strings(["पृथ्वी की सृष्टि"], "hi", SERIF, self.table)
        with open(self.table, encoding="utf-8") as f:
            after = f.read()
        self.assertTrue(after.startswith(before))
        self.assertGreater(len(after), len(before))
        # Same input, same code points, no new lines.
        self.assertEqual(shape.shape_strings(["परमेश्वर ने"], "hi", SERIF, self.table), first)
        with open(self.table, encoding="utf-8") as f:
            self.assertEqual(f.read(), after)

    def test_frozen_refuses_new_clusters(self):
        shape.shape_strings(["परमेश्वर"], "hi", SERIF, self.table)
        shape.shape_strings(["परमेश्वर"], "hi", SERIF, self.table, frozen=True)
        with self.assertRaises(shape.ShapingError):
            shape.shape_strings(["सृष्टि"], "hi", SERIF, self.table, frozen=True)

    def test_other_font_refused(self):
        shape.shape_strings(["परमेश्वर"], "hi", SERIF, self.table)
        with self.assertRaises(shape.ShapingError):
            shape.shape_strings(["परमेश्वर"], "hi", SANS, self.table)

    def test_shape_pack_text(self):
        verses = [Verse("आदि में,  परमेश्वर"), Verse("1 पृथ्वी")]
        headings = [(1, 0, "सृष्टि")]
        v, h, clusters = shape.shape_pack_text(verses, headings, "hi", SERIF, self.table)
        self.assertEqual(len(v), 2)
        self.assertEqual(verses[0].parts, ["आदि में,  परमेश्वर"])  # input untouched
        self.assertEqual(shape.unshape(v[0].text(), clusters), "आदि में, परमेश्वर")
        self.assertEqual(v[1].text()[:2], "1 ")
        self.assertEqual(h[0][:2], (1, 0))
        self.assertEqual(shape.unshape(h[0][2], clusters), "सृष्टि")

    def test_font_file(self):
        (out,) = shape.shape_strings(["क्योंकि परमेश्वर"], "hi", SERIF, self.table)
        clusters = shape.load_clusters(self.table)
        paths = shape.build_cluster_fonts(clusters, SERIF, [14, 28], self.tmp)
        self.assertEqual(set(paths), {14, 28})
        f14, f28 = (shape.read_epdfont(paths[s]) for s in (14, 28))
        for f in (f14, f28):
            self.assertEqual(f["version"], 1)
            self.assertEqual(f["is2bit"], 1)
            self.assertEqual(f["intervals"][0][:2], (0x20, 0x7E))
            self.assertEqual(f["intervals"][1][:2], (shape.PUA_BASE, shape.PUA_BASE + len(clusters) - 1))
            self.assertEqual(f["glyph_count"], 95 + len(clusters))
            self.assertGreater(f["ascender"], 0)
            self.assertLess(f["descender"], 0)
            self.assertGreater(f["advance_y"], f["ascender"])
            for ch in out.replace(" ", ""):
                w, h, adv, _left, _top, data = f["glyphs"][ord(ch)]
                self.assertGreater(adv, 0)
                self.assertEqual(len(data), (w * h + 3) // 4)
        self.assertEqual(f14["glyphs"][0x20][:3], (0, 0, f14["glyphs"][0x20][2]))
        self.assertGreater(f28["glyphs"][ord(out[0])][2], f14["glyphs"][ord(out[0])][2])
        with open(paths[14], "rb") as fh:
            self.assertEqual(fh.read(4), b"EPDF")


if __name__ == "__main__":
    unittest.main()
