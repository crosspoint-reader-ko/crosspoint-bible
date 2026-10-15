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

    def test_convert_adds_language_keys(self):
        import json
        books = [{"nr": bk, "name": f"책{bk}", "chapters": [{"chapter": 1, "verses": [{"verse": 1, "text": "절"}]}]}
                 for bk in range(1, 67)]
        with tempfile.TemporaryDirectory() as t:
            path = os.path.join(t, "b.json")
            with open(path, "w", encoding="utf-8") as f:
                json.dump({"description": "테스트판", "books": books}, f)
            verses, headings, names, abbrs, title, meta = epub2cpb.convert(path, "ko", "테스트")
        self.assertEqual((meta["language_name"], meta["direction"], meta["wrap"]), ("한국어", "ltr", "char"))
        self.assertNotIn("hyphenation", meta)
        pack, _ = epub2cpb.build_pack(verses, headings, names, title, "ko", meta, abbrs)
        back = epub2cpb.read_pack(pack)
        self.assertEqual(list(back["meta"])[:3], ["language_name", "direction", "wrap"])
        self.assertEqual(back["meta"]["edition"], "테스트판")
        self.assertEqual(back["abbrs"][1], "창")


def make_otb(root):
    """A tiny OTB checkout: lang/xx-XX with 66 books of one chapter (Psalms: two)."""
    import json
    lang = os.path.join(root, "lang", "en-GB")
    os.makedirs(lang)
    with open(os.path.join(lang, "lang.json"), "w", encoding="utf-8") as f:
        json.dump({"NAME": "Test Bible", "READ": "Read"}, f)
    for bk in range(1, 67):
        name = "Psalms" if bk == 19 else f"Book {bk}"
        jdir = os.path.join(lang, f"{bk:02d}.{name}", "json")
        os.makedirs(jdir)
        chapters = 2 if bk == 19 else 1
        for ch in range(1, chapters + 1):
            if bk == 19 and ch == 2:
                verses = [
                    {"text": ["A Psalm of David."]},
                    {"verse": 1, "text": ["> Line one,", "> line two."]},
                    {"verse": 2, "text": ["> Streams.", "---", "[Psalm 2 continues; would you like ", " more?]"]},
                    {"text": ["---"]},
                    {"verse": 3, "text": ["Prose ", "> \\Selah", "> "]},
                ]
            else:
                verses = [
                    {"verse": 1, "text": ["In the beginning", "there was text."]},
                    {"verse": 2, "text": ["Second."]},
                    {"text": ["---"]},
                    {"verse": 3, "text": ["Third", "---", "goes on."]},
                    {"text": ["Some manuscripts add a note here"]},
                    {"verse": 4, "text": ["Fourth."]},
                ]
            with open(os.path.join(jdir, f"{name.lower()}-{ch:02d}.json"), "w", encoding="utf-8") as f:
                json.dump({"book": name, "chapter": ch, "verses": verses}, f)
    return lang


class OtbFolderTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        folder = make_otb(self.tmp.name)
        out = epub2cpb.convert(folder, "en", meta={}, otb_commit_id="abc123")
        self.verses, self.headings, self.names, abbrs, self.title, self.meta = out
        pack, _ = epub2cpb.build_pack(self.verses, self.headings, self.names, self.title, "en", self.meta, abbrs)
        self.pack = epub2cpb.read_pack(pack)

    def tearDown(self):
        self.tmp.cleanup()

    def test_text_and_flags(self):
        v = self.verses
        self.assertEqual(len(v), 65 * 4 + 4 + 3)
        self.assertEqual((v[0].text(), v[0].flags), ("In the beginning there was text.", epub2cpb.VF_PARAGRAPH))
        self.assertEqual(v[1].flags, 0)
        self.assertEqual((v[2].text(), v[2].flags), ("Third goes on.", epub2cpb.VF_PARAGRAPH))
        self.assertEqual(v[3].flags, epub2cpb.VF_PARAGRAPH)  # after a heading

    def test_psalm(self):
        ps = [x for x in self.verses if x.book == 19 and x.chapter == 2]
        self.assertEqual([x.text() for x in ps], ["Line one, line two.", "Streams.", "Prose Selah"])
        self.assertEqual([x.flags for x in ps], [epub2cpb.VF_PARAGRAPH | epub2cpb.VF_POETRY, epub2cpb.VF_POETRY,
                                                 epub2cpb.VF_PARAGRAPH | epub2cpb.VF_POETRY])  # any part poetry
        first = self.verses.index(ps[0])
        self.assertIn((first, epub2cpb.HK_PSALM_TITLE, "A Psalm of David."), self.headings)

    def test_section_note(self):
        self.assertEqual(self.headings[0], (3, epub2cpb.HK_SECTION, "Some manuscripts add a note here"))

    def test_names_and_meta(self):
        self.assertEqual(self.names[19], "Psalms")
        self.assertEqual(self.pack["book_names"][2], "Book 2")
        self.assertEqual(self.pack["abbrs"][19], "Ps")
        self.assertEqual(self.pack["title"], "Test Bible")
        self.assertEqual(self.pack["lang"], "en")
        m = self.pack["meta"]
        self.assertEqual(m["language_name"], "English")
        self.assertEqual((m["direction"], m["wrap"], m["hyphenation"]), ("ltr", "word", "en"))
        self.assertEqual((m["edition"], m["license"]), ("Test Bible", "CC BY-SA 4.0"))
        self.assertEqual(m["source"], "Open Translation Bible (https://openbible.uk) en-GB @abc123")

    def test_round_trip(self):
        self.assertEqual([x.text() for x in self.pack["verses"]], [x.text() for x in self.verses])
        self.assertEqual(self.pack["headings"], self.headings)


class AbbreviationTest(unittest.TestCase):
    def test_tables(self):
        names = {bk: f"Book {bk}" for bk in range(1, 67)}
        for lang in ("en", "de", "fr", "es", "pt", "it", "nl", "ru", "ja", "zh", "ko"):
            a = epub2cpb.abbreviations_for(lang, names)
            self.assertEqual(len(set(a.values())), 66, lang)
            self.assertTrue(all(len(x.encode()) <= 15 for x in a.values()), lang)
            self.assertTrue(all(epub2cpb.fits_grid(x) for x in a.values()),
                            (lang, [x for x in a.values() if not epub2cpb.fits_grid(x)]))
        self.assertEqual(epub2cpb.abbreviations_for("en", names)[9], "1Sa")

    def test_grid_width(self):
        self.assertIsNotNone(epub2cpb.grid_label_width("Gen"))
        self.assertIsNone(epub2cpb.grid_label_width("창"))  # not in the built-in font: not measured
        self.assertFalse(epub2cpb.fits_grid("2Thess"))

    def test_derived(self):
        names = {bk: f"Kitabu {bk}" for bk in range(1, 67)}
        names.update({9: "1 Samweli", 10: "2 Samweli", 43: "Johannesevangeliet", 66: "Johannes' Åbenbaring",
                      19: "صموئيل الأول", 20: "صموئيل الثاني", 1: "التكوين"})
        a = epub2cpb.derive_abbreviations(names)
        self.assertEqual(len(set(a.values())), 66)
        self.assertEqual((a[9], a[10], a[43], a[66]), ("1Sa", "2Sa", "Joh", "Joha"))
        self.assertEqual((a[19], a[20], a[1]), ("1صمو", "2صمو", "تكو"))

    def test_otb_pack_uses_short_abbreviations(self):
        with tempfile.TemporaryDirectory() as t:
            out = epub2cpb.convert(make_otb(t), "en", otb_commit_id="x")
        pack = epub2cpb.read_pack(epub2cpb.build_pack(out[0], out[1], out[2], out[4], "en", out[5], out[3])[0])
        self.assertEqual((pack["abbrs"][1], pack["abbrs"][19], pack["abbrs"][66]), ("Gen", "Ps", "Rev"))


class Utf8TruncTest(unittest.TestCase):
    def test_cuts_on_character_boundary(self):
        self.assertEqual(epub2cpb.utf8_trunc("요한계시록", 7), "요한".encode())
        self.assertEqual(epub2cpb.utf8_trunc("abc", 15), b"abc")



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
