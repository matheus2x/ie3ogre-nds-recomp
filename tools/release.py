#!/usr/bin/env python3
"""Release package: the launcher and its SDK, with no game data and no game code.

  python3 tools/release.py                     # Windows x64 (cross-compiled with llvm-mingw)
  python3 tools/release.py --platform linux    # native Linux (system compiler and SDL3)

Output in build/release/: ie3ogre-recomp-<version>-<platform>.zip and sha256sums.txt,
the assets the launcher's updater looks for, and mod-<name>-<version>.zip for each official
mod in mods/<name>/ (patches only, no game data; the player puts it in the game's mods/
folder), <version> being the one in its mod.ini. assets.txt lists them for the release workflow.
A mod that changed since the previous tag without a new version is refused.
The zip holds one folder:

  ie3ogre-recomp/
    ie3-launcher(.exe)  SDL3.dll (Windows)  README.txt  LEIA-ME.txt  TERCEIROS.txt  licenses/ (MIT + third parties)
    sdk/version.txt     the release version (VERSION)
    sdk/manifest.tsv    the recompiler input with {modules}/ paths (tools/analyze.py manifest --template)
    sdk/bin/            ntr-recomp and dsd (pinned release, checked by SHA-256)
    sdk/include/ sdk/runtime/ sdk/game/   runtime and game entry sources (and the icon)
    sdk/ui/ sdk/imgui/  the pause menu and Dear ImGui (with its SDL3 backends)
    sdk/sdl3/           SDL3 headers and import library (Windows)
    sdk/toolchain.txt   the llvm-mingw archive the launcher downloads (Windows)

The manifest is made from config/boej (function boundaries and names), so no dump is
needed here. Tools are downloaded once to build/tools: LLVM_MINGW can point to an
llvm-mingw folder instead (Linux host build, for cross-compiling the launcher).
"""

import argparse
import hashlib
import os
import shutil
import stat
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

import changelog  # tools/changelog.py
import mod  # tools/mod.py

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "build" / "tools"

LLVM_MINGW_RELEASE = "20260922"
# Linux-hosted llvm-mingw, to cross-compile the launcher and ntr-recomp.
LLVM_MINGW_HOST = {
    "url": f"https://github.com/mstorsjo/llvm-mingw/releases/download/{LLVM_MINGW_RELEASE}/"
           f"llvm-mingw-{LLVM_MINGW_RELEASE}-ucrt-ubuntu-22.04-x86_64.tar.xz",
    "sha256": "bb7bb7654b33d5aa8712acb837c963b2e0c56352560c76105270a3268c665c21",
}
# Windows-hosted llvm-mingw, which the launcher downloads to build the game.
LLVM_MINGW_TARGET = {
    "url": f"https://github.com/mstorsjo/llvm-mingw/releases/download/{LLVM_MINGW_RELEASE}/"
           f"llvm-mingw-{LLVM_MINGW_RELEASE}-ucrt-x86_64.zip",
    "sha256": "e3ad77d117a4bea19a7a3b333341824d79a5a371004a10e25b8504e7b3047666",
    "size": 190725905,
    "dir": f"llvm-mingw-{LLVM_MINGW_RELEASE}-ucrt-x86_64",
}
DSD_LICENSE = ("https://raw.githubusercontent.com/AetiasHax/ds-decomp/v0.12.1/LICENSE",
               "6868b9ef46509224c823cc2f80d2d0bbd37086f89bb3ce2a570c0b04f0bb4a7b")
DSD = {
    "windows": ("https://github.com/AetiasHax/ds-decomp/releases/download/v0.12.1/dsd-windows-x86_64.exe",
                "5917df26bffc05d4752276a2e0b1edd21bd6e920aabe1992be8a50efc9d10932"),
    "linux": ("https://github.com/AetiasHax/ds-decomp/releases/download/v0.12.1/dsd-linux-x86_64",
              "256edde804dd8e5d7bc4c0e4187f85244ee4cd1f42bb427502e01fda6af43ceb"),
}


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run([str(c) for c in cmd], check=True, **kw)


