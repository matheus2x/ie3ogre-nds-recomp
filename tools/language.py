"""The language of a translation: translations/<language>/language.json (docs/modding/TRANSLATING.md).

  {
    "type": "translation",
    "language": "pt-BR",
    "mod": "CyberLifePlus",
    "base": "CyberLife",
    "name": "CyberLifePlus (tradução PT-BR)", "version": "1.15", "author": "...", "description": "...",
    "charset": {"à": "B1", "á": "B2", ...},
    "spacer": "DF", "space": "CF",
    "plain": {"…": "...", "“": "\\""},
    "texts": "texts/*.json",
    "images": "images.json",
    "pictures": "images",
    "fonts": "font",
    "hd_fonts": {"Font Name": {"FONT12": "hd/Font.ttf", "FONT12T": "hd/Font.ttf", "FONT8": "hd/Font.ttf"}}
  }

charset is the letters of the language that are not ASCII, each on one of the free bytes
A1-DF (where the half-width kana were; the font of the mod draws them there). spacer is the
blank of no width and space the 4-pixel space of the subtitles (both drawn blank by the font);
plain what to write in place of characters the font lacks. texts are the .json files of the
lines (globs, from this folder), images the file of the text drawn on images (tools/image.py),
fonts the folder of the fonts (<NAME>.json + <NAME>.png for data_iz/font/<NAME>.NFTR; tools/font.py),
hd_fonts the TrueType fonts (from this folder; under a free licence that lets the
mod carry them) the game's fonts can be drawn with above native resolution, by name: each
is a choice of the mod's "Text font" option (the first one is the game's own letters). The
mod gets hd/fonts.json, settings.json and the files, with the licence next to each font
(OFL.txt, LICENSE.txt...; tools/font.py hd_files; docs/modding/MODS.md, "HD text").
pictures the folder of whole pictures drawn anew, <archive>/<entry>.png as tools/image.py export
writes them (images/pic3d/option/OPDN_B01.PAC_.png); images.json edits are drawn over them.
name, version, author and description go into the mod.ini of the mod; mod is its folder
under mods/. base is the layer the mod is built on, translations/bases/<base> (patches as in a
mod, files/ and overlays/): without base, the game itself (a new language); CyberLife is the fan
translation's 1.0, under PT-BR and the languages copied from it (the places of their .json files
are positions in its layout; tools/text.py place_map turns them into the game's).

A folder without language.json is read as before it existed: PT-BR, its .json files the lines
and images.json the images.
"""

import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

from ntr import text

MANIFEST = "language.json"
IMAGES = "images.json"
TYPE = "translation"
BASES = "bases"  # translations/bases/<name>: the layers a mod is built on
KEYS = {"type", "language", "mod", "base", "name", "version", "author", "description", "charset", "spacer", "space",
        "plain", "texts", "images", "pictures", "fonts", "hd_fonts", "glossary"}
MOD_INI = ("name", "version", "author", "description")  # what goes into the mod.ini
HD_FONTS = ("FONT12", "FONT12T", "FONT8", "RUBI8")  # the game's fonts the port draws in HD
LICENCE_NAMES = ("OFL", "LICENSE", "LICENCE", "COPYING")


@dataclass
class Language:
    folder: Path
    code: str = "pt-BR"
    mod: str = ""
    base: Path | None = None  # translations/bases/<name>
    info: dict[str, str] = field(default_factory=dict)  # name, version, author, description
    charset: dict[str, int] = field(default_factory=lambda: dict(text.LETTERS))
    spacer: int = text.SPACER_BYTE
    space: int = text.FAN_SPACE
    plain: dict[str, str] = field(default_factory=lambda: dict(text.PLAIN))
    texts: list[Path] = field(default_factory=list)
    images: Path | None = None
    fonts: dict[str, tuple[Path, Path]] = field(default_factory=dict)  # NAME -> (json, png)
    pictures: dict[tuple[str, str], Path] = field(default_factory=dict)  # (archive, entry) -> png
    hd_fonts: dict[str, dict[str, Path]] = field(default_factory=dict)  # font name -> NAME of a game font -> file
    hd_licences: list[Path] = field(default_factory=list)  # next to them (OFL.txt, LICENSE.txt...)
    manifest: bool = False  # read from a language.json

    def activate(self) -> "Language":
        """encode/decode write and read this language's letters from now on."""
        text.set_charset(self.charset, self.spacer, self.space, self.plain)
        return self


def _fail(path: Path, msg: str):
    sys.exit(f"error: {path}: {msg}")


def _byte(path: Path, what: str, v) -> int:
    try:
        b = int(v, 16) if isinstance(v, str) else -1
    except ValueError:
        b = -1
    if not 0 <= b <= 0xFF:
        _fail(path, f"{what}: {v!r} is not a byte in hexadecimal (like \"B1\")")
    return b


