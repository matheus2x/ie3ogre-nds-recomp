#!/usr/bin/env python3
"""A translation mod made from its folder, translations/<language>, with nothing but .json and .png
to edit (how to translate: docs/modding/TRANSLATING.md).

  python3 tools/translate.py new es --mod Traduccion-ES --name "Traducción al español" --author "Yo"
  python3 tools/translate.py todo es [--only 3300] [--reference pt-BR]
  python3 tools/translate.py images es [--only 'pic3d/option'] [--retail]
  python3 tools/translate.py check es
  python3 tools/translate.py build es [--install] [--zip]

new starts a language on the game itself: language.json, the letters of another language (pt-BR
by default) with their drawing as a layer over the game's fonts, and an empty texts/; with
--duplicate pt-BR, a copy of the whole language instead (its texts, images, fonts and base). todo lists, under build/, what is still to translate:
each place with the room it has, the Japanese and the reference language's text, and .json files
with the reference's text to copy into texts/ and translate. images writes the pictures, as the
base has them (--retail: as the game), as PNG under build/ to draw anew into images/. check tells what does not fit (fonts,
lines, images) without writing the mod. build makes the mod in build/translate/<mod>: the base
(the game, or translations/bases/<base>), the fonts, the lines and the images, as .bps patches, and its mod.ini;
--install puts it in mods/<mod> (the files there besides files/, overlays/ and mod.ini stay),
--zip makes the .zip that players put in the mods folder.

The game's image is boej.pak (next to the game, or build/boej.pak in the repository) or the retail
.nds: --image to give another.
"""

import argparse
import contextlib
import io
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import font as fonttool  # noqa: E402
import image as imagetool  # noqa: E402
import language  # noqa: E402
import mod as modtool  # noqa: E402
import text as texttool  # noqa: E402
from ntr.font import FontError, Nftr  # noqa: E402
from ntr.text import TextError, japanese_count  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
TRANSLATIONS = ROOT / "translations"
OUT = ROOT / "build" / "translate"
IMAGES = (ROOT / "boej.pak", ROOT / "build" / "boej.pak", Path.cwd() / "boej.pak")
REFERENCE = "pt-BR"


def find_image(given: Path | None) -> Path:
    if given:
        if not given.exists():
            sys.exit(f"error: {given} does not exist")
        return given
    for p in IMAGES:
        if p.exists():
            return p
    sys.exit("error: no boej.pak found (it is next to the game once the launcher prepared it): "
             "--image boej.pak or --image the retail .nds")


def folder_of(name: str) -> Path:
    """translations/<name>, or the folder given as a path."""
    p = Path(name)
    return p if (p / language.MANIFEST).exists() else TRANSLATIONS / name


def load(name: str) -> language.Language:
    folder = folder_of(name)
    if not (folder / language.MANIFEST).exists():
        sys.exit(f"error: {folder} has no {language.MANIFEST} (start one with: translate.py new <language>)")
    return language.load(folder)


def mod_ini(lang: language.Language) -> str:
    info = {"name": lang.mod, **lang.info}
    rel = lang.folder.resolve().relative_to(ROOT).as_posix() if lang.folder.resolve().is_relative_to(ROOT) \
        else lang.folder.name
    return (f"; Gerado pelo tools/translate.py a partir de {rel}/{language.MANIFEST}: mude lá.\n[mod]\n"
            + "".join(f"{k} = {v}\n" for k, v in info.items())
            + f"game = BOEJ\ntype = {language.TYPE}\nlanguage = {lang.code}\n")


