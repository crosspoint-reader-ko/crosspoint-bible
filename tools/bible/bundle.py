#!/usr/bin/env python3
"""Build the per-language SD card bundles (docs/languages.md).

For every language in languages.json except the built-in English:

  dist/bundles/<code>/bible/lang/<code>/<id>.cpb             Bibles
  dist/bundles/<code>/bible/lang/<code>/README.txt           what this is, where to copy it
  dist/bundles/<code>/bible/lang/<code>/LICENSES/            Bible licenses (OTB CC BY-SA 4.0 ...)
  dist/bundles/<code>/bible/fonts/<code>/<label>/<pt>.epdfont reader fonts, sizes_pt
  dist/bundles/<code>/bible/fonts/<code>/ui/<ui pt>.epdfont   system font
  dist/bundles/<code>/bible/fonts/<code>/LICENSES/           font licenses
  dist/bundles/crosspoint-bible-<version>-<code>.zip         the tree above, rooted at the SD card root

Fonts are 2-bit, uncompressed, subset to the characters the bundle uses: its Bibles, the
UI strings of the language (lib/I18n/translations/*.yaml by _bcp47, English when there is
none yet), ASCII and common punctuation. Arabic-script bundles also carry the Arabic
Presentation Forms the firmware's MiniBidi shapes those letters into. Latin and Cyrillic
languages use the built-in Noto fonts, so their bundles hold only the Bible.

Hindi and Malayalam ("shaped": true) are pre-shaped with tools/bible/shape.py into
plane-15 private-use clusters. The table data/shaping/<code>.txt is append-only: a new
cluster is added at the end, so existing glyph code points never move. The UI of these two
stays English, so their bundles carry no UI font.

usage:
  bundle.py --otb OTB_CHECKOUT [--out dist/bundles] [--lang ko ja ...] [--jobs N] [--no-zip]
  bundle.py --otb OTB_CHECKOUT --english data/bible/otb-en.cpb    also rebuild the built-in English pack

Run with a Python that has freetype-py and fontTools (lib/EpdFont/scripts/venv), or
build/shaping-venv (adds uharfbuzz) for hi and ml.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# shape.py must be imported before anything that loads fontTools/uharfbuzz (see its header).
try:
    import shape  # noqa: E402
except Exception as e:  # missing module or uharfbuzz
    shape = None
    SHAPE_ERROR = str(e)
else:
    SHAPE_ERROR = ""

import argparse  # noqa: E402
import configparser  # noqa: E402
import json  # noqa: E402
import re  # noqa: E402
import shutil  # noqa: E402
import subprocess  # noqa: E402
import time  # noqa: E402
import unicodedata  # noqa: E402
import zipfile  # noqa: E402
from concurrent.futures import ProcessPoolExecutor  # noqa: E402

import epub2cpb  # noqa: E402
import fonts  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, os.pardir, os.pardir))
TRANSLATIONS = os.path.join(ROOT, "lib", "I18n", "translations")
BUILTIN_SCRIPTS = {"latin", "cyrillic", "greek"}
PUNCT = " «»·‐‑–—‘’‚“”„…‹›"
BASE_CHARS = {chr(c) for c in range(0x20, 0x7F)} | set(PUNCT)
# Not drawn as glyphs (layout controls); never reported as missing.
INVISIBLE = {0x200B, 0x200C, 0x200D, 0x200E, 0x200F, 0xFEFF, 0x00AD, 0x2060}
ARABIC_PF = (range(0xFB50, 0xFE00), range(0xFE70, 0xFEFF))
LAM_ALEF = range(0xFEF5, 0xFEFD)


def crosspoint_version():
    cp = configparser.ConfigParser(interpolation=None, strict=False)
    cp.read(os.path.join(ROOT, "platformio.ini"))
    return cp.get("crosspoint", "version")


def ui_strings(code):
    """(yaml file, all string values) of the translation whose _bcp47 is code, else English."""
    files = sorted(f for f in os.listdir(TRANSLATIONS) if f.endswith(".yaml")) if os.path.isdir(TRANSLATIONS) else []
    chosen = None
    for f in files:
        with open(os.path.join(TRANSLATIONS, f), encoding="utf-8") as fh:
            text = fh.read()
        m = re.search(r'^_bcp47:\s*"?([^"\n]+)"?', text, re.M)
        if m and m.group(1).strip().lower() == code:
            chosen = (f, text)
    if chosen is None and "english.yaml" in files:
        with open(os.path.join(TRANSLATIONS, "english.yaml"), encoding="utf-8") as fh:
            chosen = ("english.yaml", fh.read())
    if chosen is None:
        return None, []
    values = []
    for m in re.finditer(r'^[A-Za-z_][A-Za-z0-9_]*:\s*"((?:[^"\\]|\\.)*)"', chosen[1], re.M):
        values.append(re.sub(r"\\(.)", lambda x: {"n": "\n", "t": "\t"}.get(x.group(1), x.group(1)), m.group(1)))
    return chosen[0], values


def arabic_forms(chars):
    """Presentation-form code points MiniBidi can produce from the letters in chars."""
    out = set()
    for r in ARABIC_PF:
        for cp in r:
            ch = chr(cp)
            if unicodedata.name(ch, None) is None:
                continue
            base = unicodedata.normalize("NFKC", ch)
            if (len(base) == 1 and base in chars) or (cp in LAM_ALEF and set(base) <= chars):
                out.add(cp)
    return out


def otb_dir(otb, entry):
    return os.path.join(otb, "lang", entry["otb"])


def git_head(path):
    try:
        return subprocess.run(["git", "-C", path, "rev-parse", "HEAD"], capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


# --- Bibles


def build_otb(otb, entry, commit, title=None):
    """(verses, headings, names, abbrs, title, meta) of the language's OTB text, unshaped."""
    return epub2cpb.convert(otb_dir(otb, entry), entry["code"], title, {}, commit)


