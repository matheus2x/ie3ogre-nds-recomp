#!/usr/bin/env python3
"""Text drawn on the game's images, for a translation mod (formats in tools/ntr/image.py).

  python3 tools/image.py export boej.pak mods/CyberLifePlus build/images [--only 'pic2d/menu/MMSlot*'] [--retail]
  python3 tools/image.py check boej.pak mods/CyberLifePlus translations/pt-BR [--png build/images/check]
  python3 tools/image.py apply boej.pak mods/CyberLifePlus translations/pt-BR

export writes each picture of the archives (pic2d .SPF_, pic3d .SPL/.SPD) as an indexed PNG,
<archive>/<entry>.png, to look at; it is game data, so only under build/.

A translation keeps its images in images.json: archive -> entry -> list of edits on the
retail picture (pixel coordinates, as export --retail shows it; for a 2D picture,
{"width": cells, "edits": [...]} fixes the width that export guessed):

  "pic3d/option": {"OPDN_B01.PAC_": [
      {"frames": [0, 1], "erase": [19, 2, 140, 20], "from": 162},
      {"frames": [0, 1], "text": "Vídeos", "box": [19, 5, 138, 14], "colour": 15, "shadow": 7}]}

erase takes out the old text: each pixel of an "ink" colour in the rectangle takes the colour
of the nearest pixel of the row that is not ink; without "ink", the whole row takes the colour
of its pixel in column "from" (by default the one left of the rectangle). With "under"
("archive:entry", a retail picture with the same background and the text elsewhere), an ink
pixel takes that picture's pixel when it is not ink. text is drawn with
a font of the mod (FONT12 by default; "font": "FONT8"), centred in box ("align": "left"), one
letter after the other with "gap" pixels between them (1), rows ("\\n") "line" pixels apart
(the font's height), with a shadow to the right and below ("shadow", a colour) or a line
all around ("outline"). The box
centres the font's cells of 12 rows (the capitals fill rows 3-9). Colours are palette
indices, as export shows them (for a 4 bpp 2D picture, within the 16 colours of the cell).
"frames" repeats an edit on frames of a 3D picture, with x, y, "from" counted from each
frame's corner. check lists what does not fit (--png writes the edited pictures to look
at). apply puts the edited pictures in place of the mod's (the rest of each archive stays
as the mod has it) and writes the archives as patches over the retail files; since the
edits start from the retail pictures, apply can run again.
"""

import argparse
import fnmatch
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import language  # noqa: E402
import text as texttool  # noqa: E402
from ntr import lz10  # noqa: E402
from ntr.image import Glyphs, ImageError, Picture, read_spf, read_spl, write_spf, write_spl  # noqa: E402
from ntr.png import PngError  # noqa: E402
from ntr.text import codes, encode, fit_font  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DATA = "data_iz"
SHADOW = ((1, 0), (0, 1), (1, 1))
OUTLINE = tuple((dx, dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1) if dx or dy)


def archives(game: texttool.Game) -> list[str]:
    """pic2d .SPF_, pic3d archives (without .SPL/.SPD) and lone .pac_ pictures, as paths under data_iz."""
    out = []
    for rel, _ in game.rom.files():
        rel = rel.lstrip("/")
        if not rel.startswith(DATA + "/pic"):
            continue
        rel = rel[len(DATA) + 1:]
        if rel.endswith(".SPF_") or rel.lower().endswith(".pac_"):
            out.append(rel)
        elif rel.endswith(".SPD"):
            out.append(rel[:-4])
    return sorted(out)


def load(get, arc: str) -> tuple[str, list[tuple[str, bytes]]]:
    """(kind, [(entry, PAC bytes, decompressed)]) of an archive, read with get (retail or modded)."""
    if arc.lower().endswith(".pac_"):  # one picture, its file name as the entry
        d = get(f"{DATA}/{arc}")
        return arc[3:5], [(arc.rsplit("/", 1)[-1], lz10.try_decompress(d) or d)]
    if arc.endswith(".SPF_"):
        return "2d", read_spf(get(f"{DATA}/{arc}"))
    entries = read_spl(get(f"{DATA}/{arc}.SPL"), get(f"{DATA}/{arc}.SPD"))
    return "3d", [(n, lz10.decompress(d) if d[:1] == b"\x10" else d) for n, d in entries]


