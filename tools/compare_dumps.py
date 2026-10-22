#!/usr/bin/env python3
"""Compare two dumps of the same game: ARM9/ARM7, overlays and NitroFS files.

Usage: python3 tools/compare_dumps.py <base.nds> <other.nds> [--json report.json]

For each changed overlay it reports the byte runs that differ, so you can tell whether
a mod touched code or only data (strings and tables). The recompiler relies on code
being identical across supported dumps.
"""

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from ntr.rom import Rom  # noqa: E402


def diff_runs(a: bytes, b: bytes, gap: int = 4) -> list[tuple[int, int]]:
    """Return (start, length) runs where a and b differ, merging runs closer than gap.

    Bytes past the end of the shorter buffer count as different.
    """
    runs: list[tuple[int, int]] = []
    n = max(len(a), len(b))
    start = last = None
    for i in range(n):
        if i < len(a) and i < len(b) and a[i] == b[i]:
            continue
        if start is not None and i - last > gap:
            runs.append((start, last - start + 1))
            start = None
        if start is None:
            start = i
        last = i
    if start is not None:
        runs.append((start, last - start + 1))
    return runs


def compare(base: Rom, other: Rom) -> dict:
    report: dict = {
        "arm9_equal": base.arm9_decompressed() == other.arm9_decompressed(),
        "arm7_equal": base.arm7 == other.arm7,
        "overlays": [],
    }
    for ob, oo in zip(base.overlays9(), other.overlays9(), strict=True):
        db, do = base.overlay_decompressed(ob), other.overlay_decompressed(oo)
        if db == do:
            continue
        runs = diff_runs(db, do)
        report["overlays"].append({
            "id": ob.id,
            "size": [f"{len(db):#x}", f"{len(do):#x}"],
            "first_diff": f"{ob.ram_address + runs[0][0]:#010x}",
            "runs": [[f"{ob.ram_address + s:#010x}", n] for s, n in runs],
        })

    fb, fo = dict(base.files()), dict(other.files())
    report["files_only_in_base"] = sorted(set(fb) - set(fo))
    report["files_only_in_other"] = sorted(set(fo) - set(fb))
    report["files_changed"] = sorted(p for p in fb.keys() & fo.keys()
                                     if base.file_data(fb[p]) != other.file_data(fo[p]))
    return report


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("base", type=Path)
    ap.add_argument("other", type=Path)
    ap.add_argument("--json", type=Path, help="write the full report here")
    args = ap.parse_args()

    report = compare(Rom.load(args.base), Rom.load(args.other))
    print(f"arm9 equal: {report['arm9_equal']}")
    print(f"arm7 equal: {report['arm7_equal']}")
    print(f"overlays changed: {len(report['overlays'])}")
    for ov in report["overlays"]:
        print(f"  ov{ov['id']:03d} size {ov['size'][0]} -> {ov['size'][1]}, "
              f"{len(ov['runs'])} runs, first at {ov['first_diff']}")
    print(f"files changed: {len(report['files_changed'])}, "
          f"only in base: {len(report['files_only_in_base'])}, only in other: {len(report['files_only_in_other'])}")
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