# Chapter and psalm titles for the pre-shaped languages, whose UI (and its formats) stays English.
SHAPED_FORMATS = {
    "hi": {"chapter_fmt": "अध्याय %d", "psalm_fmt": "भजन %d"},
    "ml": {"chapter_fmt": "അദ്ധ്യായം %d", "psalm_fmt": "സങ്കീർത്തനം %d"},
}


def shaped_bible(data, code):
    """Shape verses, headings, book names, abbreviations, title and edition with the reader table.

    The device draws all of these with the reader font (headers, book grid, bookmarks, Bible
    info), so they must use the reader font's clusters. New clusters are appended to the
    committed table (append-only, so builds stay reproducible). The UI stays English and the
    bundle has no UI font; language_name stays unshaped (the language list shows the English name).
    """
    verses, headings, names, abbrs, title, meta = data
    reader_stack = shape.default_font_stack(code, "reader")
    table = os.path.join(ROOT, "data", "shaping", f"{code}.txt")
    verses, headings, _ = shape.shape_pack_text(verses, headings, code, reader_stack, table)
    keys = [bk for bk in range(1, 67)]
    formats = SHAPED_FORMATS.get(code, {})
    strings = [names[bk] for bk in keys] + [title, meta.get("edition", "")] + list(formats.values())
    shaped = shape.shape_strings(strings, code, reader_stack, table)
    names = dict(zip(keys, shaped[:66]))
    # Abbreviations: the first clusters of the shaped names.
    abbrs = epub2cpb.derive_abbreviations(names)
    title = shaped[66]
    meta = dict(meta)
    if shaped[67]:
        meta["edition"] = shaped[67]
    for key, value in zip(formats, shaped[68:]):
        meta[key] = value  # ASCII "%d" survives shaping
    meta["shaped"] = "pua"
    clusters = shape.load_clusters(table)
    return (verses, headings, names, abbrs, title, meta), clusters


# --- Fonts


# Fallback fonts rendered into the same .epdfont for code points the primary font lacks
# (Vazirmatn has no U+FBA4, the isolated heh with yeh above that MiniBidi can produce).
FALLBACKS = {"Vazirmatn": ["NotoSansArabic"]}


def _render(job):
    font_path, size, cps, out = job
    t = time.time()
    missing = fonts.render_epdfont(font_path, size, cps, out)
    return out, missing, time.time() - t


def _render_shaped(job):
    clusters, stack, sizes, out_dir = job
    t = time.time()
    shape.build_cluster_fonts(clusters, stack, sizes, out_dir)
    return out_dir, [], time.time() - t


def copy_licenses(font_ids, dest, cache):
    os.makedirs(dest, exist_ok=True)
    for fid in font_ids:
        _, licenses = fonts.fetch(fid, cache)
        for p in licenses:
            shutil.copyfile(p, os.path.join(dest, f"{fid}-{os.path.basename(p)}"))


