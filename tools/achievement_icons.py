#!/usr/bin/env python3
"""The achievements' icons (achievements/icons/*.png), drawn with ImageMagick.

  python3 tools/achievement_icons.py

64x64 PNGs with 8 bits per channel (what SDL_LoadPNG reads). Only shapes and text: no game
graphics, so they ship with the release. The text font is Liberation Sans Bold (SIL OFL).
first_save.png is drawn by hand and left alone.
"""

import math
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ICONS = ROOT / "achievements" / "icons"
FONT = "/usr/share/fonts/liberation/LiberationSans-Bold.ttf"

# The captains' teams: file name -> (abbreviation, background, text colour).
TEAMS = {
    "big_waves": ("BW", "#0f766e", "#fde047"),
    "desert_lion": ("DL", "#7f1d1d", "#fef3c7"),
    "neo_japan": ("NJ", "#1e293b", "#e2e8f0"),
    "fire_dragon": ("FD", "#b91c1c", "#fef2f2"),
    "knights_of_queen": ("KQ", "#1e3a8a", "#f8fafc"),
    "team_k": ("K", "#111827", "#ef4444"),
    "the_empire": ("EMP", "#38bdf8", "#f8fafc"),
    "unicorn": ("UNI", "#1d4ed8", "#fecaca"),
    "orpheus": ("ORF", "#2563eb", "#f8fafc"),
    "team_garshield": ("GAR", "#3b0764", "#e9d5ff"),
    "the_kingdom": ("KDM", "#15803d", "#fde047"),
    "little_gigant": ("LG", "#c2410c", "#fff7ed"),
    "tenkuu_no_shito": ("TS", "#e5e7eb", "#a16207"),
    "makai_gundan_z": ("MZ", "#450a0a", "#f87171"),
    "the_ogre": ("OGR", "#3f3f46", "#a3e635"),
}


def magick(*args: str, out: Path):
    subprocess.run(["magick", "-size", "64x64", "xc:none", *args, "-depth", "8", f"PNG32:{out}"], check=True)


def frame(fill: str, border: str) -> list[str]:
    return ["-fill", border, "-draw", "roundrectangle 1,1 62,62 10,10",
            "-fill", fill, "-draw", "roundrectangle 4,4 59,59 8,8"]


def text(value: str, size: int, color: str, y: int) -> list[str]:
    return ["-font", FONT, "-pointsize", str(size), "-fill", color, "-gravity", "center",
            "-annotate", f"+0{y:+d}", value, "-gravity", "northwest"]


def star(x: float, y: float, r: float) -> str:
    """A five-pointed star centred on (x, y), for -draw."""
    pts = []
    for k in range(10):
        radius = r if k % 2 == 0 else r * 0.42
        angle = math.pi * k / 5 - math.pi / 2
        pts.append(f"{x + radius * math.cos(angle):.1f},{y + radius * math.sin(angle):.1f}")
    return "polygon " + " ".join(pts)


def main() -> int:
    if not shutil.which("magick"):
        sys.exit("error: ImageMagick (magick) not found")
    if not Path(FONT).exists():
        sys.exit(f"error: {FONT} not found (Liberation fonts)")
    ICONS.mkdir(parents=True, exist_ok=True)
    for n in range(2, 12):  # chapters: an open book and the number
        magick(*frame("#0d9488", "#134e4a"),
               "-fill", "#f0fdfa", "-draw", "polygon 14,14 31,18 31,40 14,36", "-draw", "polygon 50,14 33,18 33,40 50,36",
               *text(str(n), 22 if n < 10 else 20, "#f0fdfa", 17), out=ICONS / f"chapter_{n:02d}.png")
    for n in (10, 25, 50, 75, 99):  # team level: a star and the level
        magick(*frame("#d97706", "#78350f"),
               "-fill", "#fde68a", "-draw", star(32, 20, 14),
               *text(str(n), 20, "#ffffff", 18), out=ICONS / f"level_{n}.png")
    for n in (16, 30, 50, 100):  # members: three heads and the count
        magick(*frame("#7c3aed", "#3b0764"),
               "-fill", "#ddd6fe", "-draw", "circle 20,18 20,24", "-draw", "circle 44,18 44,24", "-draw", "circle 32,15 32,22",
               "-draw", "roundrectangle 13,25 27,36 5,5", "-draw", "roundrectangle 37,25 51,36 5,5",
               "-fill", "#ede9fe", "-draw", "roundrectangle 24,23 40,38 6,6",
               *text(str(n), 18 if n < 100 else 16, "#ffffff", 18), out=ICONS / f"members_{n}.png")
    for team, (abbr, fill, color) in TEAMS.items():  # captains: the armband's C and the team
        magick(*frame(fill, color),
               "-fill", color, "-draw", "circle 32,22 32,36",
               *text("C", 20, fill, -10), *text(abbr, 17 if len(abbr) < 3 else 14, color, 18),
               out=ICONS / f"captain_{team}.png")
    stars = []
    for x, y in ((20, 27), (44, 27), (32, 16)):  # all stars: three stars
        stars += ["-draw", star(x, y, 9)]
    magick(*frame("#1e1b4b", "#facc15"), "-fill", "#facc15", *stars, *text("ALL", 14, "#fde68a", 17),
           out=ICONS / "all_stars.png")
    print(f"icons in {ICONS}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
