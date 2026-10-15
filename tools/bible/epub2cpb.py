#!/usr/bin/env python3
"""Convert a Bible EPUB, getBible JSON or Open Translation Bible folder into a CrossPoint Bible pack (.cpb).

The pack gives the firmware O(1) access to any book/chapter/verse without
parsing HTML on the device. Layout (little endian) is described in
docs/bible-pack-format.md and mirrored by lib/BiblePack/BiblePack.h.

usage: epub2cpb.py BIBLE.epub|BIBLE.json|OTB_LANG_DIR|PACK.cpb OUT.cpb [--title 개역개정]
                  [--lang ko] [--meta key=value ...] [--charset OUT.txt]

Supported sources:
  - the "ESV template" Korean EPUB (bNN.MM.Name.text.html files,
    span.chapter-num / span.verse-num markers, p.heading section titles);
  - getBible v2 JSON (https://api.getbible.net/v2/<translation>.json), e.g. the
    public-domain 개역한글 (korean.json). It has no section titles; verse numbers
    missing inside a chapter become empty verses so numbering stays right.
  - an Open Translation Bible language folder (https://openbible.uk, CC BY-SA 4.0),
    <otb repo>/lang/<code> with <NN.BookName>/json/<book>-<NN>.json chapter files.
    "---" separators start a paragraph; other separators are headings (psalm
    superscriptions become psalm titles).
  - an existing pack (.cpb): same text, metadata rewritten (used to add keys).

Metadata (shown on the device's Bible info screen) is stored after the title as
"key<TAB>value" lines: edition, copyright, license, source, version, note.
EPUB packs take it from the book's copyright pages, JSON packs from the file;
--meta overrides or adds keys. The language keys (language_name, direction, wrap,
hyphenation) come from languages.json for --lang (docs/languages.md).
"""

import argparse
import html
import json
import os
import re
import subprocess
import unicodedata
import struct
import sys
import zipfile
import zlib
from html.parser import HTMLParser

MAGIC = b"CPBIBLE\0"
VERSION = 2
HEADER_SIZE = 64
BOOK_ENTRY_SIZE = 64
CHAPTER_ENTRY_SIZE = 24
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


def html_text(raw):
    text = html.unescape(re.sub(r"<[^>]+>", "\n", raw))
    return [norm_space(t).strip() for t in text.split("\n") if t.strip()]


def epub_meta(path):
    """Edition, copyright and version lines from the EPUB's front matter, when present."""
    z = zipfile.ZipFile(path)
    meta = {}
    for n in z.namelist():
        base = n.rsplit("/", 1)[-1].lower()
        if base not in ("copyright.html", "notice.html"):
            continue
        lines = html_text(z.read(n).decode("utf-8", "replace"))
        for i, line in enumerate(lines):
            if "©" in line and "copyright" not in meta:
                meta["copyright"] = line
            elif line.startswith("Version") and "version" not in meta:
                # "Version:" and its value are often separate elements.
                value = line.replace("Version:", "").strip() or (lines[i + 1] if i + 1 < len(lines) else "")
                meta["version"] = value
            elif "성경전서" in line and "(" in line and "edition" not in meta:
                meta["edition"] = line
    return meta


def parse_getbible(path):
    """Verses from a getBible v2 JSON file (books -> chapters -> verses)."""
    with open(path, encoding="utf-8") as f:
        d = json.load(f)
    verses, book_names = [], {}
    for bk in d["books"]:
        number = int(bk["nr"])
        book_names[number] = bk["name"]
        for ch in bk["chapters"]:
            texts = {int(v["verse"]): norm_space(v["text"]).strip() for v in ch["verses"]}
            for n in range(1, max(texts) + 1):
                v = Verse(number, int(ch["chapter"]), n, VF_PARAGRAPH if n == 1 else 0)
                v.parts.append(texts.get(n, ""))
                verses.append(v)
    meta = {
        "edition": d.get("description") or d.get("translation") or "",
        "license": d.get("distribution_license") or "",
        "source": "; ".join(x for x in (d.get("distribution_about"), "getBible (api.getbible.net)") if x),
        "version": " ".join(x for x in (d.get("distribution_version"), d.get("distribution_version_date")) if x),
    }
    return verses, [], book_names, {k: v for k, v in meta.items() if v}


LANGUAGES_JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "languages.json")
OTB_LICENSE = "CC BY-SA 4.0"
OTB_URL = "https://openbible.uk"
# Keys the firmware reads to pick fonts and layout; written before the info-screen keys
# so the 1500-byte metadata cap never cuts them.
LANGUAGE_KEYS = ("language_name", "direction", "wrap", "hyphenation", "shaped")