def sha256(path: Path) -> str:
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def fetch(url: str, digest: str) -> Path:
    """Downloads into build/tools once; the SHA-256 must match."""
    TOOLS.mkdir(parents=True, exist_ok=True)
    path = TOOLS / url.rsplit("/", 1)[1]
    if not path.exists() or sha256(path) != digest:
        print(f"downloading {url}", flush=True)
        tmp = path.with_suffix(path.suffix + ".tmp")
        with urllib.request.urlopen(url) as r, open(tmp, "wb") as f:
            shutil.copyfileobj(r, f)
        if sha256(tmp) != digest:
            tmp.unlink()
            sys.exit(f"error: {url} does not match the pinned SHA-256")
        tmp.replace(path)
    return path


def llvm_mingw_host() -> Path:
    if os.environ.get("LLVM_MINGW"):
        return Path(os.environ["LLVM_MINGW"])
    archive = fetch(LLVM_MINGW_HOST["url"], LLVM_MINGW_HOST["sha256"])
    folder = TOOLS / archive.name.removesuffix(".tar.xz")
    if not folder.exists():
        with tarfile.open(archive) as t:
            t.extractall(TOOLS, filter="tar")
    return folder


def build(platform: str) -> tuple[Path, Path]:
    """Builds the launcher and ntr-recomp; returns (build dir, SDL3 folder or None)."""
    if platform == "windows":
        out = ROOT / "build" / "cmake-llvm"
        config = ["-DCMAKE_TOOLCHAIN_FILE=" + str(ROOT / "cmake" / "toolchains" / "llvm-mingw-x86_64.cmake"),
                  "-DLLVM_MINGW=" + str(llvm_mingw_host())]
    else:
        out = ROOT / "build" / "cmake-release"
        config = []
    # No game target: the game is built on the player's PC.
    config += ["-DCMAKE_BUILD_TYPE=Release", "-DNTR_GENERATED_DIR=" + str(out / "no-generated-code")]
    env = dict(os.environ)
    if platform == "windows":
        env["LLVM_MINGW"] = str(llvm_mingw_host())
    run(["cmake", "-S", ROOT, "-B", out, *config], env=env)
    run(["cmake", "--build", out, "--target", "ie3-launcher", "ntr-recomp", "-j", os.cpu_count() or 4])
    sdl = out / "_deps" / "sdl3_mingw-src" / "x86_64-w64-mingw32" if platform == "windows" else None
    return out, sdl


def copy_tree(src: Path, dst: Path, patterns: tuple[str, ...]):
    dst.mkdir(parents=True, exist_ok=True)
    for pattern in patterns:
        for f in sorted(src.glob(pattern)):
            if f.is_file():
                shutil.copy2(f, dst / f.name)


def check_version(folder: Path, version: str):
    """A mod that changed since the previous release must have a new version in mod.ini."""
    since = changelog.previous_tag("HEAD")
    if not since:
        return
    rel = folder.relative_to(ROOT).as_posix()
    if subprocess.run(["git", "diff", "--quiet", since, "HEAD", "--", rel], cwd=ROOT).returncode == 0:
        return
    try:
        before = mod.ini(changelog.git("show", f"{since}:{rel}/mod.ini")).get("version", "")
    except subprocess.CalledProcessError:
        return  # a new mod
    if before == version:
        sys.exit(f"error: {rel} changed since {since} but mod.ini still says version = {version}")


