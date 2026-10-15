# Languages

CrossPoint Bible reads the Bible in 24 languages. English is built into the firmware; every other language is an SD card bundle. The table of languages, Bibles and fonts is `tools/bible/languages.json`.

## What is where

| | Firmware | SD bundle |
|---|---|---|
| English Bible (Open Translation Bible en-GB) | built in | - |
| Reader font Noto Serif (Latin, Greek, Cyrillic, Hebrew letters), 14/16/18/22/28 pt | built in | - |
| UI fonts Noto Sans 10 pt (Settings) and 14/16/22 pt (Bible menus, lists, headers), Latin, Greek, Cyrillic, Hebrew letters | built in | - |
| Hyphenation (en, de, es, fi, fr, it, pl, pt, ru, sv, uk) | built in | - |
| UI translations | built in | - |
| Other Bibles | - | `/bible/lang/<code>/*.cpb` |
| Fonts for other scripts | - | `/bible/fonts/<code>/` |

Latin and Cyrillic languages (da, de, es, fi, fr, id, is, it, nb, nl, pt, ru, so, sv, sw) need only their Bible: the built-in fonts cover them.

## SD card layout

```
/bible/
  lang/
    ko/
      krv1961.cpb          개역한글
      otb.cpb              열린 성경 (Open Translation Bible ko-KR)
    ja/
      otb.cpb
  fonts/
    ko/
      KoPub 바탕/14.epdfont 16.epdfont 18.epdfont 22.epdfont 28.epdfont
      리디바탕/14.epdfont ...
      ui/10.epdfont 14.epdfont 16.epdfont 22.epdfont   system font, Pretendard
    ja/
      Noto Serif JP/14.epdfont ...
      ui/10.epdfont
```

- A language is installed when `/bible/lang/<code>/` holds at least one `.cpb`. The language list shows English plus every installed language.
- `<code>` is the language code from `languages.json` (`ko`, `ja`, `zh`, `ar`, ...).
- Reader font families are the folders under `/bible/fonts/<code>/` other than `ui`. The folder name is the label shown on the device. Each family has one `.epdfont` per size, named `<pt>.epdfont`.
- `ui/` holds the language's sans-serif system font: `10.epdfont` for Settings, `14`, `16` and `22` for the Bible screens (menu, bookmarks, language list, About this Bible, keyboard, the reader's chapter title and page number). Switching language makes it the system font; without it the UI uses the built-in Noto Sans. The Bible text and Go to (book, chapter and verse grids) use the reader font.

| language | system font |
|---|---|
| Latin and Cyrillic languages, English | Noto Sans (built in) |
| ko | Pretendard |
| ja | Noto Sans JP |
| zh | Noto Sans SC |
| ar | Noto Sans Arabic |
| fa | Vazirmatn |
| he | Noto Sans Hebrew |
| hi, ml | Noto Sans (built in); the pre-shaped clusters in book names come from the reader font |
- Older layouts still work: a `.cpb` directly in `/bible/` is treated as an extra Bible of its own language.

Each release attaches one zip per language (`crosspoint-bible-<version>-<code>.zip`) and one with every language (`crosspoint-bible-<version>-all.zip`), each unzipping into this layout at the SD card root. The English zip carries the built-in pack; the firmware does not list an SD copy of the built-in pack (same title and size) as a second Bible.

## On the device

- Settings > System > Language (and the reader menu's Language row) lists English plus installed languages. Choosing one switches the UI language, the Bible and the fonts together.
- When a language has more than one Bible (Korean: 개역한글, 열린 성경), the menu shows Bible to pick one.
- When a language has more than one reader font (Korean: KoPub 바탕, 리디바탕), the menu shows Font.
- The reading position and bookmarks carry over between Bibles: all packs share the 66-book versification.

## Pack metadata

Packs (`docs/bible-pack-format.md`) carry these metadata keys:

| key | meaning |
|---|---|
| `language_name` | native name shown in the language list (`한국어`) |
| `direction` | `ltr` or `rtl` |
| `wrap` | `word` (break at spaces, hyphenate when the language has patterns) or `char` (break between any characters: Korean, Chinese, Japanese) |
| `hyphenation` | hyphenation pattern language, when built in |
| `shaped` | `pua`: text is pre-shaped into private-use glyphs that only the bundle's fonts carry (Hindi, Malayalam) |
| `chapter_fmt`, `psalm_fmt` | chapter and psalm titles with `%d`, for pre-shaped languages (their UI strings stay English) |
| `edition`, `license`, `copyright`, `source`, `version`, `note` | shown on the Bible info screen |

The header's language field holds the language code.

## Scripts

- **Latin, Greek, Cyrillic**: built-in fonts. Word wrap with hyphenation where patterns exist (English by default).
- **Korean, Chinese, Japanese**: SD fonts, subset to the characters the bundle's Bibles and UI strings use. Character wrap; justified lines spread the leftover over the gaps between characters, since there are few or no spaces.
- **Arabic, Persian, Hebrew**: SD fonts. Rendered at runtime with MiniBidi (joining forms via Arabic Presentation Forms, as upstream CrossPoint), so the fonts must carry the presentation forms. Lines run right to left: the layout places words from the right edge.
- **Book grid labels**: six columns leave about 68 px per label on the X4 at 14 pt bold in the reader font (serif). `abbreviations.json` lists fit the built-in Noto Serif (checked by `test_epub2cpb.py`), derived abbreviations are shortened until they fit, and `bundle.py` warns when a pack's labels are wider in its own reader font. Arabic-script and Hebrew labels are derived again in the bundle, measured shaped (HarfBuzz) in each reader font of the language. The device cuts a label that still overflows.
- **Hinting**: bundle fonts render like the built-in ones (FreeType, 150 DPI, 2-bit). Fonts marked `"hinting": "autohint"` in `fonts.lock.json` (the Noto CJK fonts, which carry no hinting instructions) use the FreeType auto-hinter, so thin horizontal strokes land on one dark pixel row instead of two gray ones.
- **Hindi, Malayalam**: no runtime shaping engine on the device. `tools/bible/shape.py` shapes the text with HarfBuzz when the bundle is built. Each shaped cluster (a syllable with its marks) becomes one glyph in the Unicode private-use plane 15 (U+F0000-), rendered into the bundle's fonts. The cluster table is `data/shaping/<code>.txt` (one cluster per line, line n = U+F0000+n), committed so builds are reproducible. The UI stays in English for these two.

## Licenses

- Open Translation Bible: CC BY-SA 4.0, attribution "Open Translation Bible (https://openbible.uk), CC BY-SA 4.0". Converted packs are distributed under the same license.
- 개역한글 (1952/1961): public domain (`data/bible/README.md`).
- Fonts: SIL OFL 1.1 (Noto, RIDIBatang, Pretendard, Vazirmatn, Heebo) or the KoPub font terms. Each bundle carries the licenses of its fonts in `/bible/fonts/<code>/LICENSES/`.
