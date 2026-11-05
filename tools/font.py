#!/usr/bin/env python3
"""The fonts of a translation as PNG sheets (formats in tools/ntr/font.py; docs/modding/TRANSLATING.md).

  python3 tools/font.py export boej.pak mods/CyberLifePlus translations/pt-BR/font
  python3 tools/font.py export boej.pak mods/CyberLifePlus translations/es/font --layer
  python3 tools/font.py check translations/pt-BR
  python3 tools/font.py freeze translations/pt-BR

export writes each font of a mod (data_iz/font/<NAME>.NFTR) as <NAME>.png, the glyphs to draw,
and <NAME>.json, the code and widths of each glyph, in the order of the sheet. A font the mod
does not change is the game's: export writes it only under build/ (to start a font from it).
With --layer, only the Latin letters (ASCII, the one-byte places A1-DF and the full-width
letters, digits and punctuation): a layer ("layer": true in the JSON) that tools/translate.py draws over the
font of the mod's base, so a language on the game itself keeps the game's kana and kanji
without carrying them. A translation keeps its fonts in the folder "fonts" of its language.json
(font/ by default); check builds them and tells what is wrong.

In the sheet, dark pixels are ink (black; a 2 bpp font has greys too) and light ones are not;
the tinted columns are outside the glyph's width (left + width in the JSON), the red lines the
grid. To add a letter, draw it in the cell of its byte (one of the charset in language.json)
and set its width and advance in the JSON.

hd_files (used by tools/translate.py build) makes what a mod needs for HD text: hd/fonts.json,
the character of each glyph of the game's font (as the mod builds it) for each TrueType font
of "hd_fonts" in language.json; settings.json, the mod's "Text font" option (the game's
letters by default, or one of those fonts); and the font files with their licences.

freeze writes metrics.lock in the folder of the fonts: the cell, baseline, line height and
(left, width, advance) of each code, what the fitting of every line depends on (line breaks,
the objectives' spacers, the px widths of tables and menus). From then on check refuses a font
whose metrics differ, so a font can be drawn anew without a translated line having to be redone.
A letter added is fine (freeze again to take it in); to change a metric on purpose, freeze again
and pass tools/translate.py check whole.
"""

import argparse
import json
import sys
import unicodedata
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import language  # noqa: E402
import text as texttool  # noqa: E402
from ntr import text as ntrtext  # noqa: E402
from ntr.font import FontError, Nftr  # noqa: E402
from ntr.png import PngError  # noqa: E402

FONT_DIR = "data_iz/font"
FONTS = ("FONT12", "FONT12T", "FONT8", "RUBI8")
LOCK = "metrics.lock"  # in the folder of the fonts (freeze)
SETTINGS = "settings.json"  # the mod's options (the port's ntr/mod_settings.h)
LATIN = (set(range(0x20, 0x7F)) | set(range(0xA1, 0xE0)) | set(range(0x8140, 0x81FD))  # what --layer takes
         | set(range(0x824F, 0x829B)))


def names() -> dict[int, str]:
    """Code -> character of what the active language writes in one byte, and ASCII."""
    return {c: chr(c) for c in range(0x20, 0x7F)} | {b: ch for ch, b in ntrtext.ACCENTS.items()
                                                      if ch != ntrtext.SPACER}


def build(json_path: Path, png_path: Path, base: bytes | None = None) -> bytes:
    """The NFTR of a font of a translation; a layer is drawn over base (the font under it)."""
    try:
        f = Nftr.from_json(json_path.read_text(encoding="utf-8"))
        f.glyphs = f.read_sheet(png_path.read_bytes())
        if f.layer:
            if base is None:
                raise FontError("a layer needs the font under it")
            f = Nftr.read(base).with_layer(f)
        return f.write()
    except FileNotFoundError as e:
        raise FontError(f"{e.filename} is missing") from None
    except (FontError, PngError) as e:
        raise FontError(f"{json_path.stem}: {e}") from None


