#!/usr/bin/env python3
"""Tests for epub2cpb.py on a synthetic EPUB (no copyrighted text).

usage: python3 tools/bible/test_epub2cpb.py [--write-fixture test/bible_pack/fixture.cpb]
"""

import argparse
import io
import os
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import epub2cpb  # noqa: E402

HEAD = '<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml"><body>'
TAIL = "</body></html>"


def book_html(bk):
    """Book bk: chapter 1 has 3 verses, chapter 2 has 2. Book 19 gets psalm markup."""
    tag = f'<span class="book-name"><a href="x">B{bk}</a></span> '
    out = [HEAD]
    if bk == 19:
        out.append('<p class="psalm-book">제 일 권</p><p class="psalm-title">[다윗의 시]<br/></p>')
    out.append(f'<p class="heading" id="v{bk:02d}001001">소제목 {bk} <span class="smaller">(마 1:1)</span></p>')
    out.append(
        f'<p class="no-indent">{tag}<span class="chapter-num">1</span> 첫째&nbsp;절 '
        f'<span class="verse-num" id="v{bk:02d}001002">2</span>둘째 절 '
        f'<span class="footnote"><a href="f">[1]</a></span> 끝</p>'
    )
    out.append(f'<p class="line"><span class="verse-num">3</span>셋째 ^절</p>')
    out.append(f'<p class="no-indent">{tag}<span class="chapter-num">2</span> 이장 일절 ')
    out.append(f'<span class="verse-num">2</span>이장 이절</p>')
    out.append(TAIL)
    return "".join(out)


def make_epub():
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("mimetype", "application/epub+zip")
        for bk in range(1, 67):
            z.writestr(f"OEBPS/Text/b{bk:02d}.00.Book{bk}.main.html", f'{HEAD}<h1 id="m" title="책{bk}">x</h1>{TAIL}')
            z.writestr(f"OEBPS/Text/b{bk:02d}.00.Book{bk}.text.html", book_html(bk))
    return buf.getvalue()


def convert(tmp):
    epub = os.path.join(tmp, "t.epub")
    with open(epub, "wb") as f:
        f.write(make_epub())
    verses, headings, names = epub2cpb.parse_epub(epub)
    pack, chapters = epub2cpb.build_pack(verses, headings, names, "테스트", "ko",
                                         {"edition": "합성 본문", "license": "Public Domain"})
    return verses, headings, pack, chapters


class Epub2CpbTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.verses, self.headings, self.pack, self.chapters = convert(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_counts(self):
        self.assertEqual(len(self.verses), 66 * 5)
        self.assertEqual(len(self.chapters), 66 * 2)

    def test_text_cleanup(self):
        v = self.verses
        self.assertEqual(v[0].text(), "첫째 절")
        self.assertEqual(v[1].text(), "둘째 절 끝")  # footnote marker dropped
        self.assertEqual(v[2].text(), "셋째 절")  # stray ^ removed
        self.assertEqual((v[3].chapter, v[3].verse, v[3].text()), (2, 1, "이장 일절"))

    def test_flags(self):
        v = self.verses
        self.assertTrue(v[0].flags & epub2cpb.VF_PARAGRAPH)
        self.assertFalse(v[1].flags & epub2cpb.VF_PARAGRAPH)
        self.assertTrue(v[2].flags & epub2cpb.VF_POETRY)

    def test_headings(self):
        first = [h for h in self.headings if h[0] == 0]
        self.assertEqual(first, [(0, epub2cpb.HK_SECTION, "소제목 1 (마 1:1)")])
        ps = [(k, t) for i, k, t in self.headings if self.verses[i].book == 19]
        self.assertEqual(ps[:2], [(epub2cpb.HK_PSALM_BOOK, "제 일 권"), (epub2cpb.HK_PSALM_TITLE, "[다윗의 시]")])

    def test_header(self):
        magic, ver, nb, nc, nv = struct.unpack_from("<8sHHII", self.pack)
        self.assertEqual((magic, ver, nb, nc, nv), (epub2cpb.MAGIC, 2, 66, 132, 330))
        self.assertEqual(self.pack[56:58], b"ko")

    def test_chapter_blocks_round_trip(self):
        import zlib
        p = self.pack
        chapters_off, verses_off = struct.unpack_from("<II", p, 28)
        blocks_off = struct.unpack_from("<I", p, 48)[0]
        # Book 1, chapter 1: verses 0..2.
        first, count, _, comp_off, comp_size, raw_size, verses_end = struct.unpack_from("<IHHIIII", p, chapters_off)
        raw = zlib.decompress(p[blocks_off + comp_off : blocks_off + comp_off + comp_size], -15)
        self.assertEqual(len(raw), raw_size)
        starts = [struct.unpack_from("<H", p, verses_off + 2 * i)[0] for i in range(count)] + [verses_end]
        texts = [raw[starts[i] : starts[i + 1]].decode() for i in range(count)]
        self.assertEqual(texts, ["첫째 절", "둘째 절 끝", "셋째 절"])
        self.assertTrue(raw[verses_end:].decode().startswith("소제목 1"))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--write-fixture")
    args, rest = ap.parse_known_args()
    if args.write_fixture:
        with tempfile.TemporaryDirectory() as t:
            pack = convert(t)[2]
        with open(args.write_fixture, "wb") as f:
            f.write(pack)
        print(f"wrote {args.write_fixture} ({len(pack)} bytes)")
    else:
        unittest.main(argv=[sys.argv[0]] + rest)


class GetBibleJsonTest(unittest.TestCase):
    def test_fills_missing_verses(self):
        import json
        books = []
        for bk in range(1, 67):
            verses = [{"chapter": 1, "verse": n, "text": f"절{n} "} for n in (1, 2, 4)]
            books.append({"nr": bk, "name": f"책{bk}", "chapters": [{"chapter": 1, "verses": verses}]})
        with tempfile.TemporaryDirectory() as t:
            path = os.path.join(t, "b.json")
            with open(path, "w", encoding="utf-8") as f:
                json.dump({"description": "테스트판", "distribution_license": "Public Domain", "books": books}, f)
            verses, headings, names, meta = epub2cpb.parse_getbible(path)
        first = [(v.verse, v.text()) for v in verses[:4]]
        self.assertEqual(first, [(1, "절1"), (2, "절2"), (3, ""), (4, "절4")])
        self.assertEqual(meta["edition"], "테스트판")
        self.assertEqual(names[1], "책1")