def load_languages(path=LANGUAGES_JSON):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def language_entry(code, languages=None):
    for entry in (languages or load_languages())["languages"]:
        if entry["code"] == code:
            return entry
    return None


def language_meta(code, languages=None):
    """language_name, direction, wrap and hyphenation for a languages.json code ({} if unknown)."""
    entry = language_entry(code, languages)
    if entry is None:
        return {}
    meta = {"language_name": entry["name"], "direction": entry["dir"], "wrap": entry["wrap"]}
    if entry.get("hyphenation"):
        meta["hyphenation"] = entry["hyphenation"]
    return meta


ABBREVIATIONS_JSON = os.path.join(os.path.dirname(os.path.abspath(__file__)), "abbreviations.json")
# Ordinal words that number a book ("صموئيل الأول", "שמואל א"): become a leading digit.
ORDINALS = {
    "الأول": "1", "الأولى": "1", "الثاني": "2", "الثانية": "2", "الثالث": "3", "الثالثة": "3",
    "اول": "1", "دوم": "2", "سوم": "3", "א": "1", "ב": "2", "ג": "3",
    "I": "1", "II": "2", "III": "3",
    "Fyrsta": "1", "Önnur": "2", "Annað": "2", "Þriðja": "3", "Fjórða": "4", "Fimmta": "5",
}
VIRAMAS = {"\u094d", "\u0d4d"}  # Devanagari, Malayalam: a cut must not end on one


def _letters(word):
    # Private-use code points are pre-shaped clusters (tools/bible/shape.py): letters too.
    return "".join(ch for ch in word if ch.isalnum() or unicodedata.category(ch)[0] == "M"
                   or unicodedata.category(ch) == "Co")


def _prefix(word, n):
    """First n letters of word, extended so the cut keeps combining marks with their base."""
    while n < len(word) and (unicodedata.category(word[n]).startswith("M") or word[n - 1] in VIRAMAS):
        n += 1
    return word[:n]


def _strip_article(word):
    """Arabic definite article: "التكوين" -> "تكوين" (keeps short words whole)."""
    return word[2:] if word.startswith("ال") and len(word) > 4 else word


# The book grid has six columns; on the X4 (480 px wide) a cell leaves about 68 px for the
# label, drawn in 14 pt bold. Widths come from the built-in Noto Serif, so the check applies
# to scripts it covers (Latin, Greek, Cyrillic); other scripts use short labels anyway.
GRID_LABEL_WIDTH = 68
GRID_FONT_HEADER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib", "EpdFont",
                                "builtinFonts", "notoserif_14_bold.h")
_grid_advances = None


def grid_label_width(text):
    """Pen advance of text in the built-in 14 pt bold Noto Serif, or None when a character is
    not in that font (the label is then drawn with a bundle font and not measured here)."""
    global _grid_advances
    if _grid_advances is None:
        _grid_advances = {}
        try:
            with open(GRID_FONT_HEADER, encoding="utf-8") as f:
                for m in re.finditer(r"\{ *\d+, *\d+, *(\d+),[^}]*\}, // (?:U\+([0-9A-F]+)|(.))\n", f.read()):
                    cp = int(m.group(2), 16) if m.group(2) else ord(m.group(3))
                    _grid_advances[cp] = int(m.group(1)) / 16  # 12.4 fixed point
        except FileNotFoundError:
            pass
    if not _grid_advances or any(ord(ch) not in _grid_advances for ch in text):
        return None
    return sum(_grid_advances[ord(ch)] for ch in text)


def fits_grid(text):
    w = grid_label_width(text)
    return w is None or w <= GRID_LABEL_WIDTH


