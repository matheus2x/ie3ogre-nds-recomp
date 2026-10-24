#!/usr/bin/env python3
"""Tools for making mods (format in docs/MODS.md).

  python3 tools/mod.py new mods/minha-traducao --name "Minha tradução"
  python3 tools/mod.py unpack boej.pak build/original [--only 'data_iz/logic/*'] [--overlays]
  python3 tools/mod.py from-dump retail.nds modificado.nds mods/traducao --name "Tradução" [--patch]
  python3 tools/mod.py patch mods/traducao boej.pak
  python3 tools/mod.py expand mods/traducao boej.pak build/traducao
  python3 tools/mod.py check mods/minha-traducao boej.pak     (or a .zip)
  python3 tools/mod.py pack mods/minha-traducao [minha-traducao.zip]

unpack gives the original files to edit (NitroFS paths under files/, overlays as they
are in RAM under overlays/). from-dump turns a modified dump (a ROM hack or fan
translation whose code is the retail one) into a mod: the files and overlays that differ.
patch turns the whole files of a mod into BPS patches (<path>.bps, only the changes, so
the mod carries no game data); expand does the opposite, for editing the files.
check looks for paths the game does not have and for overlays whose code changed; the
game does the same check when it loads the mod (boej --check-mods prints it).

The retail image may be the dump or the game data package (boej.pak).
"""

import argparse
import configparser
import fnmatch
import re
import sys
import tempfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ntr import bps  # noqa: E402
from ntr.pak import MAGIC, PakReader  # noqa: E402
from ntr.rom import Overlay, Rom  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
DSD_OVERLAYS = ROOT / "config" / "boej" / "dsd" / "arm9" / "overlays"
# DS Protect: encrypted in the ROM and decrypted by the game, so it cannot be compared or
# replaced.
PROTECTED_OVERLAYS = {129}
PATCH = ".bps"

MOD_INI = """\
; Mod de Inazuma Eleven 3 - The Ogre (formato em docs/MODS.md).
[mod]
name = {name}
version = 1.0
author = {author}
description = {description}
game = BOEJ
"""


def ini(text: str) -> dict[str, str]:
    """The [mod] section of a mod.ini (name, version, author, description, game)."""
    parser = configparser.RawConfigParser()
    parser.read_string(text)
    return dict(parser["mod"]) if parser.has_section("mod") else {}


def load_image(path: Path) -> Rom:
    """A dump, or a game data package rebuilt into a ROM image (0xFF where it has nothing)."""
    data = path.read_bytes()
    if data[:8] != MAGIC:
        return Rom(data)
    pak = PakReader(data)
    image = bytearray(b"\xff" * pak.rom_size)
    for start, length, at in pak.ranges:
        image[start:start + length] = data[at:at + length]
    return Rom(bytes(image))


def code_ranges(ov: Overlay) -> list[tuple[int, int]]:
    """[start, end) of the functions of an overlay, from the ds-decomp symbols (RAM addresses).

    The game checks with the exact ranges the recompiler used; these are the same function
    bodies, which hold the literal pools and jump tables.
    """
    path = DSD_OVERLAYS / f"ov{ov.id:03d}" / "symbols.txt"
    if not path.exists():
        return []
    out = []
    for line in path.read_text().splitlines():
        m = re.search(r"kind:function\(\w+,size=(0x[0-9a-f]+)\) addr:(0x[0-9a-f]+)", line)
        if m:
            start = int(m[2], 16)
            out.append((start, start + int(m[1], 16)))
    return sorted(out)


def code_change(ov: Overlay, retail: bytes, image: bytes) -> int | None:
    """RAM address of the first code byte that differs, or None."""
    padded = image + retail[len(image):]
    for start, end in code_ranges(ov):
        for a in range(start - ov.ram_address, min(end - ov.ram_address, len(retail))):
            if padded[a] != retail[a]:
                return ov.ram_address + a
    return None


_file_ids: dict[int, tuple[Rom, dict[str, int]]] = {}


def file_ids(rom: Rom) -> dict[str, int]:
    """NitroFS path (lower case) -> file id, worked out once per image."""
    if id(rom) not in _file_ids:
        _file_ids[id(rom)] = (rom, {p.lower(): i for p, i in rom.files()})
    return _file_ids[id(rom)][1]


