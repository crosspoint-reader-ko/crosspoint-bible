Xteink X4 / X3 (ESP32-C3) Bible firmware. English (Open Translation Bible) is built in; every other language is an SD card bundle below.

**Firmware**: `crosspoint-bible-*-x4-x3.bin`. Copy it to the SD card and flash it from Settings > System > SD Card Firmware Update, or over USB: `esptool.py --chip esp32c3 write_flash 0x10000 <file>`.

**Languages**: download `crosspoint-bible-*-<code>.zip` and unzip it at the root of the SD card. It creates `/bible/lang/<code>/` (the Bibles) and `/bible/fonts/<code>/` (fonts, where the script needs them). Then pick the language in the menu > Language. Several languages can share one card.

| code | language | Bibles |
|---|---|---|
| ko | 한국어 | 개역한글 (1952/1961, public domain), 열린 성경 |
| ja, zh | 日本語, 简体中文 | Open Translation Bible |
| ar, fa, he | العربية, فارسی, עברית | Open Translation Bible (right to left) |
| hi, ml | हिन्दी, മലയാളം | Open Translation Bible (pre-shaped; menus stay in English) |
| da, de, es, fi, fr, id, is, it, nb, nl, pt, ru, so, sv, sw | | Open Translation Bible |

**Licenses**: Open Translation Bible (https://openbible.uk), CC BY-SA 4.0. Fonts: SIL OFL 1.1 and the KoPub font terms, included in each bundle under `/bible/fonts/<code>/LICENSES/`.

`epub2cpb.py` converts your own EPUB or getBible JSON into a pack (see the README).