def derive_abbreviations(book_names, limit=15):
    """Short unique abbreviations from book names: a leading number ("1 ", "1. ", or an ordinal
    word anywhere) becomes a digit prefix; then the first 3 letters of the first word, growing
    (initials of the other words, then more letters) until unique within the 66 books. Each
    must fit a book-grid cell (fits_grid); when the longer ones do not, two letters or the
    first letter with a later one are tried."""
    out, used = {}, set()
    for bk in range(1, 67):
        words = (book_names.get(bk) or f"B{bk}").replace(".", " ").split()
        num = ""
        if words and words[0].isdigit():
            num, words = words[0], words[1:]
        rest = []
        for w in words:
            if not num and w in ORDINALS:
                num = ORDINALS[w]
            else:
                rest.append(w)
        words = rest or words or [str(bk)]
        if len(words) > 1 and len(_letters(words[0])) <= 2:  # "אל הרומים": skip a short preposition
            words = words[1:]
        first = _strip_article(_letters(words[0]) or words[0])
        initials = "".join(_letters(_strip_article(w))[:1] for w in words[1:])
        candidates = [_prefix(first, 3), _prefix(first, 3) + initials]
        candidates += [_prefix(first, n) for n in range(4, len(first) + 1)]
        candidates += [first + initials, "".join(_letters(w) for w in words)]
        candidates += [_prefix(first, 2)] + [first[0] + ch for ch in first[2:] if ch.isalpha()]
        candidates += [first[0] + ch for w in words[1:] for ch in _letters(w)[:1]]
        choice = None
        for c in candidates:
            c = num + c
            if c and c not in used and len(c.encode("utf-8")) <= limit and fits_grid(c):
                choice = c
                break
        if choice is None:
            n = 2
            while f"{num}{_prefix(first, 3)}{n}" in used:
                n += 1
            choice = utf8_trunc(f"{num}{_prefix(first, 3)}{n}", limit).decode("utf-8")
        used.add(choice)
        out[bk] = choice
    return out


def abbreviations_for(lang, book_names):
    """book -> abbreviation: Korean list, the abbreviations.json table, or derived from the names."""
    if lang == "ko":
        return {bk: KO_ABBR[bk - 1] for bk in range(1, 67)}
    try:
        with open(ABBREVIATIONS_JSON, encoding="utf-8") as f:
            table = json.load(f).get(lang)
    except FileNotFoundError:
        table = None
    if table:
        return {bk: table[bk - 1] for bk in range(1, 67)}
    return derive_abbreviations(book_names)


def utf8_trunc(s, limit):
    """At most `limit` UTF-8 bytes of s, cut on a character boundary."""
    b = s.encode("utf-8")
    if len(b) <= limit:
        return b
    b = b[:limit]
    while b and (b[-1] & 0xC0) == 0x80:  # drop continuation bytes of a cut character
        b = b[:-1]
    if b and b[-1] >= 0xC0:  # and its lead byte
        b = b[:-1]
    return b


# --- Open Translation Bible (https://openbible.uk), lang/<code>/<NN.Name>/json/<name>-<NN>.json

OTB_BOOK_DIR = re.compile(r"^(\d\d)\.(.+)$")
OTB_STRAY = (("\\", ""),)  # a stray backslash before a word (hi-IN Psalm 46:7)


def otb_line(text):
    """One text item: drop the Markdown blockquote "> " poetry marker (or a lone ">") and stray characters.
    Square brackets stay: they mark textual variants."""
    t = text.strip()
    poetry = t.startswith(">")
    if poetry:
        t = t.lstrip(">").strip()
    for a, b in OTB_STRAY:
        t = t.replace(a, b)
    return t, poetry


def otb_verse_lines(items):
    """Verse text items without in-verse "---" breaks and the editorial note that can follow one.

    The pack has no paragraph break inside a verse, so "---" is dropped. A bracketed run
    right after it ("[Psalm 78 continues to verse 72; ... next message?]", every language,
    Psalm 78:16) is a leftover of the machine translation, not text.
    """
    items = [x for x in items if isinstance(x, str)]
    out = []
    i = 0
    while i < len(items):
        if items[i].strip() == "---":
            rest = " ".join(x.strip() for x in items[i + 1 :]).strip()
            if rest.startswith("[") and rest.endswith("]"):
                break
            i += 1
            continue
        out.append(items[i])
        i += 1
    return out