def original(rom: Rom, rel: str) -> bytes | None:
    """The retail bytes that a path of a mod replaces ("files/<path>" or "overlays/ovNNN.bin")."""
    kind, _, name = rel.partition("/")
    if kind == "files":
        file_id = file_ids(rom).get(name.lower())
        return None if file_id is None else rom.file_data(file_id)
    m = re.fullmatch(r"ov(\d{3})\.bin", name.lower())
    ov = next((o for o in rom.overlays9() if m and o.id == int(m[1])), None)
    return None if ov is None or ov.id in PROTECTED_OVERLAYS else rom.overlay_decompressed(ov)


def game_files(mod: Path):
    """(relative path, file) of what the game reads in a mod folder, patches included."""
    for kind in ("files", "overlays"):
        for f in sorted((mod / kind).rglob("*")) if (mod / kind).is_dir() else []:
            if f.is_file():
                yield f.relative_to(mod).as_posix(), f


def safe_name(path: str) -> bool:
    return not (path.startswith("/") or ".." in path.split("/") or "\\" in path)


# --- commands -------------------------------------------------------------------------

def cmd_new(args) -> int:
    out: Path = args.dir
    if out.exists() and any(out.iterdir()):
        sys.exit(f"error: {out} already exists and is not empty")
    if "," in out.name:
        sys.exit("error: the folder name cannot have commas")
    (out / "files").mkdir(parents=True, exist_ok=True)
    (out / "overlays").mkdir(exist_ok=True)
    (out / "mod.ini").write_text(MOD_INI.format(name=args.name or out.name, author=args.author or "",
                                                description=args.description or ""), encoding="utf-8")
    print(f"created {out}: edit mod.ini, put files under files/ (same paths as in the game)")
    return 0


def cmd_unpack(args) -> int:
    rom = load_image(args.image)
    out: Path = args.out
    count = 0
    for path, file_id in rom.files():
        if args.only and not any(fnmatch.fnmatch(path, pattern) for pattern in args.only):
            continue
        dst = out / "files" / path
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(rom.file_data(file_id))
        count += 1
    print(f"{count} files in {out / 'files'}")
    if args.overlays:
        (out / "overlays").mkdir(parents=True, exist_ok=True)
        for ov in rom.overlays9():
            if ov.id in PROTECTED_OVERLAYS:
                continue
            (out / "overlays" / f"ov{ov.id:03d}.bin").write_bytes(rom.overlay_decompressed(ov))
        print(f"overlays in {out / 'overlays'} (only data may change, see docs/MODS.md)")
    return 0


def write(dst: Path, base: bytes, data: bytes, patch: bool):
    """The file of a mod: whole, or as a patch over the retail bytes (<path>.bps)."""
    dst.parent.mkdir(parents=True, exist_ok=True)
    if patch:
        dst.with_name(dst.name + PATCH).write_bytes(bps.encode(base, data))
    else:
        dst.write_bytes(data)


def cmd_from_dump(args) -> int:
    retail, other = load_image(args.retail), load_image(args.modified)
    if retail.header.game_code != other.header.game_code:
        sys.exit(f"error: {args.modified} is {other.header.game_code}, not {retail.header.game_code}")
    if retail.arm9_decompressed() != other.arm9_decompressed():
        sys.exit("error: the ARM9 of the modified dump is not the retail one; mods cannot change it")
    out: Path = args.out
    if out.exists() and any(out.iterdir()):
        sys.exit(f"error: {out} already exists and is not empty")

    base_files = dict(retail.files())
    other_files = dict(other.files())
    changed = 0
    for path, file_id in other_files.items():
        if path not in base_files:
            print(f"warning: {path} is not in the retail game (left out)")
            continue
        data, base = other.file_data(file_id), retail.file_data(base_files[path])
        if data == base:
            continue
        write(out / "files" / path, base, data, args.patch)
        changed += 1
    for path in base_files.keys() - other_files.keys():
        print(f"warning: {path} was removed in the modified dump (the mod keeps the original)")

    overlays = 0
    for ob, oo in zip(retail.overlays9(), other.overlays9(), strict=True):
        a, b = retail.overlay_decompressed(ob), other.overlay_decompressed(oo)
        if a == b:
            continue
        if ob.id in PROTECTED_OVERLAYS:
            print(f"note: overlay {ob.id} (copy protection) differs; it cannot be part of a mod (left out)")
            continue
        at = code_change(ob, a, b)
        if at is not None:
            sys.exit(f"error: overlay {ob.id} changes code at {at:#010x}; mods may only change data")
        # The slot keeps its original size; a shorter overlay is completed with the
        # original bytes (the game loads the whole slot).
        write(out / "overlays" / f"ov{ob.id:03d}.bin", a, b + a[len(b):], args.patch)
        overlays += 1

    out.mkdir(parents=True, exist_ok=True)
    (out / "mod.ini").write_text(MOD_INI.format(
        name=args.name or out.name, author=args.author or "",
        description=args.description or f"Feito a partir de {args.modified.name}"), encoding="utf-8")
    print(f"{out}: {changed} files and {overlays} overlays")
    return 0