def save(get, arc: str, new: dict[str, bytes]) -> dict[str, bytes]:
    """The files of an archive with some entries replaced: {path under data_iz: bytes}."""
    if arc.lower().endswith(".pac_"):
        return {f"{DATA}/{arc}": lz10.compress(new[arc.rsplit("/", 1)[-1]])}
    if arc.endswith(".SPF_"):
        return {f"{DATA}/{arc}": write_spf(get(f"{DATA}/{arc}"), new)}
    stored = dict(read_spl(get(f"{DATA}/{arc}.SPL"), get(f"{DATA}/{arc}.SPD")))
    packed = {n: lz10.compress(d) if stored[n][:1] == b"\x10" else d for n, d in new.items()}
    spl, spd = write_spl(get(f"{DATA}/{arc}.SPL"), get(f"{DATA}/{arc}.SPD"), packed)
    return {f"{DATA}/{arc}.SPL": spl, f"{DATA}/{arc}.SPD": spd}


def cmd_export(args) -> int:
    game = texttool.Game(args.image, args.dir)
    get = game.retail if args.retail else game.modded
    texttool.under_build(args.out)
    count = 0
    for arc in archives(game):
        if args.only and not fnmatch.fnmatch(arc, args.only):
            continue
        kind, entries = load(get, arc)
        for name, d in entries:
            if ".PAC" not in name.upper():
                continue
            try:
                pic = Picture(d, kind)
            except (ImageError, ValueError, IndexError, struct.error) as e:
                print(f"{arc}:{name}: skipped ({e})")
                continue
            out = args.out / arc / f"{name}.png"
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(pic.png(args.scale))
            size = f"{pic.cols} cells" if kind == "2d" else f"frames {pic.frames}"
            print(f"{arc}:{name}: {pic.w}x{pic.h}, {size}")
            count += 1
    print(f"{count} pictures in {args.out}")
    return 0