def built(lang: language.Language, base) -> dict[str, bytes]:
    """data_iz/font/<NAME>.NFTR -> bytes, for each font of the language (base(path): the file
    under the language, for a layer)."""
    return {f"{FONT_DIR}/{name}.NFTR": build(j, p, base(f"{FONT_DIR}/{name}.NFTR")) for name, (j, p) in lang.fonts.items()}


def codepoints(f: Nftr, lang: language.Language) -> list[int]:
    """The character of each glyph of a font, for its HD version (0: none, a blank or a code
    that is no letter of the language, like the fan translation's ligatures). One byte: ASCII
    or the language's charset; two: Shift-JIS, full width taken to its plain letter (the
    fonts of the translations draw those proportional)."""
    by_byte = {b: ch for ch, b in lang.charset.items()}
    out = [0] * len(f.glyphs)
    for code, g in sorted(f.codes.items(), reverse=True):  # the lowest code of a glyph wins
        if code in (lang.spacer, lang.space):
            continue
        if code < 0x80:
            ch = chr(code)
        elif code < 0x100:
            ch = by_byte.get(code, "")
        else:
            try:
                ch = bytes((code >> 8, code & 0xFF)).decode("cp932")
            except UnicodeDecodeError:
                ch = ""
        ch = unicodedata.normalize("NFKC", ch)
        out[g] = ord(ch) if len(ch) == 1 and ch.isprintable() and not ch.isspace() else 0
    return out


def font_id(title: str) -> str:
    """The id of a font in hd/fonts.json and settings.json: "Atkinson Hyperlegible" -> "atkinson-hyperlegible"."""
    plain = unicodedata.normalize("NFKD", title).encode("ascii", "ignore").decode().lower()
    return "-".join("".join(c if c.isalnum() else " " for c in plain).split()) or "font"


def hd_settings(titles: list[str]) -> dict:
    """settings.json of a mod with HD fonts: the option that picks one, the game's letters by default."""
    return {"settings": [{
        "id": "hd_font",
        "label": {"en": "Text font (HD)", "pt": "Fonte do texto (HD)"},
        "hint": {"en": "Drawn at the high resolution, with the 3D resolution above 1×. The text stays in the same place.",
                 "pt": "Desenhada na resolução alta, com a resolução do 3D acima de 1×. O texto fica no mesmo lugar."},
        "default": "default",
        "options": [{"id": "default", "label": {"en": "Default", "pt": "Padrão"}}]
                   + [{"id": font_id(t), "label": t} for t in titles]}]}


