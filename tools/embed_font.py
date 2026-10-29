#!/usr/bin/env python3
"""The fonts of ntr-ui (the launcher and the pause menu) as a C++ header.

  python3 tools/embed_font.py              # runtime/ui/fonts/*.ttf -> runtime/ui/fonts_embedded.h

The header is kept in the repository: the game compiles runtime/ui on the player's PC from
the SDK (sdk/ui/), with no build step to make it there.

The fonts are Rubik (SIL OFL 1.1, runtime/ui/fonts/OFL.txt), from the variable font of
github.com/googlefonts/rubik (fonts/variable/Rubik[wght].ttf): Dear ImGui cannot choose a
weight, so each one is a static instance, and Latin only (the Hebrew and Cyrillic of the
original are not used). With fontTools:

  fonttools varLib.instancer 'Rubik[wght].ttf' wght=400 --update-name-table -o r.ttf
  pyftsubset r.ttf --layout-features=kern,liga --output-file=Rubik-Regular.ttf \\
      --unicodes=U+0020-007E,U+00A0-024F,U+2000-206F,U+20AC,U+2122,U+2190-21FF,U+2212,U+25A0-25FF

and wght=700 for Rubik-Bold.ttf.
"""

import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
FONTS = ROOT / "runtime" / "ui" / "fonts"
OUT = ROOT / "runtime" / "ui" / "fonts_embedded.h"


def array(name: str, data: bytes) -> str:
    rows = []
    for i in range(0, len(data), 24):
        rows.append("    " + ",".join(str(b) for b in data[i:i + 24]) + ",")
    return f"inline const unsigned char {name}[{len(data)}] = {{\n" + "\n".join(rows) + "\n};\n"


def main() -> None:
    parts = [
        "// Made by tools/embed_font.py from runtime/ui/fonts/ (Rubik, SIL OFL 1.1). Do not edit.\n",
        "#pragma once\n\n",
    ]
    for weight in ("Regular", "Bold"):
        data = (FONTS / f"Rubik-{weight}.ttf").read_bytes()
        parts.append(array(f"kRubik{weight}", data))
        parts.append("\n")
    OUT.write_text("".join(parts), newline="\n")
    print(f"{OUT.relative_to(ROOT)}: {OUT.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