class Painter:
    def __init__(self, game: texttool.Game):
        self.game = game
        self.fonts: dict[str, Glyphs] = {}
        self.others: dict[str, Picture] = {}

    def other(self, ref: str) -> Picture:
        """A retail picture named "archive:entry" (the clean pixels for an erase)."""
        if ref not in self.others:
            arc, name = ref.rsplit(":", 1)
            kind, entries = load(self.game.retail, arc)
            self.others[ref] = Picture(dict(entries)[name], kind)
        return self.others[ref]

    def glyphs(self, name: str) -> Glyphs:
        if name not in self.fonts:
            self.fonts[name] = Glyphs(fit_font(self.game.modded(f"{DATA}/font/{name}.NFTR")))
        return self.fonts[name]

    def layout(self, e: dict) -> list[tuple[int, int, int]]:
        """(x, y, code) of each letter of a text edit."""
        g = self.glyphs(e.get("font", "FONT12"))
        gap, line = e.get("gap", 1), e.get("line", g.cell_h)
        bx, by, bw, bh = e["box"]
        rows = e["text"].split("\n")
        y = by + (bh - line * len(rows)) // 2
        out = []
        for row in rows:
            cs = list(codes(encode(row)))
            width = sum(g.font.advance(c) - 1 + gap for c in cs) - gap
            x = bx if e.get("align") == "left" else bx + (bw - width) // 2
            for c in cs:
                out.append((x, y, c))
                x += g.font.advance(c) - 1 + gap
            y += line
        return out

    def problems(self, pic: Picture, e: dict) -> list[str]:
        out = []
        if "text" in e:
            g = self.glyphs(e.get("font", "FONT12"))
            bx, by, bw, bh = e["box"]
            for x, y, c in self.layout(e):
                if c not in g.font.glyph:
                    out.append(f"no letter {c:#x} in {e.get('font', 'FONT12')}")
                    continue
                for r, row in enumerate(g.bitmap(c)):
                    for k, ink in enumerate(row):
                        px, py = x + g.left.get(g.font.glyph[c], 0) + k, y + r
                        if ink and not (bx <= px < bx + bw and by <= py < by + bh):
                            return out + [f"{e['text']!r} does not fit in {e['box']}"]
        for key in ("erase", "box"):
            if key in e:
                x, y, w, h = e[key]
                if x < 0 or y < 0 or x + w > pic.w or y + h > pic.h:
                    out.append(f"{key} {e[key]} outside the {pic.w}x{pic.h} picture")
        return out

    def paint(self, pic: Picture, e: dict):
        rows = pic.rows
        if "erase" in e:
            x0, y0, w, h = e["erase"]
            ink = set(e.get("ink", ()))
            for y in range(y0, y0 + h):
                row = rows[y]
                if not ink:
                    row[x0:x0 + w] = [row[e.get("from", x0 - 1)]] * w
                    continue
                old = row[:]
                under = self.other(e["under"]).rows[y] if "under" in e else None
                for x in range(x0, x0 + w):
                    if old[x] % 16 in ink if pic.bpp == 4 else old[x] in ink:
                        if under and (under[x] % 16 not in ink if pic.bpp == 4 else under[x] not in ink):
                            row[x] = under[x]
                            continue
                        for d in range(1, pic.w):
                            near = [k for k in (x - d, x + d) if 0 <= k < pic.w and
                                    (old[k] % 16 not in ink if pic.bpp == 4 else old[k] not in ink)]
                            if near:
                                row[x] = old[near[0]]
                                break
        if "text" in e:
            g = self.glyphs(e.get("font", "FONT12"))
            ink, shadow = [], []
            for x, y, c in self.layout(e):
                left = g.left.get(g.font.glyph[c], 0)
                for r, row in enumerate(g.bitmap(c)):
                    for k, v in enumerate(row):
                        if v:
                            ink.append((x + left + k, y + r))
            if "shadow" in e:
                shadow = [(x + dx, y + dy, e["shadow"]) for x, y in ink for dx, dy in SHADOW]
            if "outline" in e:
                shadow = [(x + dx, y + dy, e["outline"]) for x, y in ink for dx, dy in OUTLINE]
            for x, y, colour in shadow + [(x, y, e["colour"]) for x, y in ink]:
                if 0 <= x < pic.w and 0 <= y < pic.h:
                    rows[y][x] = pic.bank(x, y) + colour