OTB_NOTICE = """{edition} ({code})
Open Translation Bible (https://openbible.uk), CC BY-SA 4.0.
Source: {source}

This pack is a conversion of the Open Translation Bible text into the CrossPoint Bible
pack format (.cpb): paragraph and poetry marks kept, psalm titles as headings, the
Markdown poetry marker removed, an editorial note in Psalm 78:16 dropped. It is
distributed under the same license, Creative Commons Attribution-ShareAlike 4.0
International: https://creativecommons.org/licenses/by-sa/4.0/legalcode
"""

KRV_NOTICE = """개역한글 (Korean Revised Version, 1952/1961)
Public domain. Source: getBible "korean" (https://api.getbible.net/v2/korean.json),
version 2.0.1 (2019-01-07), from Wikisource.
"""

README = """CrossPoint Bible {version} - {name} ({code})

Copy the "bible" folder of this zip to the root of the reader's SD card (merge it with
an existing "bible" folder). Then choose the language in Settings > System > Language.

Contents
{contents}
Licenses: bible/lang/{code}/LICENSES/ (Bibles){font_licenses}.
"""


def zip_tree(src_root, zip_path):
    tmp = zip_path + ".part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dirpath, dirnames, filenames in os.walk(src_root):
            dirnames.sort()
            for f in sorted(filenames):
                full = os.path.join(dirpath, f)
                arc = os.path.relpath(full, src_root).replace(os.sep, "/")
                info = zipfile.ZipInfo(arc, date_time=(2026, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o644 << 16
                with open(full, "rb") as fh:
                    z.writestr(info, fh.read(), compresslevel=9)
    os.replace(tmp, zip_path)


def tree_size(path):
    return sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(path) for f in fs)


def builtin_cmap():
    """Approximate coverage of the built-in reader font the firmware falls back to:
    Noto Serif plus the Hebrew letters (lib/EpdFont/scripts/convert-noto-fonts.sh)."""
    p = os.path.join(ROOT, "lib", "EpdFont", "builtinFonts", "source", "NotoSerif", "NotoSerif-Regular.ttf")
    return (fonts.cmap(p) if os.path.exists(p) else set()) | set(range(0x05D0, 0x05EB))


def build_language(entry, cfg, args, pool):
    code = entry["code"]
    t0 = time.time()
    stage = os.path.join(args.out, code)
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    lang_dir = os.path.join(stage, "bible", "lang", code)
    font_dir = os.path.join(stage, "bible", "fonts", code)
    report = {"code": code, "missing": {}, "notes": []}

    if entry.get("shaped") and shape is None:
        report["skipped"] = f"tools/bible/shape.py not usable ({SHAPE_ERROR}); run with build/shaping-venv"
        return report

    # Bibles
    bibles = entry.get("bibles") or [{"id": "otb"}]
    built = []  # (id, path, pack dict)
    clusters = None
    for b in bibles:
        out = os.path.join(lang_dir, f"{b['id']}.cpb")
        if b.get("source"):
            src = os.path.join(ROOT, b["source"])
            data = epub2cpb.convert(src, code)
        else:
            data = build_otb(args.otb, entry, args.otb_commit, b.get("title"))
        if entry.get("shaped"):
            data, clusters = shaped_bible(data, code)
        verses, headings, names, abbrs, title, meta = data
        pack, _ = epub2cpb.build_pack(verses, headings, names, title, code, meta, abbrs)
        os.makedirs(lang_dir, exist_ok=True)
        with open(out, "wb") as f:
            f.write(pack)
        built.append((b["id"], out, epub2cpb.read_pack(pack)))

    # Characters
    yaml_name, ui = ui_strings(code)
    chars = set(BASE_CHARS) | set(entry["name"])
    for _, _, p in built:
        chars |= epub2cpb.pack_charset(p)
    for s in ui:
        chars.update(s)
    chars = {c for c in chars if c >= " " and c != "\x7f"}
    cps = {ord(c) for c in chars}
    if code == "ko":
        # Every KS X 1001 syllable, so text typed on the keyboard (bookmark folders) renders.
        cps |= {c for c in range(0xAC00, 0xD7A4) if len(chr(c).encode("euc-kr", "ignore")) == 2}
    if entry["script"] == "arabic":
        cps |= arabic_forms(chars)
    report["ui_yaml"] = yaml_name
    report["chars"] = len(chars)

    # Fonts
    sizes = cfg["sizes_pt"]
    ui_pt = cfg["ui_size_pt"]
    reader = [r for r in entry["reader"] if r["font"] not in fonts.BUILTIN]
    ui_font = entry["ui"] if entry["ui"] not in fonts.BUILTIN else None
    jobs, shaped_jobs, font_ids = [], [], []
    if entry.get("shaped"):
        for r in reader:
            shaped_jobs.append((clusters, shape.default_font_stack(code, "reader"), sizes,
                                os.path.join(font_dir, r["label"])))
            font_ids += shape.FONT_STACKS[code]["reader"]
        # No UI font: the UI stays English (built-in Noto Sans).
        report["notes"].append(f"{len(clusters)} reader clusters; UI text English, no UI font")
    else:
        for r in reader:
            path, _ = fonts.fetch(r["font"], args.cache)
            font_ids.append(r["font"])
            if r["font"] in FALLBACKS:
                path = [path] + [fonts.fetch(f, args.cache)[0] for f in FALLBACKS[r["font"]]]
                font_ids += FALLBACKS[r["font"]]
            for pt in sizes:
                jobs.append((path, pt, cps, os.path.join(font_dir, r["label"], f"{pt}.epdfont")))
        if ui_font and (reader or entry["script"] not in BUILTIN_SCRIPTS):
            path, _ = fonts.fetch(ui_font, args.cache)
            font_ids.append(ui_font)
            if ui_font in FALLBACKS:
                path = [path] + [fonts.fetch(f, args.cache)[0] for f in FALLBACKS[ui_font]]
                font_ids += FALLBACKS[ui_font]
            jobs.append((path, ui_pt, cps, os.path.join(font_dir, "ui", f"{ui_pt}.epdfont")))
    results = list(pool.map(_render, jobs)) + list(pool.map(_render_shaped, shaped_jobs))
    builtin = builtin_cmap()
    for out, missing, _ in results:
        rel = os.path.relpath(out, font_dir)
        text_missing = [c for c in missing if c not in INVISIBLE and chr(c) in chars]
        forms_missing = [c for c in missing if chr(c) not in chars]
        if text_missing:
            report["missing"][rel] = text_missing
        if forms_missing:
            report.setdefault("missing_forms", {})[rel] = forms_missing
    report["builtin_fallback"] = builtin
    if font_ids:
        copy_licenses(sorted(set(font_ids)), os.path.join(font_dir, "LICENSES"), args.cache)

    # Bible licenses and README
    lic = os.path.join(lang_dir, "LICENSES")
    os.makedirs(lic, exist_ok=True)
    contents = []
    for bid, path, p in built:
        m = p["meta"]
        title = p["title"]
        if entry.get("shaped"):
            title = shape.unshape(title, clusters)
        if "Open Translation Bible" in m.get("source", ""):
            with open(os.path.join(lic, f"{bid}-CC-BY-SA-4.0.txt"), "w", encoding="utf-8") as f:
                f.write(OTB_NOTICE.format(edition=title, code=entry["otb"], source=m["source"]))
        elif bid == "krv1961":
            with open(os.path.join(lic, f"{bid}-public-domain.txt"), "w", encoding="utf-8") as f:
                f.write(KRV_NOTICE)
        contents.append(f"  bible/lang/{code}/{bid}.cpb  {title} ({m.get('license', '')})")
    for r in entry["reader"]:
        if r["font"] not in fonts.BUILTIN:
            contents.append(f"  bible/fonts/{code}/{r['label']}/  reader font {r['font']}, {', '.join(map(str, sizes))} pt")
    if os.path.isdir(os.path.join(font_dir, "ui")):
        contents.append(f"  bible/fonts/{code}/ui/  system font {entry['ui']}, {ui_pt} pt")
    with open(os.path.join(lang_dir, "README.txt"), "w", encoding="utf-8") as f:
        f.write(README.format(version=args.version, name=entry["name"], code=code, contents="\n".join(contents) + "\n",
                              font_licenses=f" and bible/fonts/{code}/LICENSES/ (fonts)" if font_ids else ""))

    report["unpacked"] = tree_size(stage)
    report["packs"] = {bid: os.path.getsize(p) for bid, p, _ in built}
    report["fonts"] = tree_size(font_dir) if os.path.isdir(font_dir) else 0
    if not args.no_zip:
        z = os.path.join(args.out, f"crosspoint-bible-{args.version}-{code}.zip")
        zip_tree(stage, z)
        report["zip"] = os.path.getsize(z)
    report["seconds"] = time.time() - t0
    return report


def fmt(n):
    return f"{n / 1e6:.2f} MB" if n >= 1e5 else f"{n / 1e3:.0f} KB"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--otb", required=True, help="Open Translation Bible checkout (has lang/<code>/)")
    ap.add_argument("--out", default=os.path.join(ROOT, "dist", "bundles"))
    ap.add_argument("--lang", nargs="*", help="language codes (default: every non-built-in language)")
    ap.add_argument("--cache", default=fonts.DEFAULT_CACHE)
    ap.add_argument("--jobs", type=int, default=os.cpu_count())
    ap.add_argument("--no-zip", action="store_true")
    ap.add_argument("--english", help="also write the built-in English pack (en-GB) to this path")
    args = ap.parse_args()
    args.otb = os.path.abspath(args.otb)
    args.otb_commit = git_head(args.otb)
    args.version = crosspoint_version()
    cfg = epub2cpb.load_languages()
    entries = [e for e in cfg["languages"] if not e.get("builtin")]
    if args.lang:
        unknown = set(args.lang) - {e["code"] for e in cfg["languages"]}
        if unknown:
            raise SystemExit(f"unknown language codes: {sorted(unknown)}")
        entries = [e for e in entries if e["code"] in args.lang]
    os.makedirs(args.out, exist_ok=True)
    print(f"CrossPoint Bible {args.version}, OTB {args.otb_commit or '(no git)'}, {len(entries)} languages")

    if args.english:
        en = epub2cpb.language_entry("en", cfg)
        data = build_otb(args.otb, en, args.otb_commit)
        verses, headings, names, abbrs, title, meta = data
        pack, _ = epub2cpb.build_pack(verses, headings, names, title, "en", meta, abbrs)
        with open(args.english, "wb") as f:
            f.write(pack)
        print(f"{args.english}: {fmt(len(pack))}")

    reports = []
    total_t = time.time()
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        for entry in entries:
            r = build_language(entry, cfg, args, pool)
            reports.append(r)
            if r.get("skipped"):
                print(f"{entry['code']:3} skipped: {r['skipped']}")
                continue
            print(f"{entry['code']:3} packs {', '.join(f'{k} {fmt(v)}' for k, v in r['packs'].items())}; "
                  f"fonts {fmt(r['fonts'])}; unpacked {fmt(r['unpacked'])}; zip {fmt(r.get('zip', 0))}; "
                  f"{r['chars']} chars ({r['ui_yaml']}); {r['seconds']:.1f}s")
            for note in r["notes"]:
                print(f"    {note}")
            groups = {}
            for rel, cps in r["missing"].items():
                groups.setdefault(tuple(cps), []).append(rel)
            for cps, rels in groups.items():
                cps = list(cps)
                rel = ", ".join(sorted({os.path.dirname(x) or x for x in rels}))
                fb = [c for c in cps if c in r["builtin_fallback"]]
                shown = " ".join(f"U+{c:04X}{'' if unicodedata.category(chr(c)).startswith('M') else ' ' + chr(c)}"
                                 for c in cps[:20])
                print(f"    missing in {rel}: {len(cps)} ({len(fb)} in built-in Noto Serif): {shown}"
                      f"{' ...' if len(cps) > 20 else ''}")
            for rel, cps in r.get("missing_forms", {}).items():
                print(f"    presentation forms missing in {rel}: {len(cps)}: "
                      f"{' '.join(f'U+{c:04X}' for c in cps[:24])}{' ...' if len(cps) > 24 else ''}")
    done = [r for r in reports if not r.get("skipped")]
    print(f"total: unpacked {fmt(sum(r['unpacked'] for r in done))}, zips {fmt(sum(r.get('zip', 0) for r in done))}, "
          f"{time.time() - total_t:.0f}s")
    with open(os.path.join(args.out, "report.json"), "w", encoding="utf-8") as f:
        json.dump([{k: v for k, v in r.items() if k != "builtin_fallback"} for r in reports], f, ensure_ascii=False,
                  indent=1)


if __name__ == "__main__":
    main()
