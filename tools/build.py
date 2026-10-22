#!/usr/bin/env python3
"""Build pipeline: manifest -> ntr-recomp -> generated C++ -> boej executable.

  python3 tools/build.py                        # native Release build in build/cmake-rel
  python3 tools/build.py --trace                # native RelWithDebInfo + function tracing in build/cmake
  python3 tools/build.py --windows              # boej.exe cross-compiled with MinGW-w64 in build/cmake-win
  python3 tools/build.py --windows --dist /mnt/c/Games/ie3   # ...and copy boej.exe there

Prerequisites (once per checkout): tools/extract.py on the retail dump and
"tools/analyze.py dsd" (see README). --windows needs x86_64-w64-mingw32-g++.

The game data package made by tools/extract.py (build/boej.pak) is placed next to the
executable (and copied with --dist), so the game runs with no arguments and no dump.
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MINGW_TOOLCHAIN = ROOT / "cmake" / "toolchains" / "mingw-w64-x86_64.cmake"


def run(cmd):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True)


def place(src: Path, dst: Path):
    """Hard link (no extra disk space), or copy across file systems."""
    if dst.exists() and dst.stat().st_ino == src.stat().st_ino:
        return
    dst.unlink(missing_ok=True)
    try:
        os.link(src, dst)
    except OSError:
        shutil.copy2(src, dst)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--trace", action="store_true", help="record guest function entries (slower)")
    ap.add_argument("--windows", action="store_true", help="cross-compile boej.exe with MinGW-w64")
    ap.add_argument("--dist", type=Path, help="copy the built executable to this directory")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    if args.windows and not shutil.which("x86_64-w64-mingw32-g++"):
        sys.exit("error: x86_64-w64-mingw32-g++ not found (Arch: pacman -S mingw-w64-gcc)")

    build_type = "RelWithDebInfo" if args.trace else "Release"
    trace = "-DNTR_TRACE=" + ("ON" if args.trace else "OFF")

    # ntr-recomp always runs on the build machine, so it comes from a native build.
    native = ROOT / "build" / ("cmake" if args.trace else "cmake-rel")
    native_config = [trace, "-DCMAKE_BUILD_TYPE=" + build_type]
    run([sys.executable, "-I", ROOT / "tools" / "analyze.py", "manifest"])
    run(["cmake", "-S", ROOT, "-B", native, *native_config])
    run(["cmake", "--build", native, "--target", "ntr-recomp", "-j", args.jobs])
    run([native / "recompiler" / "ntr-recomp", ROOT / "build" / "recomp" / "manifest.tsv",
         ROOT / "build" / "recomp" / "gen"])

    if args.windows:
        game_build = ROOT / "build" / "cmake-win"
        config = [trace, "-DCMAKE_BUILD_TYPE=" + build_type, "-DNTR_BUILD_RECOMPILER=OFF",
                  "-DCMAKE_TOOLCHAIN_FILE=" + str(MINGW_TOOLCHAIN)]
        exe = game_build / "game" / "boej.exe"
    else:
        game_build, config = native, native_config
        exe = game_build / "game" / "boej"

    # Configure again so the game target picks up the generated sources.
    run(["cmake", "-S", ROOT, "-B", game_build, *config])
    run(["cmake", "--build", game_build, "--target", "boej", "-j", args.jobs])
    print(f"built: {exe}")

    pak = ROOT / "build" / "boej.pak"
    if pak.exists():
        place(pak, exe.parent / pak.name)
    else:
        print("warning: build/boej.pak not found; run tools/extract.py on the retail dump to create it")

    if args.dist:
        args.dist.mkdir(parents=True, exist_ok=True)
        shipped = [exe] + ([pak] if pak.exists() else [])
        if args.windows:  # SDL3.dll from the SDL3-devel-*-mingw package fetched by CMake
            shipped += list((game_build / "_deps").glob("sdl3_mingw-src/x86_64-w64-mingw32/bin/SDL3.dll"))
        for f in shipped:
            dst = args.dist / f.name
            if dst.exists() and dst.stat().st_size == f.stat().st_size and dst.stat().st_mtime >= f.stat().st_mtime:
                continue  # unchanged (the package is large)
            shutil.copy2(f, dst)
            print(f"copied to: {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
