#!/usr/bin/env python3
"""Extract a BOEJ dump into build/extract/<variant> and write its layout to config/boej/layout/<variant>.json.

Usage: python3 tools/extract.py <rom.nds> [--out build/extract] [--config config/boej] [--no-files] [--allow-unknown]

The dump is identified by SHA-1 against config/boej/dumps.json. Game data goes only to
--out, which is gitignored. The layout file holds addresses, sizes and hashes; it
contains no game code or assets.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ntr.rom import Rom, RomError  # noqa: E402


def sha1(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def safe_join(root: Path, rel: str) -> Path:
    target = (root / rel).resolve()
    if root.resolve() not in target.parents:
        raise RomError(f"refusing to write outside {root}: {rel!r}")
    return target


def write(path: Path, data: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def identify(rom: Rom, known: dict, allow_unknown: bool) -> str:
    h = rom.header
    if (h.game_code, h.maker_code) != (known["game_code"], known["maker_code"]):
        raise RomError(f"expected {known['game_code']}/{known['maker_code']}, got {h.game_code}/{h.maker_code}")
    digest = sha1(rom.data)
    for dump in known["dumps"]:
        if dump["sha1"] == digest:
            return dump["variant"]
    if not allow_unknown:
        raise RomError(f"unknown dump (SHA-1 {digest}); see config/boej/dumps.json or pass --allow-unknown")
    print(f"warning: unknown dump {digest}, extracting as unknown-{digest[:8]}", file=sys.stderr)
    return f"unknown-{digest[:8]}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("rom", type=Path)
    root = Path(__file__).resolve().parent.parent
    ap.add_argument("--out", type=Path, default=root / "build" / "extract")
    ap.add_argument("--config", type=Path, default=root / "config" / "boej")
    ap.add_argument("--no-files", action="store_true", help="skip dumping the NitroFS files")
    ap.add_argument("--allow-unknown", action="store_true", help="extract dumps not listed in dumps.json")
    args = ap.parse_args()

    known = json.loads((args.config / "dumps.json").read_text())
    rom = Rom.load(args.rom)
    h = rom.header
    variant = identify(rom, known, args.allow_unknown)
    out = args.out / variant
    print(f"dump: {variant}")

    params = rom.module_params()
    arm9 = rom.arm9_decompressed()
    write(out / "arm9.bin", arm9)
    write(out / "arm7.bin", rom.arm7)
    print(f"arm9: {len(rom.arm9):#x} -> {len(arm9):#x} bytes, NitroSDK {params.sdk_version_str}")

    autoloads = []
    for ram, size, bss, data in rom.autoloads(arm9):
        write(out / "autoload" / f"{ram:08x}.bin", data)
        autoloads.append({"ram_address": f"{ram:#010x}", "size": f"{size:#x}", "bss_size": f"{bss:#x}",
                          "sha1": sha1(data)})
    print(f"autoloads: {', '.join(a['ram_address'] for a in autoloads)}")

    overlays = []
    for ov in rom.overlays9():
        data = rom.overlay_decompressed(ov)
        if len(data) != ov.ram_size:
            raise RomError(f"overlay {ov.id} decompressed to {len(data):#x}, table says {ov.ram_size:#x}")
        write(out / "overlays" / f"ov{ov.id:03d}.bin", data)
        overlays.append({
            "id": ov.id,
            "ram_address": f"{ov.ram_address:#010x}",
            "ram_size": f"{ov.ram_size:#x}",
            "bss_size": f"{ov.bss_size:#x}",
            "sinit_start": f"{ov.sinit_start:#010x}",
            "sinit_end": f"{ov.sinit_end:#010x}",
            "file_id": ov.file_id,
            "compressed": ov.compressed,
            "sha1": sha1(data),
        })
    print(f"overlays: {len(overlays)}")

    files = rom.files()
    if not args.no_files:
        files_root = out / "files"
        for path, file_id in files:
            write(safe_join(files_root, path), rom.file_data(file_id))
        print(f"files: {len(files)} -> {files_root}")

    slots: dict[str, list[int]] = {}
    for ov in overlays:
        slots.setdefault(ov["ram_address"], []).append(ov["id"])

    layout = {
        "variant": variant,
        "input_sha1": sha1(rom.data),
        "header": {
            "title": h.title, "game_code": h.game_code, "maker_code": h.maker_code,
            "unit_code": h.unit_code, "rom_version": h.rom_version, "rom_size": f"{h.rom_size:#x}",
        },
        "arm9": {
            "ram_address": f"{h.arm9_ram:#010x}", "entry": f"{h.arm9_entry:#010x}",
            "rom_size": f"{h.arm9_size:#x}", "decompressed_size": f"{len(arm9):#x}",
            "module_params_offset": f"{params.offset:#x}",
            "autoload_list": f"{params.autoload_list:#010x}",
            "autoload_list_end": f"{params.autoload_list_end:#010x}",
            "autoload_start": f"{params.autoload_start:#010x}",
            "bss_start": f"{params.static_bss_start:#010x}",
            "bss_end": f"{params.static_bss_end:#010x}",
            "compressed_static_end": f"{params.compressed_static_end:#010x}",
            "sdk_version": f"{params.sdk_version:#010x}",
            "sha1": sha1(arm9),
            "autoloads": autoloads,
        },
        "arm7": {
            "ram_address": f"{h.arm7_ram:#010x}", "entry": f"{h.arm7_entry:#010x}",
            "size": f"{h.arm7_size:#x}", "sha1": sha1(rom.arm7),
        },
        "overlay_slots": slots,
        "overlays": overlays,
        "file_count": len(files),
    }
    layout_path = args.config / "layout" / f"{variant}.json"
    layout_path.parent.mkdir(parents=True, exist_ok=True)
    layout_path.write_text(json.dumps(layout, indent=2) + "\n")
    print(f"layout: {layout_path}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RomError as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)