def hd_files(lang: language.Language, font_bytes) -> dict[str, bytes]:
    """hd/fonts.json, settings.json and the TrueType files (with their licences) of the
    language's "hd_fonts", for its mod; font_bytes(NAME) is the NFTR the mod gives the game.
    Empty without "hd_fonts"."""
    if not lang.hd_fonts:
        return {}
    out, sets = {f"hd/{p.name}": p.read_bytes() for p in lang.hd_licences}, []
    points = {}  # the characters of a font of the game do not depend on the TrueType font
    for title, fonts in lang.hd_fonts.items():
        if font_id(title) == "default" or font_id(title) in (font_id(t) for t, _ in sets):
            raise FontError(f'hd_fonts: "{title}" has the id of another choice ({font_id(title)})')
        doc = {}
        for name, ttf in sorted(fonts.items()):
            rel = f"hd/{ttf.name}"
            out[rel] = ttf.read_bytes()
            if name not in points:
                points[name] = codepoints(Nftr.read(font_bytes(name)), lang)
            doc[name] = {"file": rel, "codepoints": points[name]}
        sets.append((title, doc))
    lines = []
    for title, doc in sets:
        fonts = ",\n".join(f'      "{n}": {json.dumps(v, separators=(",", ":"))}' for n, v in doc.items())
        lines.append(f'  "{font_id(title)}": {{"name": {json.dumps(title, ensure_ascii=False)}, "fonts": {{\n{fonts}\n  }}}}')
    out["hd/fonts.json"] = ("{\n" + ",\n".join(lines) + "\n}\n").encode("utf-8")
    out["settings.json"] = (json.dumps(hd_settings([t for t, _ in sets]), ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    return out


def lock_path(lang: language.Language) -> Path | None:
    """Where the frozen metrics of the language's fonts go (None: it has no fonts)."""
    return next(iter(lang.fonts.values()))[0].parent / LOCK if lang.fonts else None


def metrics(lang: language.Language) -> dict[str, dict]:
    """NAME -> Nftr.metrics() of each font of the language, from its JSON (no ROM, no sheet)."""
    out = {}
    for name, (j, _) in lang.fonts.items():
        try:
            out[name] = Nftr.from_json(j.read_text(encoding="utf-8")).metrics()
        except FontError as e:
            raise FontError(f"{name}: {e}") from None
    return out


def write_lock(fonts: dict[str, dict]) -> str:
    """The lock as JSON with one code a line, so a diff shows which letter changed."""
    parts = []
    for name, m in sorted(fonts.items()):
        head = ", ".join(f'"{k}": {json.dumps(m[k])}' for k in ("cell", "baseline", "line"))
        glyphs = ",\n".join(f'      "{c}": {json.dumps(w)}' for c, w in m["glyphs"].items())
        parts.append(f'  "{name}": {{{head}, "glyphs": {{\n{glyphs}\n  }}}}')
    return "{\n" + ",\n".join(parts) + "\n}\n"


def metric_changes(lock: dict[str, dict], fonts: dict[str, dict]) -> tuple[list[str], list[str]]:
    """(changes, additions) of fonts against the lock: a change breaks the fitting of texts
    already checked; an addition (a font or code the lock does not have) does not."""
    bad, new = [], []
    for name, m in sorted(fonts.items()):
        if name not in lock:
            new.append(f"{name}: not in {LOCK}")
            continue
        ref = lock[name]
        for k in ("cell", "baseline", "line"):
            if m[k] != ref[k]:
                bad.append(f"{name}: {k} {ref[k]} -> {m[k]}")
        changed = [f"{c} {ref['glyphs'][c]} -> {w}" for c, w in m["glyphs"].items()
                   if c in ref["glyphs"] and ref["glyphs"][c] != w]
        gone = [c for c in ref["glyphs"] if c not in m["glyphs"]]
        added = [c for c in m["glyphs"] if c not in ref["glyphs"]]
        if changed:
            bad.append(f"{name}: (left, width, advance) differ in {len(changed)} codes: {'; '.join(changed[:8])}"
                       + (" ..." if len(changed) > 8 else ""))
        if gone:
            bad.append(f"{name}: {len(gone)} codes without a glyph now: {' '.join(gone[:16])}")
        if added:
            new.append(f"{name}: {len(added)} new codes: {' '.join(added[:16])}")
    bad += [f"{name}: the font is gone" for name in sorted(set(lock) - set(fonts))]
    return bad, new


def check_lock(lang: language.Language) -> int:
    """Print what differs from the frozen metrics; the number of problems."""
    path = lock_path(lang)
    if path is None or not path.exists():
        if path is not None:
            print(f"(no {LOCK}: the metrics are not frozen; tools/font.py freeze {lang.folder})")
        return 0
    try:
        bad, new = metric_changes(json.loads(path.read_text(encoding="utf-8")), metrics(lang))
    except (FontError, json.JSONDecodeError, KeyError, TypeError) as e:
        print(f"!! {path}: {e}")
        return 1
    for line in bad:
        print(f"!! {line}")
    for line in new:
        print(f"{line} (fine; freeze again to take them in)")
    if bad:
        print(f"!! the metrics differ from {path}: lines already fitted may no longer fit. Undo the change, "
              f"or freeze again and pass tools/translate.py check {lang.folder}")
    return len(bad)


def cmd_freeze(args) -> int:
    lang = language.load(args.language).activate()
    path = lock_path(lang)
    if path is None:
        sys.exit(f"error: {args.language} has no fonts")
    fonts = metrics(lang)
    if path.exists():
        bad, new = metric_changes(json.loads(path.read_text(encoding="utf-8")), fonts)
        for line in bad + new:
            print(line)
    path.write_text(write_lock(fonts), encoding="utf-8")
    print(f"{path}: the metrics of {len(fonts)} fonts ({sum(len(m['glyphs']) for m in fonts.values())} codes)")
    return 0


def cmd_export(args) -> int:
    game = texttool.Game(args.image, args.dir)
    if args.language:
        language.load(args.language).activate()
    wanted = args.only.split(",") if args.only else FONTS
    for name in wanted:
        rel = f"{FONT_DIR}/{name}.NFTR"
        data = game.modded(rel)
        if data == game.retail(rel):
            if not args.retail:
                print(f"{name}: the mod does not change it (--retail to export the game's, under build/)")
                continue
            texttool.under_build(args.out)  # the game's font is game data
        f = Nftr.read(data)
        if args.layer:
            f = f.subset(LATIN)
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / f"{name}.png").write_bytes(f.sheet())
        (args.out / f"{name}.json").write_text(f.to_json(names()), encoding="utf-8")
        print(f"{args.out / name}.png/.json: {len(f.glyphs)} glyphs of {f.cell[0]}x{f.cell[1]}, {f.depth} bpp")
    return 0


def cmd_check(args) -> int:
    lang = language.load(args.language).activate()
    under = texttool.Game(args.image, lang.base) if lang.base else None  # without a base, the game's own
    bad = 0
    for name, (j, p) in lang.fonts.items():
        rel = f"{FONT_DIR}/{name}.NFTR"
        try:
            if under is None:
                under = texttool.Game(args.image, lang.folder / "no-base")
            f = Nftr.read(build(j, p, under.modded(rel)))
        except FontError as e:
            print(f"!! {e}")
            bad += 1
            continue
        missing = [ch for ch, b in ntrtext.ACCENTS.items() if ch != ntrtext.SPACER and b not in f.codes]
        if missing and name.startswith("FONT"):
            print(f"!! {name}: no glyph for {' '.join(missing)} (the charset of {lang.code})")
            bad += 1
        print(f"{name}: {len(f.glyphs)} glyphs")
    bad += check_lock(lang)
    print(f"{len(lang.fonts)} fonts, {bad} problems")
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("export", help="the fonts of a mod as PNG + JSON")
    p.add_argument("image", type=Path, help="boej.pak or the retail .nds")
    p.add_argument("dir", type=Path, help="the mod folder")
    p.add_argument("out", type=Path, help="where to write NAME.png and NAME.json")
    p.add_argument("--only", help="fonts, e.g. FONT12,FONT8")
    p.add_argument("--retail", action="store_true", help="also the fonts the mod does not change (under build/)")
    p.add_argument("--language", type=Path, help="name the glyphs with the letters of this translation")
    p.add_argument("--layer", action="store_true", help="only the Latin letters, as a layer over the base's font")
    p.set_defaults(fn=cmd_export)
    p = sub.add_parser("check", help="build the fonts of a translation and tell what is wrong")
    p.add_argument("language", type=Path, help="the translation folder (translations/<language>)")
    p.add_argument("--image", type=Path, default=Path("build/boej.pak"),
                   help="boej.pak or the retail .nds (a layer is drawn over the game's font)")
    p.set_defaults(fn=cmd_check)
    p = sub.add_parser("freeze", help=f"record the metrics of a translation's fonts ({LOCK}) for check")
    p.add_argument("language", type=Path, help="the translation folder (translations/<language>)")
    p.set_defaults(fn=cmd_freeze)
    args = ap.parse_args()
    try:
        return args.fn(args)
    except FontError as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    sys.exit(main())