def cmd_patch(args) -> int:
    """Whole files of the mod become patches over the retail ones; a whole file wins over a
    patch of the same path (it is the newer edit), and one equal to the original removes it."""
    rom = load_image(args.image)
    patched = same = 0
    for rel, f in list(game_files(args.dir)):
        if rel.endswith(PATCH):
            continue
        base = original(rom, rel)
        if base is None:
            print(f"warning: {rel} is not part of the game (left as it is)")
            continue
        data = f.read_bytes()
        if data == base or (rel.startswith("overlays/") and data == base[:len(data)]):
            print(f"note: {rel} is the original; removed (and its patch)")
            f.with_name(f.name + PATCH).unlink(missing_ok=True)
            same += 1
        else:
            write(f, base, data, True)
            patched += 1
        f.unlink()
    print(f"{args.dir}: {patched} files turned into patches" + (f", {same} originals removed" if same else ""))
    return 0


def cmd_expand(args) -> int:
    """A copy of the mod with every patch applied: whole files, to edit or to compare."""
    rom = load_image(args.image)
    out: Path = args.out
    if out.exists() and any(out.iterdir()):
        sys.exit(f"error: {out} already exists and is not empty")
    for f in sorted(args.dir.rglob("*")):
        if not f.is_file():
            continue
        rel = f.relative_to(args.dir).as_posix()
        dst = out / rel
        data = f.read_bytes()
        if rel.endswith(PATCH) and rel.startswith(("files/", "overlays/")):
            dst = dst.with_name(dst.name.removesuffix(PATCH))
            base = original(rom, rel.removesuffix(PATCH))
            if base is None:
                sys.exit(f"error: {rel} is not part of the game")
            try:
                data = bps.apply(data, base)
            except bps.BpsError as e:
                sys.exit(f"error: {rel}: {e}")
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(data)
    print(f"{out}: the mod with whole files")
    return 0


def check(mod: Path, rom: Rom) -> tuple[list[str], list[str]]:
    errors, warnings = [], []
    if not (mod / "mod.ini").exists():
        errors.append("missing mod.ini")
    paths = file_ids(rom)
    overlays = {ov.id: ov for ov in rom.overlays9()}
    count = 0
    seen: dict[str, str] = {}
    for rel, f in game_files(mod):
        target = rel.removesuffix(PATCH)
        kind, _, name = target.partition("/")
        if target.lower() in seen:
            errors.append(f"{seen[target.lower()]} and {rel} change the same file (keep one)")
            continue
        seen[target.lower()] = rel
        ov = None
        if kind == "files":
            if name.lower() not in paths:
                warnings.append(f"{rel} is not a file of the game (ignored)")
                continue
        else:
            m = re.fullmatch(r"ov(\d{3})\.bin", name.lower())
            if not m or "/" in name:
                warnings.append(f"{rel}: expected overlays/ovNNN.bin (ignored)")
                continue
            ov = overlays.get(int(m[1]))
            if ov is None or ov.id in PROTECTED_OVERLAYS:
                errors.append(f"{rel}: overlay {int(m[1])} cannot be replaced")
                continue
        data = f.read_bytes()
        if rel.endswith(PATCH):
            try:
                data = bps.apply(data, original(rom, target))
            except bps.BpsError as e:
                errors.append(f"{rel}: {e}")
                continue
        if not data:
            errors.append(f"{rel} is empty")
            continue
        if ov is not None:
            retail = rom.overlay_decompressed(ov)
            if len(data) > len(retail) + 0x10000:
                errors.append(f"{rel} is much larger than the original")
                continue
            if (at := code_change(ov, retail, data)) is not None:
                errors.append(f"{rel} changes code at {at:#010x}")
                continue
        count += 1
    if not count and not errors:
        warnings.append("the mod changes nothing")
    return errors, warnings


