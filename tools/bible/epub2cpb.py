#!/usr/bin/env python3
"""Convert a Bible EPUB into a CrossPoint Bible pack (.cpb).

The pack gives the firmware O(1) access to any book/chapter/verse without
parsing HTML on the device. Layout (little endian) is described in
docs/bible-pack-format.md and mirrored by lib/BiblePack/BiblePack.h.

usage: epub2cpb.py BIBLE.epub OUT.cpb [--title 개역개정] [--lang ko] [--charset OUT.txt]

Supported source: the "ESV template" Korean EPUB (bNN.MM.Name.text.html files,
span.chapter-num / span.verse-num markers, p.heading section titles).
"""

import argparse
import re
import struct
import sys
import zipfile
from html.parser import HTMLParser

MAGIC = b"CPBIBLE\0"
VERSION = 1
HEADER_SIZE = 64
BOOK_ENTRY_SIZE = 64
CHAPTER_ENTRY_SIZE = 8
HEADING_ENTRY_SIZE = 12

# Verse flags
VF_PARAGRAPH = 0x01  # verse starts a new paragraph
VF_POETRY = 0x02  # verse sits in a poetry line block

# Heading kinds
HK_SECTION = 0
HK_PSALM_TITLE = 1
HK_PSALM_BOOK = 2

KO_ABBR = (
    "창 출 레 민 신 수 삿 룻 삼상 삼하 왕상 왕하 대상 대하 스 느 에 욥 시 잠 전 아 사 렘 애 겔 단 "
    "호 욜 암 옵 욘 미 나 합 습 학 슥 말 "
    "마 막 눅 요 행 롬 고전 고후 갈 엡 빌 골 살전 살후 딤전 딤후 딛 몬 히 약 벧전 벧후 요일 요이 요삼 유 계"
).split()
assert len(KO_ABBR) == 66

TEXT_FILE = re.compile(r"b(\d\d)\.(\d\d)\.[^/]+\.text\.html$")
MAIN_FILE = re.compile(r"b(\d\d)\.00\.[^/]+\.main\.html$")


# Typos in the 2018 개역개정 EPUB (stray markup characters, a mistyped book name).
FIXUPS = (("^", ""), ("{", ""), ("}", ""), ("ㅘㅇ하", "왕하"))


def norm_space(s):
    for a, b in FIXUPS:
        s = s.replace(a, b)
    return re.sub(r"\s+", " ", s.replace(" ", " "))


class Verse:
    __slots__ = ("book", "chapter", "verse", "parts", "flags")

    def __init__(self, book, chapter, verse, flags):
        self.book, self.chapter, self.verse, self.flags = book, chapter, verse, flags
        self.parts = []

    def text(self):
        return norm_space("".join(self.parts)).strip()