def edits_of(folder: Path) -> dict:
    """The images.json of a translation folder (its language.json may name another file), with
    the language's letters active."""
    lang = language.load(folder).activate()
    if lang.images is None:
        return {}
    try:
        return json.loads(lang.images.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        sys.exit(f"error: {lang.images}: not valid JSON ({e})")


def placed(pic: Picture, e: dict) -> list[dict]:
    """An edit in picture coordinates, once per frame it names."""
    if "frames" not in e:
        return [e]
    out = []
    for f in e["frames"]:
        if not 0 <= f < len(pic.frames):
            raise ImageError(f"no frame {f} (the picture has {len(pic.frames)})")
        fx, fy, fw, fh = pic.frames[f]
        one = {k: v for k, v in e.items() if k != "frames"}
        for key in ("erase", "box"):
            if key in e:
                x, y, w, h = e[key]
                if x + w > fw or y + h > fh:
                    raise ImageError(f"{key} {e[key]} outside frame {f} ({fw}x{fh})")
                one[key] = [x + fx, y + fy, w, h]
        if "from" in e:
            one["from"] = e["from"] + fx
        out.append(one)
    return out


def pictures(game: texttool.Game, folder: Path):
    """(archive, {entry: edited Picture}, problems): the retail picture, drawn anew from a PNG
    of the language's pictures folder if it has one, then the edits of images.json."""
    painter = Painter(game)
    edits = edits_of(folder)
    pngs = language.load(folder).pictures
    for arc, _ in pngs:
        edits.setdefault(arc, {})
    for (arc, name), _ in pngs.items():
        edits[arc].setdefault(name, [])
    for arc, entries in edits.items():
        try:
            kind, stored = load(game.retail, arc)
        except (SystemExit, ImageError) as e:
            yield arc, {}, [f"{arc}: not an archive of pictures ({e})"]
            continue
        stored = dict(stored)
        done, bad = {}, []
        for name, spec in entries.items():
            if name not in stored:
                bad.append(f"{arc}: no entry {name}")
                continue
            width = spec.get("width") if isinstance(spec, dict) else None
            pic = Picture(stored[name], kind, width)
            if (arc, name) in pngs:
                try:
                    near = pic.draw_png(pngs[arc, name].read_bytes())
                except (ImageError, PngError) as e:
                    bad.append(f"{arc}:{name}: {e}")
                    continue
                if near:
                    print(f"{arc}:{name}: {near} pixels of colours not in its palette took the nearest")
            for edit in spec["edits"] if isinstance(spec, dict) else spec:
                try:
                    edits = placed(pic, edit)
                except ImageError as err:
                    bad.append(f"{arc}:{name}: {err}")
                    continue
                for e in edits:
                    bad += [f"{arc}:{name}: {p}" for p in painter.problems(pic, e)]
                    if not bad:
                        painter.paint(pic, e)
            done[name] = pic
        yield arc, done, bad


def cmd_check(args) -> int:
    return 1 if check(args.image, args.dir, args.translations, args.png, args.scale) else 0


def check(image: Path, mod: Path, folder: Path, png: Path | None = None, scale: int = 1) -> int:
    """How many problems the edited images of a translation have (png: write them there)."""
    game = texttool.Game(image, mod)
    total = count = 0
    if png:
        texttool.under_build(png)
    for arc, done, bad in pictures(game, folder):
        for p in bad:
            print(p)
        for name, pic in done.items() if png else ():
            out = png / arc / f"{name}.png"
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(pic.png(scale))
        total += len(bad)
        count += len(done)
    print(f"{count} pictures, {total} problems")
    return total


def cmd_apply(args) -> int:
    return apply(args.image, args.dir, args.translations)


def apply(image: Path, mod: Path, folder: Path) -> int:
    """Write the edited images of a translation into the mod as patches; 1 if some have problems."""
    game = texttool.Game(image, mod)
    failed = False
    for arc, done, bad in pictures(game, folder):
        if bad:
            for p in bad:
                print(p)
            failed = True
            continue
        new = {name: pic.encode() for name, pic in done.items()}
        for rel, data in save(game.modded, arc, new).items():
            if texttool.write_file(mod, game, rel, data):
                print(f"{rel}: changed")
    if failed:
        print("some archives were not written: fix the problems first")
        return 1
    print(f"{mod}: done; raise the version in mod.ini and check with tools/mod.py check")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("export", help="the pictures as PNG, to look at (writes under build/)")
    p.add_argument("image", type=Path, help="boej.pak or the retail .nds")
    p.add_argument("dir", type=Path, help="the mod folder")
    p.add_argument("out", type=Path, help="a folder under build/")
    p.add_argument("--only", help="archives, e.g. 'pic2d/menu/MMSlot*' or 'pic3d/menu_MovieView'")
    p.add_argument("--retail", action="store_true", help="the retail pictures, not the mod's")
    p.add_argument("--scale", type=int, default=1)
    p.set_defaults(fn=cmd_export)
    for name, fn, text in (("check", cmd_check, "what in the translated images does not fit"),
                           ("apply", cmd_apply, "write the translated images into the mod, as patches")):
        p = sub.add_parser(name, help=text)
        p.add_argument("image", type=Path)
        p.add_argument("dir", type=Path)
        p.add_argument("translations", type=Path)
        if name == "check":
            p.add_argument("--png", type=Path, help="write the edited pictures here (under build/)")
            p.add_argument("--scale", type=int, default=1)
        p.set_defaults(fn=fn)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