def official_mods(release: Path) -> list[Path]:
    """mods/<name>/ -> mod-<name>-<version>.zip, as tools/mod.py pack makes it (shown with that
    name on the release page). Whole game files are refused: an official mod carries only patches (docs/MODS.md)."""
    out = []
    for folder in sorted((ROOT / "mods").iterdir()) if (ROOT / "mods").is_dir() else []:
        if not (folder / "mod.ini").is_file():
            continue
        info = mod.ini((folder / "mod.ini").read_text(encoding="utf-8"))
        version = info.get("version", "")
        if not version:
            sys.exit(f"error: {folder}/mod.ini has no version")
        check_version(folder, version)
        whole = [f for kind in ("files", "overlays") for f in (folder / kind).rglob("*")
                 if f.is_file() and f.suffix != ".bps"]
        if whole:
            sys.exit(f"error: {whole[0]} is a whole game file; official mods carry only patches "
                     f"(tools/mod.py patch)")
        zip_path = release / f"mod-{folder.name}-{version}.zip"
        zip_path.unlink(missing_ok=True)
        mod.pack(folder, zip_path)
        out.append(zip_path)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--platform", choices=["windows", "linux"], default="windows")
    args = ap.parse_args()
    platform = args.platform
    exe = ".exe" if platform == "windows" else ""
    version = (ROOT / "VERSION").read_text().strip()

    out, sdl = build(platform)
    release = ROOT / "build" / "release"
    stage = release / "stage"
    shutil.rmtree(stage, ignore_errors=True)
    top = stage / "ie3ogre-recomp"
    sdk = top / "sdk"
    (sdk / "bin").mkdir(parents=True)

    shutil.copy2(out / "launcher" / f"ie3-launcher{exe}", top)
    shutil.copy2(out / "recompiler" / f"ntr-recomp{exe}", sdk / "bin")
    dsd_url, dsd_sha = DSD[platform]
    shutil.copy2(fetch(dsd_url, dsd_sha), sdk / "bin" / f"dsd{exe}")
    for f in (sdk / "bin").iterdir():
        f.chmod(f.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    run([sys.executable, "-I", ROOT / "tools" / "analyze.py", "manifest", "--template", sdk / "manifest.tsv"])
    (sdk / "version.txt").write_text(version + "\n")
    copy_tree(ROOT / "runtime" / "include" / "ntr", sdk / "include" / "ntr", ("*.h",))
    copy_tree(ROOT / "runtime" / "src", sdk / "runtime", ("*.cpp", "*.h"))
    copy_tree(ROOT / "game", sdk / "game", ("main.cpp", "icon.rc", "icon.ico"))
    copy_tree(ROOT / "runtime" / "ui", sdk / "ui", ("*.cpp", "*.h"))
    imgui = out / "_deps" / "imgui-src"
    copy_tree(imgui, sdk / "imgui", ("imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp", "*.h"))
    copy_tree(imgui / "backends", sdk / "imgui", ("imgui_impl_sdl3.*", "imgui_impl_sdlrenderer3.*"))
    if platform == "windows":
        shutil.copy2(sdl / "bin" / "SDL3.dll", top)
        shutil.copytree(sdl / "include", sdk / "sdl3" / "include")
        (sdk / "sdl3" / "lib").mkdir(parents=True)
        shutil.copy2(sdl / "lib" / "libSDL3.dll.a", sdk / "sdl3" / "lib")
        t = LLVM_MINGW_TARGET
        (sdk / "toolchain.txt").write_text(f"url={t['url']}\nsha256={t['sha256']}\nsize={t['size']}\ndir={t['dir']}\n")
    for doc in ("README.txt", "LEIA-ME.txt", "TERCEIROS.txt"):
        shutil.copy2(ROOT / "launcher" / doc, top)
    licenses = top / "licenses"
    licenses.mkdir()
    shutil.copy2(ROOT / "LICENSE", licenses / "ie3ogre-recomp.txt")
    shutil.copy2(out / "_deps" / "imgui-src" / "LICENSE.txt", licenses / "imgui.txt")
    shutil.copy2(fetch(*DSD_LICENSE), licenses / "dsd.txt")
    if platform == "windows":
        shutil.copy2(sdl.parent / "LICENSE.txt", licenses / "SDL3.txt")

    name = f"ie3ogre-recomp-{version}-{platform}.zip"
    zip_path = release / name
    zip_path.unlink(missing_ok=True)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(stage.rglob("*")):
            if f.is_file():  # from_file keeps the executable bits
                info = zipfile.ZipInfo.from_file(f, f.relative_to(stage).as_posix())
                info.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(info, f.read_bytes())
    mods = official_mods(release)
    zips = [zip_path] + mods
    sums = release / "sha256sums.txt"
    names = {z.name for z in zips}
    lines = [line for line in (sums.read_text().splitlines() if sums.exists() else []) if line.split()[-1] not in names]
    lines += [f"{sha256(z)}  {z.name}" for z in zips]
    sums.write_text("\n".join(sorted(lines, key=lambda l: l.split()[-1])) + "\n")
    # What the release workflow uploads, shown on the page with the file names.
    assets = [str(z) for z in zips] + [str(sums)]
    (release / "assets.txt").write_text("\n".join(assets) + "\n", encoding="utf-8")
    for z in zips:
        print(f"release: {z} ({z.stat().st_size / 2**20:.1f} MiB)")
    print(f"sums: {sums}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