class BibleParser(HTMLParser):
    """Walks every text file in reading order and splits it into verses."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.verses = []
        self.headings = []  # (verse index it precedes, kind, text)
        self.book = 0
        self.chapter = 0
        self.cur = None
        self.p_class = None
        self.p_fresh = False  # no verse text seen yet in this <p>
        self.capture = None  # None | "verse-num" | "chapter-num" | "skip" | heading kind
        self.capture_buf = []
        self.span_stack = []
        self.pending_headings = []

    def start_book(self, book):
        self.book = book
        self.chapter = 0

    # --- verse handling
    def new_verse(self, chapter, verse):
        flags = 0
        if self.p_fresh:
            flags |= VF_PARAGRAPH
        if self.p_class in ("line", "line-group"):
            flags |= VF_POETRY
        self.cur = Verse(self.book, chapter, verse, flags)
        self.verses.append(self.cur)
        idx = len(self.verses) - 1
        for kind, text in self.pending_headings:
            self.headings.append((idx, kind, text))
        self.pending_headings = []
        self.p_fresh = False

    # --- HTMLParser callbacks
    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        cls = a.get("class", "")
        if tag == "p":
            self.p_class = cls
            self.p_fresh = True
            if cls == "heading":
                self.capture, self.capture_buf = HK_SECTION, []
            elif cls == "psalm-title":
                self.capture, self.capture_buf = HK_PSALM_TITLE, []
            elif cls == "psalm-book":
                self.capture, self.capture_buf = HK_PSALM_BOOK, []
            elif self.cur is not None and self.cur.parts:
                # Verse continues into a new paragraph.
                self.cur.parts.append(" ")
            return
        if tag == "span":
            self.span_stack.append(cls)
            if cls in ("verse-num", "chapter-num"):
                self.num_kind, self.num_buf = cls, []
            return
        if tag == "br" and self.cur is not None and isinstance(self.capture, type(None)):
            self.cur.parts.append(" ")

    def handle_endtag(self, tag):
        if tag == "span" and self.span_stack:
            cls = self.span_stack.pop()
            if cls in ("verse-num", "chapter-num"):
                n = int("".join(self.num_buf).strip())
                if cls == "chapter-num":
                    if n != self.chapter + 1:
                        raise ValueError(f"book {self.book}: chapter {n} after {self.chapter}")
                    self.chapter = n
                    self.new_verse(n, 1)
                else:
                    if self.cur is None or self.cur.chapter != self.chapter:
                        raise ValueError(f"book {self.book} ch {self.chapter}: verse {n} before chapter")
                    if n != self.cur.verse + 1:
                        print(f"warn: {self.book}:{self.chapter} verse {n} after {self.cur.verse}", file=sys.stderr)
                    self.new_verse(self.chapter, n)
            return
        if tag == "p":
            if isinstance(self.capture, int):
                text = norm_space("".join(self.capture_buf)).strip()
                if text:
                    self.pending_headings.append((self.capture, text))
            self.capture = None
            self.p_class = None

    def handle_data(self, data):
        if self.span_stack:
            top = self.span_stack[-1]
            if top in ("verse-num", "chapter-num"):
                self.num_buf.append(data)
                return
            if top in ("book-name", "footnote"):
                return
        if isinstance(self.capture, int):
            self.capture_buf.append(data)
            return
        if self.cur is not None and self.p_class is not None:
            if data.strip():
                self.p_fresh = False
            self.cur.parts.append(data)


class NameParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.name = None

    def handle_starttag(self, tag, attrs):
        if tag == "h1" and self.name is None:
            self.name = dict(attrs).get("title")


def parse_epub(path):
    z = zipfile.ZipFile(path)
    names = z.namelist()
    texts = sorted((n for n in names if TEXT_FILE.search(n)), key=lambda n: TEXT_FILE.search(n).groups())
    book_names = {}
    for n in names:
        m = MAIN_FILE.search(n)
        if m:
            p = NameParser()
            p.feed(z.read(n).decode("utf-8"))
            book_names[int(m.group(1))] = p.name
    parser = BibleParser()
    current = None
    for n in texts:
        book = int(TEXT_FILE.search(n).group(1))
        if book != current:
            parser.start_book(book)
            current = book
        parser.p_class = None
        parser.feed(z.read(n).decode("utf-8"))
    parser.close()
    return parser.verses, parser.headings, book_names


def build_pack(verses, headings, book_names, title, lang):
    # Text blob: verse texts, then heading texts.
    blob = bytearray()
    verse_offsets = []
    for v in verses:
        verse_offsets.append(len(blob))
        blob += v.text().encode("utf-8")
    verse_offsets.append(len(blob))
    heading_entries = []
    for idx, kind, text in headings:
        b = text.encode("utf-8")
        heading_entries.append((idx, len(blob), len(b), kind))
        blob += b

    # Chapters and books.
    chapters = []  # (first verse index, verse count, first heading index)
    books = {}  # book -> [first chapter index, chapter count]
    hi = 0
    for i, v in enumerate(verses):
        if not chapters or (v.book, v.chapter) != (chapters[-1][3], chapters[-1][4]):
            while hi < len(heading_entries) and heading_entries[hi][0] < i:
                hi += 1
            chapters.append([i, 0, hi, v.book, v.chapter])
            b = books.setdefault(v.book, [len(chapters) - 1, 0])
            b[1] += 1
        chapters[-1][1] += 1

    if sorted(books) != list(range(1, 67)):
        raise ValueError(f"expected 66 books, got {sorted(books)}")

    book_table = bytearray()
    for bk in range(1, 67):
        first, count = books[bk]
        name = (book_names.get(bk) or f"Book {bk}").encode("utf-8")[:39]
        abbr = KO_ABBR[bk - 1].encode("utf-8")[:15] if lang == "ko" else b""
        book_table += struct.pack("<BBHI40s16s", 0 if bk <= 39 else 1, bk, count, first, name, abbr)
        assert len(book_table) % BOOK_ENTRY_SIZE == 0

    chapter_table = b"".join(struct.pack("<IHH", c[0], c[1], c[2]) for c in chapters)
    verse_table = b"".join(struct.pack("<I", o) for o in verse_offsets)
    flag_table = bytes(v.flags for v in verses)
    heading_table = b"".join(struct.pack("<IIHBB", idx, off, ln, kind, 0) for idx, off, ln, kind in heading_entries)
    title_b = title.encode("utf-8")[:63] + b"\0"

    off = HEADER_SIZE
    sections = {}
    body = bytearray()
    for key, data in (
        ("title", title_b),
        ("books", book_table),
        ("chapters", chapter_table),
        ("verses", verse_table),
        ("flags", flag_table),
        ("headings", heading_table),
        ("text", bytes(blob)),
    ):
        pad = (-(off + len(body))) % 4
        body += b"\0" * pad
        sections[key] = off + len(body)
        body += data

    header = struct.pack(
        "<8sHHII IIIIIIII I 8s",
        MAGIC,
        VERSION,
        66,
        len(chapters),
        len(verses),
        sections["title"],
        sections["books"],
        sections["chapters"],
        sections["verses"],
        sections["flags"],
        sections["headings"],
        len(heading_entries),
        sections["text"],
        len(blob),
        lang.encode("ascii")[:7],
    )
    header += b"\0" * (HEADER_SIZE - len(header))
    assert len(header) == HEADER_SIZE
    return header + bytes(body), chapters


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("epub")
    ap.add_argument("out")
    ap.add_argument("--title", default="개역개정")
    ap.add_argument("--lang", default="ko")
    ap.add_argument("--charset", help="write every code point used (one string) to this file, for font subsetting")
    args = ap.parse_args()

    verses, headings, book_names = parse_epub(args.epub)
    pack, chapters = build_pack(verses, headings, book_names, args.title, args.lang)
    with open(args.out, "wb") as f:
        f.write(pack)
    empty = [f"{v.book}:{v.chapter}:{v.verse}" for v in verses if not v.text()]
    print(f"{args.out}: {len(pack)} bytes, {len(chapters)} chapters, {len(verses)} verses, {len(headings)} headings")
    if empty:
        print(f"warn: {len(empty)} empty verses: {' '.join(empty[:20])}", file=sys.stderr)
    if args.charset:
        chars = set()
        for v in verses:
            chars.update(v.text())
        for _, _, t in headings:
            chars.update(t)
        for n in book_names.values():
            chars.update(n or "")
        with open(args.charset, "w", encoding="utf-8") as f:
            f.write("".join(sorted(chars)))


if __name__ == "__main__":
    main()