def load(folder: Path) -> Language:
    """The language of a translation folder (with or without language.json)."""
    folder = Path(folder)
    path = folder / MANIFEST
    if not path.exists():
        return Language(folder, texts=sorted(f for f in folder.glob("*.json") if f.name != IMAGES),
                        images=folder / IMAGES if (folder / IMAGES).exists() else None)
    try:
        m = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        _fail(path, f"not valid JSON ({e})")
    if not isinstance(m, dict):
        _fail(path, "the manifest is a JSON object, {...}")
    if unknown := set(m) - KEYS:
        _fail(path, f"unknown keys {', '.join(sorted(unknown))} (known: {', '.join(sorted(KEYS))})")
    if m.get("type", TYPE) != TYPE:
        _fail(path, f'"type" is "{TYPE}"')
    for key in ("language", "mod"):
        if not isinstance(m.get(key), str) or not m[key]:
            _fail(path, f'"{key}" is missing')
    if any(c in m["mod"] for c in "/\\,") or m["mod"] in (".", ".."):
        _fail(path, '"mod" is the name of the mod folder (no slashes or commas)')
    lang = Language(folder, code=m["language"], mod=m["mod"], info={k: str(m[k]) for k in MOD_INI if k in m},
                    manifest=True)
    if "base" in m:
        lang.base = folder.parent / BASES / str(m["base"])
        if not (lang.base / "files").is_dir():
            _fail(path, f'base "{m["base"]}" is not a folder of translations/{BASES} with files/')
    if "charset" in m:
        if not isinstance(m["charset"], dict):
            _fail(path, '"charset" is {"letter": "byte", ...}')
        lang.charset = {ch: _byte(path, f"charset {ch}", b) for ch, b in m["charset"].items()}
    if "spacer" in m:
        lang.spacer = _byte(path, "spacer", m["spacer"])
    if "space" in m:
        lang.space = _byte(path, "space", m["space"])
    if "plain" in m:
        if not isinstance(m["plain"], dict) or not all(isinstance(v, str) for v in m["plain"].values()):
            _fail(path, '"plain" is {"character": "what to write", ...}')
        lang.plain = dict(m["plain"])
    images = m.get("images", IMAGES)
    lang.images = folder / images if (folder / images).exists() else None
    globs = m.get("texts", "*.json")
    files = set()
    for g in [globs] if isinstance(globs, str) else globs:
        files.update(f for f in folder.glob(g) if f.is_file() and f.name != MANIFEST and f != lang.images)
    fonts = folder / m.get("fonts", "font")
    lang.fonts = {j.stem: (j, j.with_suffix(".png")) for j in sorted(fonts.glob("*.json"))} if fonts.is_dir() else {}
    files -= {j for j, _ in lang.fonts.values()}
    pics = folder / m.get("pictures", "images")
    if pics.is_dir():
        lang.pictures = {(f.parent.relative_to(pics).as_posix(), f.stem): f for f in sorted(pics.rglob("*.png"))}
    hd = m.get("hd_fonts", {})
    if not isinstance(hd, dict) or not all(isinstance(v, dict) and all(isinstance(f, str) for f in v.values())
                                           for v in hd.values()):
        _fail(path, '"hd_fonts" is {"Font Name": {"FONT12": "hd/Font.ttf", ...}, ...}')
    for title, fonts in hd.items():
        if not title.strip() or not fonts:
            _fail(path, f"hd_fonts: {title!r} needs a name and fonts")
        for name, rel in fonts.items():
            if name not in HD_FONTS:
                _fail(path, f'hd_fonts: {title}: {name} is not one of {", ".join(HD_FONTS)}')
            if not (folder / rel).is_file() or (folder / rel).suffix.lower() not in (".ttf", ".otf"):
                _fail(path, f"hd_fonts: {title}: {rel} is not a .ttf or .otf file in {folder}")
            lang.hd_fonts.setdefault(title, {})[name] = folder / rel
            files.discard(folder / rel)
    for d in sorted({p.parent for fonts in lang.hd_fonts.values() for p in fonts.values()}):
        lang.hd_licences += sorted(p for p in d.iterdir() if p.is_file() and p.suffix.lower() in (".txt", ".md")
                                   and p.stem.upper().startswith(LICENCE_NAMES))
    if lang.hd_fonts and not lang.hd_licences:
        _fail(path, "hd_fonts: no licence next to the fonts (OFL.txt, LICENSE.txt...): the mod must carry it")
    lang.texts = sorted(files)
    try:
        text.check_charset(lang.charset, lang.spacer, lang.space)
    except text.TextError as e:
        _fail(path, str(e))
    return lang