def quietly(fn, *args) -> tuple[int, str]:
    """(result, what it printed) of a step whose output only matters when it fails."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        result = fn(*args)
    return result, out.getvalue()


def stage(lang: language.Language, image: Path, out: Path) -> None:
    """In out, the mod's base and the language's fonts, with its mod.ini: what the lines and
    images are written over."""
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    for kind in ("files", "overlays"):
        if lang.base and (lang.base / kind).is_dir():
            shutil.copytree(lang.base / kind, out / kind)
    (out / "files").mkdir(exist_ok=True)
    (out / "mod.ini").write_text(mod_ini(lang), encoding="utf-8")
    game = texttool.Game(image, out)
    try:
        fonts = fonttool.built(lang.activate(), game.modded)
    except FontError as e:
        sys.exit(f"error: font {e}")
    for rel, data in fonts.items():
        texttool.write_file(out, game, rel, data)


def report(step: str, bad: int, printed: str) -> None:
    lines = [ln for ln in printed.splitlines() if ln.strip()]
    if bad:
        print(f"== {step}: problems")
        print("\n".join(ln for ln in lines if "changed" not in ln and "done;" not in ln))
    else:
        summary = [ln for ln in lines if " ok," in ln or "pictures," in ln]
        print(f"{step}: {summary[-1] if summary else 'ok'}")


def steps(lang: language.Language, image: Path, out: Path, write: bool) -> int:
    """Fonts, lines and images of the language into the mod staged in out; the problems found."""
    print(f"{lang.code}: {len(lang.texts)} files of lines, {len(lang.fonts)} fonts, "
          f"{len(lang.pictures)} pictures, text on images {'yes' if lang.images else 'no'}, base {lang.base.name if lang.base else 'none (the game)'}")
    bad = 0
    try:
        n, printed = quietly(texttool.apply if write else texttool.check, image, out, lang.folder)
        report("lines", n, printed)
        bad += n
        if lang.images or lang.pictures:
            n, printed = quietly(imagetool.apply if write else imagetool.check, image, out, lang.folder)
            report("images", n, printed)
            bad += n
    except TextError as e:
        print(f"!! {e}")
        bad += 1
    return bad


def cmd_check(args) -> int:
    lang, image = load(args.language), find_image(args.image)
    out = OUT / ".check" / lang.mod
    stage(lang, image, out)
    bad = steps(lang, image, out, False)
    print("ok: build it with translate.py build" if not bad else "fix the problems above (docs/modding/TRANSLATING.md)")
    return 1 if bad else 0


def install(out: Path, dst: Path) -> None:
    """The built mod in mods/<mod>: files/, overlays/ and mod.ini replaced, the rest kept."""
    dst.mkdir(parents=True, exist_ok=True)
    for kind in ("files", "overlays"):
        if (dst / kind).exists():
            shutil.rmtree(dst / kind)
        if (out / kind).exists():
            shutil.copytree(out / kind, dst / kind)
    shutil.copy2(out / "mod.ini", dst / "mod.ini")


def cmd_build(args) -> int:
    lang, image = load(args.language), find_image(args.image)
    out = OUT / lang.mod
    stage(lang, image, out)
    if steps(lang, image, out, True):
        print("nothing built: fix the problems above (docs/modding/TRANSLATING.md)")
        return 1
    errors, warnings = modtool.check(out, modtool.load_image(image))
    for w in warnings:
        print(f"warning: {w}")
    for e in errors:
        print(f"error: {e}")
    if errors:
        return 1
    files = sum(1 for _ in modtool.game_files(out))
    print(f"{out}: {files} patches, version {lang.info.get('version', '?')}")
    if args.install:
        dst = ROOT / "mods" / lang.mod
        install(out, dst)
        print(f"{dst}: installed (turn it on in the launcher's Mods page)")
    if args.zip:
        z = OUT / f"{lang.mod}-{lang.info.get('version', '0')}.zip"
        modtool.pack(out, z)
        print(f"{z} ({z.stat().st_size // 1024} KB): put it in the mods folder next to the game")
    return 0


def write_manifest(folder: Path, m: dict) -> None:
    body = ",\n".join(f"  {json.dumps(k)}: {json.dumps(v, ensure_ascii=False)}" for k, v in m.items())
    (folder / language.MANIFEST).write_text("{\n" + body + "\n}\n", encoding="utf-8")


def cmd_new(args) -> int:
    folder = TRANSLATIONS / args.language
    if args.language == language.BASES or "/" in args.language or "\\" in args.language:
        sys.exit(f"error: {args.language!r} cannot be the name of a language")
    if folder.exists():
        sys.exit(f"error: {folder} already exists")
    info = {"language": args.language, "mod": args.mod, "name": args.name or args.mod, "version": "0.1",
            "author": args.author or "", "description": args.description or ""}
    if args.duplicate:
        # the whole language, its texts, images, fonts and base included, to change bit by bit
        src = load(args.duplicate)
        shutil.copytree(src.folder, folder)
        m = json.loads((folder / language.MANIFEST).read_text(encoding="utf-8"))
        write_manifest(folder, {"type": language.TYPE, **{k: v for k, v in m.items() if k != "type"}, **info})
        print(f"{folder}: a copy of {src.code} ({len(src.texts)} files of lines, base "
              f"{src.base.name if src.base else 'the game'}). Translate the texts (translate.py todo "
              f"{args.language} lists the ones still as in {src.code}), then translate.py check {args.language}.")
        return 0
    # on the game itself, with the Latin letters of another language as a layer over the game's fonts
    src = load(args.copy)
    m = {"type": language.TYPE, **info}
    m["charset"] = {ch: f"{b:02X}" for ch, b in src.charset.items()}
    m |= {"spacer": f"{src.spacer:02X}", "space": f"{src.space:02X}", "plain": src.plain,
          "texts": "texts/*.json", "images": "images.json", "pictures": "images", "fonts": "font"}
    (folder / "texts").mkdir(parents=True)
    names = fonttool.names()
    for name, (j, png) in src.fonts.items():
        f = Nftr.from_json(j.read_text(encoding="utf-8"))
        f.glyphs = f.read_sheet(png.read_bytes())
        if not f.layer:
            f = f.subset(fonttool.LATIN)
        (folder / "font").mkdir(exist_ok=True)
        (folder / "font" / f"{name}.png").write_bytes(f.sheet())
        (folder / "font" / f"{name}.json").write_text(f.to_json(names), encoding="utf-8")
    write_manifest(folder, m)
    print(f"{folder}: language.json, the Latin letters of {src.code} as a layer over the game's fonts (font/) "
          f"and texts/ (empty); the mod is made from the Japanese game.\n"
          f"Next: change the letters of \"charset\" to the ones of your language (and draw them in font/),\n"
          f"then translate.py todo {args.language} to see what to translate.")
    return 0


def cmd_images(args) -> int:
    lang, image = load(args.language), find_image(args.image)
    staged = OUT / ".check" / lang.mod
    stage(lang, image, staged)
    out = OUT / lang.code / "images"
    if out.exists():
        shutil.rmtree(out)
    imagetool.cmd_export(argparse.Namespace(image=image, dir=staged, out=out, only=args.only,
                                            retail=args.retail, scale=1))
    print(f"Copy the ones you draw anew to {lang.folder / 'images'}, in the same folders "
          f"(images/<archive>/<entry>.png); the text on an image can also go in images.json.")
    return 0


def lines_of(lang: language.Language) -> dict[str, tuple[str, str]]:
    """place -> (file name, text) of a language."""
    out = {}
    for f in lang.texts:
        for sid, t in json.loads(f.read_text(encoding="utf-8")).items():
            out[sid] = (f.name, t)
    return out


def cmd_todo(args) -> int:
    lang, ref = load(args.language), load(args.reference)
    image = find_image(args.image)
    done, theirs = lines_of(lang), lines_of(ref)
    if lang.folder.resolve() == ref.folder.resolve():
        theirs = {}  # the reference itself: what only the base has, and (--japanese) what is still Japanese
    texttool.under_build(OUT)
    out = OUT / lang.code / "todo"
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    staged = OUT / ".check" / lang.mod
    stage(lang, image, staged)
    ref.activate()  # the slots read the mod's bytes as the reference language
    only = set(args.only.split(",")) if args.only else None
    game = texttool.Game(image, staged)
    by_id = {s.id: s for s in texttool.slots(game)}
    if ref.base != lang.base:  # the reference's places are positions in another layout
        moved = texttool.place_map(texttool.Game(image, ref.base or ref.folder / "no-base"), game)
        theirs = {moved.get(sid, sid): v for sid, v in theirs.items()}
        lost = [sid for sid in theirs if sid not in by_id]
        for sid in lost:
            del theirs[sid]
        print(f"{len(moved)} places of {ref.code} are elsewhere on this base"
              + (f"; {len(lost)} have no place here" if lost else ""))
    # the reference's lines; the ones only the base has (its check does not take them as they
    # are); with --japanese, the ones still in Japanese
    want = {sid: (fname, text, "") for sid, (fname, text) in theirs.items() if text}
    for sid, s in by_id.items():
        if sid in theirs or sid in done or not s.current.strip():
            continue
        if not japanese_count(s.current) and s.current != s.source:
            want[sid] = (texttool.file_of(s), texttool.as_written(s, s.current), "only in the base: check does not take it as it is")
        elif args.japanese and japanese_count(s.current):
            want[sid] = (texttool.file_of(s), "", "still in Japanese")
    groups: dict[str, list[str]] = {}
    files: dict[str, dict[str, str]] = {}
    for sid, (fname, text, note) in want.items():
        if sid in done and done[sid][1] != text:
            continue  # translated
        s = by_id.get(sid)
        group = s.group if s else fname.removesuffix(".json")
        if only and not {group, fname.removesuffix(".json"), sid.split(":")[0]} & only:
            continue
        show = texttool.visible if s and s.table else texttool.decode
        jp = show(s.source) if s and japanese_count(s.source) else ""
        block = [f"### {sid}  ({s.capacity if s else '?'} bytes){'  ' + note if note else ''}"] \
            + ([f"  JP: {jp}"] if jp else []) + ([f"  {ref.code}: {text}"] if text else [])
        groups.setdefault(group, []).append("\n".join(block))
        # a place still in Japanese keeps it (check refuses it until translated; "" would skip it)
        files.setdefault(fname, {})[sid] = text or jp
    for group, blocks in sorted(groups.items()):
        (out / f"{group}.txt").write_text("\n".join(blocks) + "\n", encoding="utf-8")
    for fname, lines in sorted(files.items()):
        (out / fname).write_text(json.dumps(lines, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    total = sum(len(v) for v in files.values())
    print(f"{out}: {total} places still to translate, {len(done)} done")
    for group, blocks in sorted(groups.items()):
        print(f"  {group}: {len(blocks)}")
    print(f"The .json files there have the {ref.code} text (or the Japanese): copy them to {lang.folder / 'texts'}, "
          f"translate the texts and run translate.py check {lang.code}.")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("new", help="start a language")
    p.add_argument("language", help="its code, like es or en-US: the folder translations/<language>")
    p.add_argument("--mod", required=True, help="the name of the mod's folder (no commas)")
    p.add_argument("--name", help="the name players see in the launcher")
    p.add_argument("--author")
    p.add_argument("--description")
    p.add_argument("--copy", default=REFERENCE, help=f"the language whose Latin letters to start from ({REFERENCE})")
    p.add_argument("--duplicate", metavar="LANGUAGE",
                   help="copy a whole language instead (texts, images, fonts and its base), to change bit by bit")
    p.set_defaults(fn=cmd_new)
    p = sub.add_parser("todo", help="what is still to translate (writes under build/)")
    p.add_argument("language")
    p.add_argument("--reference", default=REFERENCE, help=f"the language to translate from ({REFERENCE})")
    p.add_argument("--only", help="groups or files, e.g. 3300,item,overlays")
    p.add_argument("--japanese", action="store_true", help="also the places still in Japanese in every language")
    p.set_defaults(fn=cmd_todo)
    p = sub.add_parser("images", help="the pictures as PNG to draw anew (writes under build/)")
    p.add_argument("language")
    p.add_argument("--only", help="archives, e.g. 'pic3d/option' or 'pic2d/menu/MMSlot*'")
    p.add_argument("--retail", action="store_true", help="the game's pictures, not the base's")
    p.set_defaults(fn=cmd_images)
    p = sub.add_parser("check", help="what does not fit, without writing the mod")
    p.add_argument("language")
    p.set_defaults(fn=cmd_check)
    p = sub.add_parser("build", help="make the mod (patches, mod.ini)")
    p.add_argument("language")
    p.add_argument("--install", action="store_true", help="put it in mods/<mod>")
    p.add_argument("--zip", action="store_true", help="make the .zip to share")
    p.set_defaults(fn=cmd_build)
    for p in sub.choices.values():
        if p.prog.split()[-1] != "new":
            p.add_argument("--image", type=Path, help="boej.pak or the retail .nds")
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
