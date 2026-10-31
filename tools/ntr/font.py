"""The game's fonts (data_iz/font/*.NFTR) whole: read, written back with the same layout, and as
a PNG sheet with a JSON of the glyphs, for a translation to draw its letters (tools/font.py).

NFTR: "RTFN", 0xFEFF, u16 version (0x0101 retail, 0x0102 the fan translation's), u32 file size,
u16 0x10, u16 section count. Sections ([magic][u32 size], 4-aligned):
- FINF: u8 type, u8 line height, u16 glyph of an unknown code, u8 left, u8 width, u8 advance of
  that glyph, u8 encoding (2: Shift-JIS), u32 pointers (+8) to CGLP, CWDH and the first CMAP;
  version 0x0102 adds u8 height, width, ascent and an unknown byte.
- CGLP: u8 cell width, height, u16 bytes per glyph, u8 baseline, max width, bits per pixel,
  rotation; then the glyphs, rows MSB first.
- CWDH: u16 first glyph, last glyph, u32 next (+8), and (left, glyph width, advance) per glyph.
- CMAP (chained): u16 first code, last code, u16 type, u16 0, u32 next (+8). Type 0: u16 glyph of
  the first code, the others follow; type 1: u16 glyph per code (0xFFFF none); type 2: u16 count
  and (code, glyph) pairs.

The sheet has the glyphs in order, COLUMNS a row, each in its cell with a grid line around;
ink is dark (black for 1 bpp, greys for 2 bpp), the columns outside a glyph's width are tinted.
"""

import json
import struct
from dataclasses import dataclass, field

from .png import read_png, write_png

COLUMNS = 16
GRID, TINT, PAPER = (255, 96, 96), (222, 232, 250), (255, 255, 255)


class FontError(ValueError):
    pass


def _align(n: int) -> int:
    return (n + 3) & ~3


def _section(magic: bytes, body: bytes) -> bytes:
    size = _align(8 + len(body))
    return magic + struct.pack("<I", size) + body + bytes(size - 8 - len(body))


