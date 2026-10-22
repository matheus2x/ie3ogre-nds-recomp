#!/usr/bin/env python3
"""Static-analysis pipeline for the retail dump.

  python3 tools/analyze.py dsd <retail.nds>     # dsd extract + init -> config/boej/dsd (function map)
  python3 tools/analyze.py names                # apply config/boej/names.tsv to the dsd symbols
  python3 tools/analyze.py ghidra               # headless Ghidra project -> build/ghidra/boej.gpr
  python3 tools/analyze.py decompile 0x02000c8c ov132:0x0213acc0 FS_LoadOverlay   # -> stdout
  python3 tools/analyze.py asm 0x0201f7a4                 # disassembly -> stdout
  python3 tools/analyze.py export ram itcm      # whole blocks -> build/ghidra/export/<block>.c

dsd (ds-decomp) finds function boundaries, sections and relocations for the ARM9,
ITCM/DTCM and every overlay, and decrypts the DS Protect functions in ov129. Its
config is committed under config/boej/dsd and is the source of truth for symbol
names. Ghidra is built from that config plus build/extract/retail (tools/extract.py).

Tool locations come from the environment, falling back to ~/tools:
  DSD=~/tools/ds-decomp-0.12.1/bin/dsd   GHIDRA_INSTALL_DIR=~/tools/ghidra_12.1.4_PUBLIC   JAVA_HOME
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DSD_VERSION = "0.12.1"
HOME_TOOLS = Path.home() / "tools"


def tool_path(env: str, default: Path) -> Path:
    path = Path(os.environ.get(env, default))
    if not path.exists():
        sys.exit(f"error: {path} not found (set {env})")
    return path


def run(cmd: list, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def cmd_dsd(args):
    dsd = tool_path("DSD", HOME_TOOLS / f"ds-decomp-{DSD_VERSION}" / "bin" / "dsd")
    version = subprocess.run([dsd, "--version"], capture_output=True, text=True, check=True).stdout.split()[-1]
    if version != DSD_VERSION:
        sys.exit(f"error: dsd {version} found, this pipeline is pinned to {DSD_VERSION}")

    dumps = json.loads((ROOT / "config" / "boej" / "dumps.json").read_text())
    retail = next(d for d in dumps["dumps"] if d["variant"] == dumps["canonical"])
    with open(args.rom, "rb") as f:
        digest = hashlib.file_digest(f, "sha1").hexdigest()
    if digest != retail["sha1"]:
        sys.exit(f"error: analysis runs on the {retail['variant']} dump only (SHA-1 {retail['sha1']}), got {digest}")

    work = ROOT / "build" / "dsd"
    extract = work / "extract"
    config = ROOT / "config" / "boej" / "dsd"
    if config.exists() and not args.force:
        sys.exit(f"error: {config} exists; it may hold manual renames. Pass --force to regenerate it")
    shutil.rmtree(extract, ignore_errors=True)
    shutil.rmtree(config, ignore_errors=True)
    run([dsd, "rom", "extract", "-r", args.rom, "-o", extract])
    run([dsd, "init", "-r", extract / "config.yaml", "-o", config, "-b", work / "build"])


def ghidra_env() -> tuple[Path, dict]:
    ghidra = tool_path("GHIDRA_INSTALL_DIR", HOME_TOOLS / "ghidra_12.1.4_PUBLIC")
    env = dict(os.environ)
    if "JAVA_HOME" not in env:
        jdks = sorted(HOME_TOOLS.glob("jdk-21*"))
        if jdks:
            env["JAVA_HOME"] = str(jdks[-1])
            env["PATH"] = f"{jdks[-1] / 'bin'}{os.pathsep}{env['PATH']}"
    return ghidra, env


def cmd_ghidra(args):
    ghidra, env = ghidra_env()

    layout = json.loads((ROOT / "config" / "boej" / "layout" / "retail.json").read_text())
    extract = ROOT / "build" / "extract" / "retail"
    dsd_config = ROOT / "config" / "boej" / "dsd" / "arm9"
    dsd_extract = ROOT / "build" / "dsd" / "extract"
    for p in (extract / "arm9.bin", dsd_config / "symbols.txt", dsd_extract / "arm9_overlays"):
        if not p.exists():
            sys.exit(f"error: {p} missing; run tools/extract.py and 'tools/analyze.py dsd' first")

    out = ROOT / "build" / "ghidra"
    out.mkdir(parents=True, exist_ok=True)

    # Only the static module goes in the "ram" block; autoloads are mapped at their own addresses.
    arm9 = layout["arm9"]
    static_size = int(arm9["autoload_start"], 16) - int(arm9["ram_address"], 16)
    static = out / "arm9_static.bin"
    static.write_bytes((extract / "arm9.bin").read_bytes()[:static_size])

    entries = [f"main\t{dsd_config / 'symbols.txt'}"]
    for name, auto in (("itcm", arm9["autoloads"][0]), ("dtcm", arm9["autoloads"][1])):
        base = auto["ram_address"]
        entries.append(f"block\t{name}\t{base}\t{extract / 'autoload' / (base[2:] + '.bin')}\t"
                       f"{dsd_config / name / 'symbols.txt'}")
    for ov in layout["overlays"]:
        name = f"ov{ov['id']:03d}"
        # dsd's copy has the DS Protect functions (ov129) decrypted.
        entries.append(f"overlay\t{name}\t{ov['ram_address']}\t{dsd_extract / 'arm9_overlays' / (name + '.bin')}\t"
                       f"{dsd_config / 'overlays' / name / 'symbols.txt'}")
    manifest = out / "manifest.tsv"
    manifest.write_text("\n".join(entries) + "\n")

    run([ghidra / "support" / "analyzeHeadless", out, "boej",
         "-import", static, "-overwrite",
         "-loader", "BinaryLoader", "-loader-baseAddr", arm9["ram_address"],
         "-processor", "ARM:LE:32:v5t", "-cspec", "default",
         "-scriptPath", ROOT / "tools" / "ghidra",
         "-preScript", "ImportBoej.java", manifest,
         "-log", out / "analyze.log"], env=env)
    print(f"project: {out / 'boej.gpr'}")


def run_query_script(script: str, result_name: str, targets: list[str]):
    ghidra, env = ghidra_env()
    out = ROOT / "build" / "ghidra"
    if not (out / "boej.gpr").exists():
        sys.exit("error: run 'tools/analyze.py ghidra' first")
    result = out / result_name
    run([ghidra / "support" / "analyzeHeadless", out, "boej",
         "-process", "arm9_static.bin", "-noanalysis", "-readOnly",
         "-scriptPath", ROOT / "tools" / "ghidra",
         "-postScript", script, result, *targets,
         "-log", out / "query.log"], env=env, stdout=subprocess.DEVNULL)
    print(result.read_text())


def cmd_decompile(args):
    run_query_script("Decompile.java", "decompile.c", args.targets)


def cmd_asm(args):
    run_query_script("Disasm.java", "disasm.s", args.targets)


def cmd_export(args):
    ghidra, env = ghidra_env()
    out = ROOT / "build" / "ghidra"
    if not (out / "boej.gpr").exists():
        sys.exit("error: run 'tools/analyze.py ghidra' first")
    run([ghidra / "support" / "analyzeHeadless", out, "boej",
         "-process", "arm9_static.bin", "-noanalysis", "-readOnly",
         "-scriptPath", ROOT / "tools" / "ghidra",
         "-postScript", "ExportC.java", out / "export", *args.blocks,
         "-log", out / "export.log"], env=env, stdout=subprocess.DEVNULL)
    print(f"exported: {out / 'export'}")


def symbols_file(module: str) -> Path:
    base = ROOT / "config" / "boej" / "dsd" / "arm9"
    if module == "main":
        return base / "symbols.txt"
    if module in ("itcm", "dtcm"):
        return base / module / "symbols.txt"
    return base / "overlays" / module / "symbols.txt"


def load_names() -> dict[str, dict[int, str]]:
    """Read config/boej/names.tsv as {module: {address: name}}, rejecting duplicates."""
    names: dict[str, dict[int, str]] = {}
    seen: dict[str, str] = {}
    path = ROOT / "config" / "boej" / "names.tsv"
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        module, addr, name, *_ = line.split("\t")
        where = f"{path.name}:{n}"
        if name in seen:
            sys.exit(f"error: {where}: {name} already used at {seen[name]}")
        table = names.setdefault(module, {})
        if int(addr, 16) in table:
            sys.exit(f"error: {where}: {addr} already named {table[int(addr, 16)]}")
        table[int(addr, 16)] = name
        seen[name] = where
    return names


def cmd_names(args):
    total = 0
    for module, table in load_names().items():
        path = symbols_file(module)
        lines = path.read_text().splitlines()
        found = set()
        for i, line in enumerate(lines):
            parts = line.split(" ")
            addr = next((int(p[5:], 16) for p in parts if p.startswith("addr:")), None)
            # Labels cover alternate entry points inside a function (e.g. _ll_sdiv).
            if addr in table and parts[1].startswith(("kind:function", "kind:label")):
                parts[0] = table[addr]
                lines[i] = " ".join(parts)
                found.add(addr)
        missing = sorted(set(table) - found)
        if missing:
            sys.exit(f"error: {module}: no dsd function or label at " + ", ".join(f"{a:#010x}" for a in missing))
        path.write_text("\n".join(lines) + "\n")
        total += len(found)
        print(f"{module}: {len(found)} names")
    print(f"applied {total} names; rebuild with 'tools/analyze.py ghidra'")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(required=True)
    p = sub.add_parser("dsd", help="run dsd extract + init on the retail dump")
    p.add_argument("rom", type=Path)
    p.add_argument("--force", action="store_true", help="overwrite config/boej/dsd")
    p.set_defaults(func=cmd_dsd)
    p = sub.add_parser("names", help="apply config/boej/names.tsv to the dsd symbols")
    p.set_defaults(func=cmd_names)
    p = sub.add_parser("ghidra", help="build the headless Ghidra project")
    p.set_defaults(func=cmd_ghidra)
    p = sub.add_parser("decompile", help="decompile functions from the Ghidra project")
    p.add_argument("targets", nargs="+", help="0xADDR, ovNNN:0xADDR or function name")
    p.set_defaults(func=cmd_decompile)
    p = sub.add_parser("asm", help="disassemble functions from the Ghidra project")
    p.add_argument("targets", nargs="+", help="0xADDR, ovNNN:0xADDR or function name")
    p.set_defaults(func=cmd_asm)
    p = sub.add_parser("export", help="decompile whole memory blocks to build/ghidra/export/<block>.c")
    p.add_argument("blocks", nargs="+", help="block names: ram, itcm, ov000...")
    p.set_defaults(func=cmd_export)
    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