def zip_root(extracted: Path) -> Path:
    """Where mod.ini is in an extracted zip: the top, or its only folder (as the game does)."""
    if (extracted / "mod.ini").exists():
        return extracted
    tops = list(extracted.iterdir())
    return tops[0] if len(tops) == 1 and tops[0].is_dir() else extracted


def cmd_check(args) -> int:
    rom = load_image(args.image)
    if args.dir.suffix.lower() == ".zip":
        with tempfile.TemporaryDirectory() as tmp:
            zipfile.ZipFile(args.dir).extractall(tmp)
            errors, warnings = check(zip_root(Path(tmp)), rom)
    else:
        errors, warnings = check(args.dir, rom)
    if "," in args.dir.name:
        errors.append("the name cannot have commas")
    for w in warnings:
        print(f"warning: {w}")
    for e in errors:
        print(f"error: {e}")
    print("ok" if not errors else f"{len(errors)} errors")
    return 1 if errors else 0


def pack(mod: Path, out: Path):
    """The zip the launcher reads: the mod in one folder named like it."""
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(mod.rglob("*")):
            if f.is_file():
                z.write(f, Path(mod.name) / f.relative_to(mod))


def cmd_pack(args) -> int:
    mod: Path = args.dir
    if not (mod / "mod.ini").exists():
        sys.exit(f"error: {mod} has no mod.ini")
    out: Path = args.out or mod.with_suffix(".zip")
    pack(mod, out)
    print(f"{out} ({out.stat().st_size} bytes): put it in the mods folder next to the game")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)

    p = sub.add_parser("new", help="empty mod folder with mod.ini")
    p.add_argument("dir", type=Path)
    p.add_argument("--name")
    p.add_argument("--author")
    p.add_argument("--description")
    p.set_defaults(fn=cmd_new)

    p = sub.add_parser("unpack", help="original files (and overlays) to edit")
    p.add_argument("image", type=Path, help="boej.pak or the retail .nds")
    p.add_argument("out", type=Path)
    p.add_argument("--only", action="append", help="glob of NitroFS paths, e.g. 'data_iz/logic/*' (repeatable)")
    p.add_argument("--overlays", action="store_true", help="also the overlays, decompressed")
    p.set_defaults(fn=cmd_unpack)

    p = sub.add_parser("from-dump", help="mod with what a modified dump changes")
    p.add_argument("retail", type=Path, help="boej.pak or the retail .nds")
    p.add_argument("modified", type=Path)
    p.add_argument("out", type=Path)
    p.add_argument("--name")
    p.add_argument("--author")
    p.add_argument("--description")
    p.add_argument("--patch", action="store_true", help="patches (<path>.bps) instead of whole files")
    p.set_defaults(fn=cmd_from_dump)

    p = sub.add_parser("patch", help="whole files of a mod into patches over the retail ones")
    p.add_argument("dir", type=Path)
    p.add_argument("image", type=Path, help="boej.pak or the retail .nds")
    p.set_defaults(fn=cmd_patch)

    p = sub.add_parser("expand", help="copy of a mod with the patches applied (whole files)")
    p.add_argument("dir", type=Path)
    p.add_argument("image", type=Path, help="boej.pak or the retail .nds")
    p.add_argument("out", type=Path)
    p.set_defaults(fn=cmd_expand)

    p = sub.add_parser("check", help="look for problems in a mod (folder or .zip)")
    p.add_argument("dir", type=Path, help="the mod folder or .zip")
    p.add_argument("image", type=Path, help="boej.pak or the retail .nds")
    p.set_defaults(fn=cmd_check)

    p = sub.add_parser("pack", help="zip a mod folder for the launcher")
    p.add_argument("dir", type=Path)
    p.add_argument("out", type=Path, nargs="?")
    p.set_defaults(fn=cmd_pack)

    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