def _pack(rows: list[list[int]], w: int, depth: int, size: int) -> bytes:
    out = bytearray(size)
    for y, row in enumerate(rows):
        for x, v in enumerate(row):
            k = (y * w + x) * depth
            out[k // 8] |= v << (8 - depth - k % 8)
    return bytes(out)


@dataclass
class Nftr:
    version: int
    finf: list[int]  # type, line, default glyph, left, width, advance, encoding (+ the 4 of 0x0102)
    cell: tuple[int, int]
    baseline: int
    max_width: int
    depth: int
    rotation: int
    glyphs: list[bytes] = field(default_factory=list)  # the packed bitmap of each glyph
    widths: list[tuple[int, int, int]] = field(default_factory=list)  # (left, width, advance)
    cmap: list[list] = field(default_factory=list)  # [first, last, type] of each block, in order
    codes: dict[int, int] = field(default_factory=dict)  # character code -> glyph
    layer: bool = False  # only some glyphs, to draw over the base's font (with_layer)

    @property
    def glyph_size(self) -> int:
        return (self.cell[0] * self.cell[1] * self.depth + 7) // 8

    # --- NFTR ------------------------------------------------------------------------

    @classmethod
    def read(cls, d: bytes) -> "Nftr":
        if d[:4] != b"RTFN":
            raise FontError("not an NFTR font")
        version = struct.unpack_from("<H", d, 6)[0]
        p = struct.unpack_from("<H", d, 0x0C)[0]
        if d[p:p + 4] != b"FNIF":
            raise FontError("no FINF")
        fsize = struct.unpack_from("<I", d, p + 4)[0]
        t, line, dflt, left, width, adv, enc, cglp, cwdh, cmap = struct.unpack_from("<BBHBBBBIII", d, p + 8)
        finf = [t, line, dflt, left, width, adv, enc] + list(d[p + 0x1C:p + fsize])
        cw, ch, size, base, maxw, depth, rot = struct.unpack_from("<BBHBBBB", d, cglp)
        f = cls(version, finf, (cw, ch), base, maxw, depth, rot)
        if size != f.glyph_size:
            raise FontError(f"{size} bytes per glyph, a {cw}x{ch} cell of {depth} bpp takes {f.glyph_size}")
        n = (struct.unpack_from("<I", d, cglp - 4)[0] - 16) // size
        widths: dict[int, tuple[int, int, int]] = {}
        q = cwdh
        while q:
            first, last, nxt = struct.unpack_from("<HHI", d, q)
            for k in range(last - first + 1):
                widths[first + k] = tuple(d[q + 8 + 3 * k:q + 11 + 3 * k])
            q = nxt
        n = min(n, max(widths) + 1)
        f.glyphs = [d[cglp + 8 + g * size:cglp + 8 + (g + 1) * size] for g in range(n)]
        f.widths = [widths.get(g, (left, width, adv)) for g in range(n)]
        q = cmap
        while q:
            first, last, kind, _, nxt = struct.unpack_from("<HHHHI", d, q)
            f.cmap.append([first, last, kind])
            if kind == 0:
                base_glyph = struct.unpack_from("<H", d, q + 12)[0]
                for c in range(first, last + 1):
                    f.codes[c] = base_glyph + c - first
            elif kind == 1:
                for k, c in enumerate(range(first, last + 1)):
                    g = struct.unpack_from("<H", d, q + 12 + 2 * k)[0]
                    if g != 0xFFFF:
                        f.codes[c] = g
            elif kind == 2:
                for k in range(struct.unpack_from("<H", d, q + 12)[0]):
                    c, g = struct.unpack_from("<HH", d, q + 14 + 4 * k)
                    f.codes[c] = g
            else:
                raise FontError(f"CMAP of type {kind}")
            q = nxt
        if bad := [c for c, g in f.codes.items() if g >= n]:
            raise FontError(f"codes without a glyph: {', '.join(f'{c:#x}' for c in bad[:5])}")
        return f

    def write(self) -> bytes:
        """The NFTR, laid out as the game's and the fan translation's fonts are."""
        n = len(self.glyphs)
        if len(self.widths) != n:
            raise FontError(f"{n} glyphs and {len(self.widths)} widths")
        if bad := [g for g, b in enumerate(self.glyphs) if len(b) != self.glyph_size]:
            raise FontError(f"glyph {bad[0]} is not {self.glyph_size} bytes")
        blocks = self._cmap_blocks()
        finf_size = 8 + 20 + len(self.finf) - 7
        cglp = _section(b"PLGC", struct.pack("<BBHBBBB", *self.cell, self.glyph_size, self.baseline,
                                             self.max_width, self.depth, self.rotation) + b"".join(self.glyphs))
        cwdh = _section(b"HDWC", struct.pack("<HHI", 0, n - 1, 0) + bytes(v for w in self.widths for v in w))
        at_cglp = 0x10 + _align(finf_size)
        at_cwdh = at_cglp + len(cglp)
        at = at_cmap = at_cwdh + len(cwdh)
        cmaps = []
        for k, (first, last, kind, data) in enumerate(blocks):
            size = _align(8 + 12 + len(data))
            nxt = at + size + 8 if k + 1 < len(blocks) else 0
            cmaps.append(_section(b"PAMC", struct.pack("<HHHHI", first, last, kind, 0, nxt) + data))
            at += size
        t, line, dflt, left, width, adv, enc = self.finf[:7]
        finf = _section(b"FNIF", struct.pack("<BBHBBBBIII", t, line, dflt, left, width, adv, enc,
                                             at_cglp + 8, at_cwdh + 8, at_cmap + 8) + bytes(self.finf[7:]))
        body = finf + cglp + cwdh + b"".join(cmaps)
        return (b"RTFN" + struct.pack("<HHIHH", 0xFEFF, self.version, 0x10 + len(body), 0x10, 3 + len(cmaps))
                + body)

    def _cmap_blocks(self) -> list[tuple[int, int, int, bytes]]:
        """(first, last, type, data) of each CMAP: the blocks of self.cmap, and the codes no block
        of type 0 or 1 covers in the type-2 block (one is added at the end if there is none)."""
        out, covered = [], set()
        blocks = [list(b) for b in self.cmap]
        if not any(b[2] == 2 for b in blocks) and any(
                not any(b[0] <= c <= b[1] for b in blocks) for c in self.codes):
            blocks.append([0, 0xFFFF, 2])
        for first, last, kind in blocks:
            if kind == 0:
                if missing := [c for c in range(first, last + 1) if c not in self.codes]:
                    raise FontError(f"code {missing[0]:#x} has no glyph, and the block {first:#x}-{last:#x} needs all")
                base = self.codes[first]
                if any(self.codes[c] != base + c - first for c in range(first, last + 1)):
                    raise FontError(f"the glyphs of the codes {first:#x}-{last:#x} must follow each other")
                out.append((first, last, 0, struct.pack("<H", base)))
                covered.update(range(first, last + 1))
            elif kind == 1:
                out.append((first, last, 1, b"".join(struct.pack("<H", self.codes.get(c, 0xFFFF))
                                                     for c in range(first, last + 1))))
                covered.update(range(first, last + 1))
        for first, last, kind in blocks:
            if kind == 2:
                pairs = sorted((c, g) for c, g in self.codes.items() if c not in covered and first <= c <= last)
                covered.update(c for c, _ in pairs)
                data = struct.pack("<H", len(pairs)) + b"".join(struct.pack("<HH", c, g) for c, g in pairs)
                out.insert(next(i for i, b in enumerate(blocks) if b == [first, last, kind]),
                           (first, last, 2, data))
        if loose := sorted(set(self.codes) - covered):
            raise FontError(f"code {loose[0]:#x} is in no CMAP block")
        return out

    # --- pixels ------------------------------------------------------------------------

    def bitmap(self, g: int) -> list[list[int]]:
        bits, (w, h), d = self.glyphs[g], self.cell, self.depth
        top = (1 << d) - 1
        return [[bits[(y * w + x) * d // 8] >> (8 - d - (y * w + x) * d % 8) & top for x in range(w)]
                for y in range(h)]

    def pack(self, rows: list[list[int]]) -> bytes:
        return _pack(rows, self.cell[0], self.depth, self.glyph_size)

    # --- the sheet ---------------------------------------------------------------------

    def sheet_size(self) -> tuple[int, int]:
        w, h = self.cell
        rows = (len(self.glyphs) + COLUMNS - 1) // COLUMNS
        return COLUMNS * (w + 1) + 1, rows * (h + 1) + 1

    def ink_colours(self) -> list[tuple[int, int, int]]:
        top = (1 << self.depth) - 1
        return [(v, v, v) for v in (255 - 255 * k // top for k in range(1, top + 1))]

    def sheet(self) -> bytes:
        """The glyphs as an indexed PNG: 0 paper, 1 tint, 2 grid, then the inks."""
        (w, h), (sw, sh) = self.cell, self.sheet_size()
        pal = [PAPER, TINT, GRID] + self.ink_colours()
        px = [bytearray(sw) for _ in range(sh)]
        for y in range(sh):
            for x in range(sw):
                if x % (w + 1) == 0 or y % (h + 1) == 0:
                    px[y][x] = 2
        for g in range(len(self.glyphs)):
            x0, y0 = 1 + g % COLUMNS * (w + 1), 1 + g // COLUMNS * (h + 1)
            left, width, _ = self.widths[g]
            for y, row in enumerate(self.bitmap(g)):
                for x, v in enumerate(row):
                    px[y0 + y][x0 + x] = 2 + v if v else 0 if left <= x < left + width else 1
        return write_png(sw, sh, pal, [bytes(r) for r in px], transparent=False)

    def read_sheet(self, png: bytes) -> list[bytes]:
        """The glyphs drawn on a sheet (any PNG an editor saves): dark is ink, light or
        see-through is not; with 2 bpp, the darkness picks the ink."""
        sw, sh, rows = read_png(png)
        if (sw, sh) != self.sheet_size():
            raise FontError(f"the sheet is {sw}x{sh}, a font of {len(self.glyphs)} glyphs of {self.cell[0]}x"
                            f"{self.cell[1]} needs {self.sheet_size()[0]}x{self.sheet_size()[1]}")
        (w, h), top = self.cell, (1 << self.depth) - 1
        out = []
        for g in range(len(self.glyphs)):
            x0, y0 = 1 + g % COLUMNS * (w + 1), 1 + g // COLUMNS * (h + 1)
            bitmap = []
            for y in range(h):
                row = []
                for x in range(w):
                    r, gr, b, a = rows[y0 + y][x0 + x]
                    dark = 255 - (r * 299 + gr * 587 + b * 114) // 1000 if a >= 128 else 0
                    row.append(min(top, (dark * top + 127) // 255) if top > 1 else int(dark >= 128))
                bitmap.append(row)
            out.append(self.pack(bitmap))
        return out

    # --- layers --------------------------------------------------------------------------

    def resized(self, cell: tuple[int, int]) -> list[bytes]:
        """The glyphs in a cell at least as large, drawn from the top left corner."""
        w, h = cell
        out = []
        for g in range(len(self.glyphs)):
            rows = self.bitmap(g)
            rows = [r + [0] * (w - len(r)) for r in rows] + [[0] * w for _ in range(h - len(rows))]
            out.append(_pack(rows, w, self.depth, (w * h * self.depth + 7) // 8))
        return out

    def with_layer(self, layer: "Nftr") -> "Nftr":
        """This font (the base's: the game's, for a language on the retail) with the glyphs of a
        layer in place of its own or added: the cell takes the larger of both, the glyphs keep
        their indices (the FINF's glyph for an unknown code stays right), and the header is the
        layer's (baseline, line height), as the letters of the translation were drawn for it."""
        if layer.depth != self.depth:
            raise FontError(f"the layer has {layer.depth} bpp, the font {self.depth}")
        cell = (max(self.cell[0], layer.cell[0]), max(self.cell[1], layer.cell[1]))
        finf = list(layer.finf)
        finf[1] = max(finf[1], self.finf[1])  # line height
        finf[2] = self.finf[2]  # glyph of an unknown code: an index of this font
        if len(finf) > 7:
            finf[7:10] = [cell[1], cell[0], layer.baseline]
        out = Nftr(layer.version, finf, cell, layer.baseline, max(self.max_width, layer.max_width),
                   self.depth, layer.rotation)
        out.glyphs, out.widths = self.resized(cell), list(self.widths)
        out.cmap, out.codes = [list(b) for b in self.cmap], dict(self.codes)
        new = layer.resized(cell)
        by_glyph = {}
        for c, g in layer.codes.items():
            by_glyph.setdefault(g, []).append(c)
        for g, codes in sorted(by_glyph.items()):
            mine = {self.codes[c] for c in codes if c in self.codes}
            if len(mine) == 1:  # a letter the font has: drawn anew
                at = mine.pop()
            else:
                at = len(out.glyphs)
                out.glyphs.append(b"")
                out.widths.append((0, 0, 0))
            out.glyphs[at], out.widths[at] = new[g], layer.widths[g]
            for c in codes:
                out.codes[c] = at
        return out

    def subset(self, wanted) -> "Nftr":
        """A layer with the glyphs of the codes in wanted (and the header of this font)."""
        out = Nftr(self.version, list(self.finf), self.cell, self.baseline, self.max_width, self.depth,
                   self.rotation, layer=True)
        index: dict[int, int] = {}
        for c, g in sorted(self.codes.items()):
            if c in wanted:
                if g not in index:
                    index[g] = len(out.glyphs)
                    out.glyphs.append(self.glyphs[g])
                    out.widths.append(self.widths[g])
                out.codes[c] = index[g]
        return out

    # --- JSON ----------------------------------------------------------------------------

    def to_json(self, names: dict[int, str] | None = None) -> str:
        """The font without its pixels (those go in the sheet): one glyph a line, in the order of
        the sheet, with its character (names: code -> character) where it is known."""
        names = names or {}
        head = {"layer": True} if self.layer else {}
        head |= {"version": f"{self.version >> 8}.{self.version & 0xFF}", "cell": list(self.cell),
                "depth": self.depth, "baseline": self.baseline, "max_width": self.max_width,
                "rotation": self.rotation, "finf": self.finf,
                "cmap": [[f"{a:X}", f"{b:X}", k] for a, b, k in self.cmap]}
        by_glyph: dict[int, list[int]] = {}
        for c, g in sorted(self.codes.items()):
            by_glyph.setdefault(g, []).append(c)
        lines = []
        for g, (left, width, adv) in enumerate(self.widths):
            cs = by_glyph.get(g, [])
            entry = {"code": " ".join(f"{c:X}" for c in cs)}
            if len(cs) == 1 and cs[0] in names:
                entry["char"] = names[cs[0]]
            entry |= {"left": left, "width": width, "advance": adv}
            lines.append("    " + json.dumps(entry, ensure_ascii=False))
        top = [f'  "{k}": {json.dumps(v, ensure_ascii=False)}' for k, v in head.items()]
        return "{\n" + ",\n".join(top) + ',\n  "glyphs": [\n' + ",\n".join(lines) + "\n  ]\n}\n"

    @classmethod
    def from_json(cls, text: str) -> "Nftr":
        """A font from its JSON, with blank glyphs (read_sheet draws them)."""
        try:
            m = json.loads(text)
            major, minor = (int(v) for v in m["version"].split("."))
            f = cls(major << 8 | minor, [int(v) for v in m["finf"]], tuple(m["cell"]), int(m["baseline"]),
                    int(m["max_width"]), int(m["depth"]), int(m["rotation"]))
            f.cmap = [[int(a, 16), int(b, 16), int(k)] for a, b, k in m["cmap"]]
            f.layer = bool(m.get("layer", False))
            glyphs = m["glyphs"]
        except (KeyError, ValueError, TypeError, json.JSONDecodeError) as e:
            raise FontError(f"not a font JSON ({e})") from None
        if f.depth not in (1, 2, 4) or len(f.cell) != 2 or len(f.finf) not in (7, 11):
            raise FontError("depth is 1, 2 or 4, cell [width, height], finf 7 or 11 numbers")
        for g, e in enumerate(glyphs):
            what = f"glyph {g + 1} ({e.get('char') or e.get('code') or 'no code'})"
            try:
                codes = [int(c, 16) for c in str(e.get("code", "")).split()]
                w = tuple(int(e[k]) for k in ("left", "width", "advance"))
            except (KeyError, ValueError):
                raise FontError(f"{what}: code is hexadecimal (\"B1\"), left/width/advance numbers") from None
            if not all(0 <= v <= 255 for v in w) or w[0] + w[1] > f.cell[0]:
                raise FontError(f"{what}: left + width is at most the cell width, {f.cell[0]}")
            for c in codes:
                if c in f.codes:
                    raise FontError(f"{what}: code {c:X} is also glyph {f.codes[c] + 1}")
                if not (c < 0x100 or 0x8140 <= c <= 0xFCFC):
                    raise FontError(f"{what}: code {c:X} is not one byte or Shift-JIS")
                f.codes[c] = g
            f.widths.append(w)
        f.glyphs = [bytes(f.glyph_size)] * len(f.widths)
        return f