def otb_commit(folder):
    root = os.path.normpath(os.path.join(folder, os.pardir, os.pardir))
    try:
        return subprocess.run(["git", "-C", root, "rev-parse", "HEAD"], capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def parse_otb(folder, commit=None):
    """Verses, headings, book names and metadata from an OTB language folder."""
    folder = os.path.normpath(folder)
    code = os.path.basename(folder)
    books = {}
    for name in os.listdir(folder):
        m = OTB_BOOK_DIR.match(name)
        if m and os.path.isdir(os.path.join(folder, name, "json")):
            books[int(m.group(1))] = name
    if sorted(books) != list(range(1, 67)):
        raise ValueError(f"{folder}: expected book folders 01..66, got {sorted(books)}")

    verses, headings, book_names = [], [], {}
    for number in range(1, 67):
        dirname = books[number]
        book_names[number] = OTB_BOOK_DIR.match(dirname).group(2).strip()
        jdir = os.path.join(folder, dirname, "json")
        files = []
        for f in os.listdir(jdir):
            m = re.search(r"-(\d+)\.json$", f)
            if m:
                files.append((int(m.group(1)), f))
        files.sort()
        if [n for n, _ in files] != list(range(1, len(files) + 1)):
            raise ValueError(f"{jdir}: chapters {[n for n, _ in files]}")
        for chapter, f in files:
            with open(os.path.join(jdir, f), encoding="utf-8") as fh:
                d = json.load(fh)
            paragraph = True  # a chapter starts a paragraph
            pending = []  # (kind, text) headings waiting for the next verse
            expected = 1
            for e in d["verses"]:
                if "verse" not in e:
                    lines = [otb_line(t)[0] for t in e.get("text", [])]
                    text = " ".join(t for t in lines if t)
                    if text == "---" or not text:
                        paragraph = True
                    else:
                        kind = HK_PSALM_TITLE if number == 19 else HK_SECTION
                        pending.append((kind, text))
                        paragraph = True
                    continue
                n = int(e["verse"])
                while expected < n:  # keep numbering when a verse is missing
                    v = Verse(number, chapter, expected, 0)
                    verses.append(v)
                    print(f"warn: {code} {number}:{chapter}:{expected} missing", file=sys.stderr)
                    expected += 1
                if n != expected:
                    raise ValueError(f"{jdir}/{f}: verse {n} after {expected - 1}")
                parsed = [otb_line(t) for t in otb_verse_lines(e.get("text", []))]
                flags = (VF_PARAGRAPH if paragraph else 0) | (VF_POETRY if any(p for _, p in parsed) else 0)
                v = Verse(number, chapter, n, flags)
                v.parts.append(" ".join(t for t, _ in parsed if t))
                verses.append(v)
                for kind, text in pending:
                    headings.append((len(verses) - 1, kind, text))
                pending = []
                paragraph = False
                expected += 1
            for _, text in pending:
                print(f"warn: {code} {number}:{chapter}: heading after the last verse dropped: {text}", file=sys.stderr)

    meta = {}
    lang_json = os.path.join(folder, "lang.json")
    if os.path.exists(lang_json):
        with open(lang_json, encoding="utf-8") as fh:
            name = json.load(fh).get("NAME", "")
        if name:
            meta["edition"] = name
    commit = otb_commit(folder) if commit is None else commit
    meta["license"] = OTB_LICENSE
    meta["source"] = f"Open Translation Bible ({OTB_URL}) {code}" + (f" @{commit}" if commit else "")
    return verses, headings, book_names, meta


# --- Reading a pack back (re-packing with new metadata, charsets for font subsetting)


def read_pack(path_or_bytes):
    """dict(title, lang, meta, verses, headings, book_names, abbrs) from a version 2 pack."""
    p = path_or_bytes
    if isinstance(p, str):
        with open(p, "rb") as f:
            p = f.read()
    magic, ver, nbooks, nchap, nverse = struct.unpack_from("<8sHHII", p)
    if magic != MAGIC or ver != VERSION:
        raise ValueError(f"not a version {VERSION} pack")
    (title_off, books_off, chapters_off, verses_off, flags_off, headings_off, nheadings, blocks_off,
     _max_raw) = struct.unpack_from("<9I", p, 20)
    lang = p[56:64].split(b"\0")[0].decode("ascii")
    end = p.index(b"\0", title_off)
    title = p[title_off:end].decode("utf-8")
    meta_end = p.index(b"\0", end + 1)
    meta = {}
    for line in p[end + 1 : meta_end].decode("utf-8").split("\n"):
        if "\t" in line:
            k, _, v = line.partition("\t")
            meta[k] = v
    book_names, abbrs, chapter_numbers = {}, {}, {}
    for i in range(nbooks):
        _, number, count, first, name, abbr = struct.unpack_from("<BBHI40s16s", p, books_off + i * BOOK_ENTRY_SIZE)
        book_names[number] = name.split(b"\0")[0].decode("utf-8")
        abbrs[number] = abbr.split(b"\0")[0].decode("utf-8")
        for c in range(count):
            chapter_numbers[first + c] = (number, c + 1)
    headings_raw = [struct.unpack_from("<IIHBB", p, headings_off + i * HEADING_ENTRY_SIZE) for i in range(nheadings)]
    verses, headings = [], []
    hi = 0
    for ci in range(nchap):
        first, count, _, comp_off, comp_size, raw_size, verses_end = struct.unpack_from(
            "<IHHIIII", p, chapters_off + ci * CHAPTER_ENTRY_SIZE)
        raw = zlib.decompress(p[blocks_off + comp_off : blocks_off + comp_off + comp_size], -15)
        assert len(raw) == raw_size
        starts = [struct.unpack_from("<H", p, verses_off + 2 * (first + i))[0] for i in range(count)] + [verses_end]
        book, chapter = chapter_numbers[ci]
        for i in range(count):
            v = Verse(book, chapter, i + 1, p[flags_off + first + i])
            v.parts.append(raw[starts[i] : starts[i + 1]].decode("utf-8"))
            verses.append(v)
        while hi < nheadings and headings_raw[hi][0] < first + count:
            idx, off, ln, kind, _ = headings_raw[hi]
            headings.append((idx, kind, raw[off : off + ln].decode("utf-8")))
            hi += 1
    return {"title": title, "lang": lang, "meta": meta, "verses": verses, "headings": headings,
            "book_names": book_names, "abbrs": abbrs}


def pack_charset(pack):
    """Every character a pack shows: text, headings, book names, abbreviations, title, metadata values."""
    chars = set(pack["title"])
    for v in pack["verses"]:
        chars.update(v.text())
    for _, _, t in pack["headings"]:
        chars.update(t)
    for d in (pack["book_names"], pack["abbrs"]):
        for t in d.values():
            chars.update(t)
    for k, v in pack["meta"].items():
        if k not in ("direction", "wrap", "hyphenation", "shaped"):
            chars.update(v)
    return chars


def encode_meta(title, meta):
    """Title, NUL, then key<TAB>value lines, NUL. Readers of the plain title stop at the first NUL.

    Lines that would push the block past 1500 bytes are left out whole.
    """
    out = b""
    for k, v in meta.items():
        if not v:
            continue
        line = f"{k}\t{v}\n".encode("utf-8")
        if len(out) + len(line) > 1500:
            print(f"warn: metadata {k} dropped (1500-byte limit)", file=sys.stderr)
            continue
        out += line
    return utf8_trunc(title, 63) + b"\0" + out + b"\0"


def order_meta(meta):
    """Language keys first (see LANGUAGE_KEYS), then the rest in their order."""
    out = {k: meta[k] for k in LANGUAGE_KEYS if meta.get(k)}
    out.update((k, v) for k, v in meta.items() if k not in out)
    return out


def build_pack(verses, headings, book_names, title, lang, meta=None, abbrs=None):
    """The pack bytes and its chapter list. abbrs: book -> abbreviation (max 15 bytes); default
    abbreviations_for(lang): KO_ABBR, abbreviations.json, or derived from the book names."""
    # Chapters and books.
    chapters = []  # [first verse index, verse count, first heading index, book, chapter]
    books = {}  # book -> [first chapter index, chapter count]
    hi = 0
    for i, v in enumerate(verses):
        if not chapters or (v.book, v.chapter) != (chapters[-1][3], chapters[-1][4]):
            while hi < len(headings) and headings[hi][0] < i:
                hi += 1
            chapters.append([i, 0, hi, v.book, v.chapter])
            b = books.setdefault(v.book, [len(chapters) - 1, 0])
            b[1] += 1
        chapters[-1][1] += 1

    if sorted(books) != list(range(1, 67)):
        raise ValueError(f"expected 66 books, got {sorted(books)}")

    # One raw-deflate block per chapter: its verse texts, then its heading texts.
    # Verse and heading offsets are relative to the uncompressed block.
    verse_offsets = [0] * len(verses)
    heading_entries = []
    blocks = bytearray()
    chapter_entries = []
    max_raw = 0
    for ci, (first, count, first_heading, _, _) in enumerate(chapters):
        raw = bytearray()
        for vi in range(first, first + count):
            verse_offsets[vi] = len(raw)
            raw += verses[vi].text().encode("utf-8")
        verses_end = len(raw)
        h = first_heading
        while h < len(headings) and headings[h][0] < first + count:
            idx, kind, text = headings[h]
            b = text.encode("utf-8")
            heading_entries.append((idx, len(raw), len(b), kind))
            raw += b
            h += 1
        if len(raw) > 0xFFFF:
            raise ValueError(f"chapter {ci} is {len(raw)} bytes; verse offsets are u16")
        comp = zlib.compressobj(level=9, wbits=-15)
        data = comp.compress(bytes(raw)) + comp.flush()
        chapter_entries.append((first, count, first_heading, len(blocks), len(data), len(raw), verses_end))
        blocks += data
        max_raw = max(max_raw, len(raw))
    assert len(heading_entries) == len(headings)

    book_table = bytearray()
    default_abbrs = abbreviations_for(lang, book_names)
    for bk in range(1, 67):
        first, count = books[bk]
        name_s = book_names.get(bk) or f"Book {bk}"
        name = utf8_trunc(name_s, 39)
        abbr_s = (abbrs or {}).get(bk) or default_abbrs[bk]
        abbr = utf8_trunc(abbr_s, 15)
        book_table += struct.pack("<BBHI40s16s", 0 if bk <= 39 else 1, bk, count, first, name, abbr)
        assert len(book_table) % BOOK_ENTRY_SIZE == 0

    chapter_table = b"".join(struct.pack("<IHHIIII", *c) for c in chapter_entries)
    assert len(chapter_table) == len(chapters) * CHAPTER_ENTRY_SIZE
    verse_table = b"".join(struct.pack("<H", o) for o in verse_offsets)
    flag_table = bytes(v.flags for v in verses)
    heading_table = b"".join(struct.pack("<IIHBB", idx, off, ln, kind, 0) for idx, off, ln, kind in heading_entries)
    title_b = encode_meta(title, order_meta(meta or {}))

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
        ("blocks", bytes(blocks)),
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
        sections["blocks"],
        max_raw,
        lang.encode("ascii")[:7],
    )
    header += b"\0" * (HEADER_SIZE - len(header))
    assert len(header) == HEADER_SIZE
    return header + bytes(body), chapters


