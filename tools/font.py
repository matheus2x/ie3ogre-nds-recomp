#!/usr/bin/env python3
"""The fonts of a translation as PNG sheets (formats in tools/ntr/font.py; docs/modding/TRANSLATING.md).

  python3 tools/font.py export boej.pak mods/CyberLifePlus translations/pt-BR/font
  python3 tools/font.py export boej.pak mods/CyberLifePlus translations/es/font --layer
  python3 tools/font.py check translations/pt-BR

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
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import language  # noqa: E402
import text as texttool  # noqa: E402
from ntr import text as ntrtext  # noqa: E402
from ntr.font import FontError, Nftr  # noqa: E402
from ntr.png import PngError  # noqa: E402

FONT_DIR = "data_iz/font"
FONTS = ("FONT12", "FONT12T", "FONT8", "RUBI8")
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
    print(f"{len(lang.fonts)} fonts, {bad} with problems")
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
    args = ap.parse_args()
    try:
        return args.fn(args)
    except FontError as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    sys.exit(main())
