#!/usr/bin/env python3
"""Build pipeline: manifest -> ntr-recomp -> generated C++ -> boej executable.

  python3 tools/build.py              # Release build in build/cmake-rel
  python3 tools/build.py --trace      # RelWithDebInfo + function-entry tracing in build/cmake

Prerequisites (once per checkout): tools/extract.py on the retail dump and
"tools/analyze.py dsd" (see README).
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run(cmd):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--trace", action="store_true", help="record guest function entries (slower)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    build = ROOT / "build" / ("cmake" if args.trace else "cmake-rel")
    config = ["-DNTR_TRACE=" + ("ON" if args.trace else "OFF"),
              "-DCMAKE_BUILD_TYPE=" + ("RelWithDebInfo" if args.trace else "Release")]

    run([sys.executable, "-I", ROOT / "tools" / "analyze.py", "manifest"])
    run(["cmake", "-S", ROOT, "-B", build, *config])
    run(["cmake", "--build", build, "--target", "ntr-recomp", "-j", args.jobs])
    run([build / "recompiler" / "ntr-recomp", ROOT / "build" / "recomp" / "manifest.tsv",
         ROOT / "build" / "recomp" / "gen"])
    # Configure again so the game target picks up the generated sources.
    run(["cmake", "-S", ROOT, "-B", build, *config])
    run(["cmake", "--build", build, "--target", "boej", "-j", args.jobs])
    print(f"built: {build / 'game' / 'boej'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