def convert(source, lang, title=None, meta=None, otb_commit_id=None):
    """Read any supported source. Returns (verses, headings, book_names, abbrs, title, meta).

    meta: the source's metadata, then languages.json keys for `lang`, then `meta` overrides.
    """
    abbrs = None
    if os.path.isdir(source):
        verses, headings, book_names, src_meta = parse_otb(source, otb_commit_id)
        default_title = src_meta.get("edition", "")
    elif source.lower().endswith(".cpb"):
        pack = read_pack(source)
        verses, headings, book_names, abbrs = pack["verses"], pack["headings"], pack["book_names"], pack["abbrs"]
        src_meta, default_title = pack["meta"], pack["title"]
    elif source.lower().endswith(".json"):
        verses, headings, book_names, src_meta = parse_getbible(source)
        default_title = src_meta.get("edition", "")
    else:
        verses, headings, book_names = parse_epub(source)
        src_meta, default_title = epub_meta(source), "개역개정"
    full = dict(src_meta)
    full.update(language_meta(lang))
    full.update(meta or {})
    return verses, headings, book_names, abbrs, title or default_title, full


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="Bible EPUB, getBible JSON, OTB language folder or existing .cpb")
    ap.add_argument("out")
    ap.add_argument("--title", help="pack title (default: EPUB 개역개정, OTB lang.json NAME, .cpb its title)")
    ap.add_argument("--lang", default="ko", help="language code from languages.json (header and language keys)")
    ap.add_argument("--meta", action="append", default=[], help="key=value metadata (repeatable)")
    ap.add_argument("--otb-commit", help="OTB commit for the source key (default: git rev-parse in the OTB clone)")
    ap.add_argument("--charset", help="write every code point used (one string) to this file, for font subsetting")
    args = ap.parse_args()

    overrides = {}
    for kv in args.meta:
        key, _, value = kv.partition("=")
        overrides[key.strip()] = value.strip()
    verses, headings, book_names, abbrs, title, meta = convert(args.source, args.lang, args.title, overrides,
                                                               args.otb_commit)
    pack, chapters = build_pack(verses, headings, book_names, title, args.lang, meta, abbrs)
    with open(args.out, "wb") as f:
        f.write(pack)
    empty = [f"{v.book}:{v.chapter}:{v.verse}" for v in verses if not v.text()]
    print(f"{args.out}: {len(pack)} bytes, {len(chapters)} chapters, {len(verses)} verses, {len(headings)} headings")
    if empty:
        print(f"warn: {len(empty)} empty verses: {' '.join(empty[:20])}", file=sys.stderr)
    if args.charset:
        chars = pack_charset(read_pack(pack))
        with open(args.charset, "w", encoding="utf-8") as f:
            f.write("".join(sorted(chars)))


if __name__ == "__main__":
    main()
