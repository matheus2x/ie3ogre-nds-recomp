"""The images of the game (data_iz/pic2d, pic3d) and the text drawn on them (docs/modding/TRANSLATION-STATUS.md).

SFP archive: "SFP\\0", u32 0, u32 5, u32 0x20, u32 size at 0x10, then from 0x20 entries of
(u32 offset of the name, u32 size, u32 offset / 0x20, u32 0) and the names. The data is a second
SFP block of the same header (its size at 0x10) with the entries from 0x400, each one 0x20-aligned.
pic3d keeps the index in a .SPL and the data in a .SPD; pic2d keeps both in one .SPF_ (LZ10),
the data right after the index (whose size is the u32 at 0x10).

PAC: u32 count (3), (u32 offset, u32 size) of each section, u32 end; sections 0x20-aligned.
- 2D (pic2d .PAC): palette (BGR555), map (u16 per 8x8 cell: tile, h/v flip in bits 10/11, palette
  bank in 12-15), tiles (4 bpp, or 8 bpp when the palette has more than 128 colours). The map
  does not say its width: guess_width finds it by the seams between cells.
- 3D (pic3d .PAC_, LZ10): texture (4 bpp, linear), palette, then u16 frame count, u8 log2(width) - 3,
  u8 log2(height) - 3, u32, and (x, y, w, h) u16 of each frame (the pieces the menu shows).

Fonts (data_iz/font/*.NFTR): CGLP holds the glyphs (cell width, height, bytes per glyph, bits per
pixel, rows MSB first); CWDH gives (left, glyph width, advance) of each glyph.
"""

import struct

from . import lz10
from .png import read_png, write_png  # noqa: F401 (write_png: also imported from here)


class ImageError(ValueError):
    pass


def _align(n: int, a: int = 0x20) -> int:
    return -(-n // a) * a


# --- SFP ------------------------------------------------------------------------------

def _entries(index: bytes, data: bytes, base: int) -> list[tuple[str, bytes]]:
    if index[:4] != b"SFP\0":
        raise ImageError("not an SFP archive")
    end = struct.unpack_from("<I", index, 0x20)[0]  # the first name follows the entries
    out = []
    for k in range(0x20, end, 16):
        name, size, at, _ = struct.unpack_from("<4I", index, k)
        out.append((index[name:index.index(b"\0", name)].decode("ascii"), data[base + at * 0x20:][:size]))
    return out


def _rebuild(index: bytes, data: bytes, base: int, new: dict[str, bytes]) -> tuple[bytearray, bytearray]:
    """The index and the data block with some entries replaced (the rest laid again in order)."""
    old = _entries(index, data, base)
    idx = bytearray(index)
    blob = bytearray(data[base:base + 0x400])
    for k, (name, d) in enumerate(old):
        d = new.get(name, d)
        at = len(blob)
        struct.pack_into("<II", idx, 0x24 + 16 * k, len(d), at // 0x20)
        blob += d + bytes(_align(len(d)) - len(d))
    struct.pack_into("<I", blob, 0x10, len(blob))
    return idx, blob


def read_spf(spf: bytes) -> list[tuple[str, bytes]]:
    """Entries of a pic2d .SPF_ (compressed, but for a few)."""
    d = spf if spf[:4] == b"SFP\0" else lz10.decompress(spf)
    return _entries(d, d, struct.unpack_from("<I", d, 0x10)[0])


def write_spf(spf: bytes, new: dict[str, bytes]) -> bytes:
    plain = spf[:4] == b"SFP\0"
    d = spf if plain else lz10.decompress(spf)
    base = struct.unpack_from("<I", d, 0x10)[0]
    idx, blob = _rebuild(d[:base], d, base, new)
    return bytes(idx + blob) if plain else lz10.compress(bytes(idx + blob))


def read_spl(spl: bytes, spd: bytes) -> list[tuple[str, bytes]]:
    """Entries of a pic3d .SPL/.SPD pair, as stored (a .PAC_ is LZ10)."""
    return _entries(spl, spd, 0)


def write_spl(spl: bytes, spd: bytes, new: dict[str, bytes]) -> tuple[bytes, bytes]:
    idx, blob = _rebuild(spl, spd, 0, new)
    return bytes(idx), bytes(blob)


# --- PAC ------------------------------------------------------------------------------

def sections(pac: bytes) -> list[bytes]:
    n = struct.unpack_from("<I", pac, 0)[0]
    return [pac[o:o + s] for o, s in (struct.unpack_from("<II", pac, 4 + 8 * i) for i in range(n))]


def join_sections(pac: bytes, parts: list[bytes]) -> bytes:
    """A PAC like pac with other sections (its last word keeps its distance to the end)."""
    n = struct.unpack_from("<I", pac, 0)[0]
    o, s = struct.unpack_from("<II", pac, 4 + 8 * (n - 1))
    slack = o + s - struct.unpack_from("<I", pac, 4 + 8 * n)[0]
    head = bytearray(struct.pack("<I", n))
    body = bytearray()
    at = _align(8 + 8 * n)
    for p in parts:
        head += struct.pack("<II", at + len(body), len(p))
        body += p + bytes(_align(len(p)) - len(p))
    head += struct.pack("<I", at + len(body) - (_align(len(parts[-1])) - len(parts[-1])) - slack)
    return bytes(head + bytes(at - len(head)) + body)


def colours(pal: bytes) -> list[tuple[int, int, int]]:
    return [((c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31)
            for c in struct.unpack_from(f"<{len(pal) // 2}H", pal)]


class Picture:
    """Pixels as palette indices (rows of lists), from a 2D or a 3D PAC, and back."""

    def __init__(self, pac: bytes, kind: str, width: int | None = None):
        self.pac, self.kind = pac, kind
        parts = sections(pac)
        if kind == "3d":
            px, self.pal, self.params = parts
            self.w, self.h = 8 << self.params[2], 8 << self.params[3]
            self.bpp = len(px) * 8 // (self.w * self.h)
            if self.bpp != 4:
                raise ImageError(f"{self.bpp} bpp texture")
            self.rows = [[px[(y * self.w + x) // 2] >> (4 * (x & 1)) & 15 for x in range(self.w)]
                         for y in range(self.h)]
            n = struct.unpack_from("<H", self.params)[0]
            self.frames = [struct.unpack_from("<4H", self.params, 8 + 8 * i) for i in range(n)]
            return
        self.pal, mp, self.tiles = parts[:3]
        self.bpp = 8 if len(self.pal) > 0x100 else 4
        self.map = list(struct.unpack_from(f"<{len(mp) // 2}H", mp))
        self.cols = width or guess_width(self)
        self.w, self.h = self.cols * 8, -(-len(self.map) // self.cols) * 8
        self.rows = self._draw(self.cols)
        self.frames = [(0, 0, self.w, self.h)]

    def _tile(self, t: int) -> list[list[int]]:
        size = 8 * self.bpp
        tb = self.tiles[t * size:t * size + size]
        if len(tb) < size:
            return [[0] * 8 for _ in range(8)]
        if self.bpp == 8:
            return [list(tb[y * 8:y * 8 + 8]) for y in range(8)]
        return [[tb[y * 4 + x // 2] >> (4 * (x & 1)) & 15 for x in range(8)] for y in range(8)]

    def _draw(self, cols: int) -> list[list[int]]:
        rows = [[0] * (cols * 8) for _ in range(-(-len(self.map) // cols) * 8)]
        for i, e in enumerate(self.map):
            t, hf, vf, bank = e & 0x3FF, e >> 10 & 1, e >> 11 & 1, e >> 12
            tile = self._tile(t)
            for y in range(8):
                for x in range(8):
                    c = tile[7 - y if vf else y][7 - x if hf else x]
                    rows[i // cols * 8 + y][i % cols * 8 + x] = c if self.bpp == 8 else bank * 16 + c
        return rows

    def bank(self, x: int, y: int) -> int:
        """First palette index of the 16 colours the pixel can take (0 in 8 bpp and in 3D)."""
        if self.kind == "3d" or self.bpp == 8:
            return 0
        return (self.map[y // 8 * self.cols + x // 8] >> 12) * 16

    def encode(self) -> bytes:
        if self.kind == "3d":
            px = bytearray(self.w * self.h // 2)
            for y, row in enumerate(self.rows):
                for x, c in enumerate(row):
                    px[(y * self.w + x) // 2] |= (c & 15) << (4 * (x & 1))
            return join_sections(self.pac, [bytes(px), self.pal, self.params])
        tiles: dict[bytes, int] = {}
        out = bytearray()
        new_map = []
        for i, e in enumerate(self.map):
            ty, tx = i // self.cols * 8, i % self.cols * 8
            cell = [[self.rows[ty + y][tx + x] for x in range(8)] for y in range(8)]
            if self.bpp == 4:
                cell = [[c & 15 for c in r] for r in cell]
            found = None
            for hf in (0, 1):
                for vf in (0, 1):
                    key = self._pack([r[::-1] if hf else r for r in (cell[::-1] if vf else cell)])
                    if key in tiles and found is None:
                        found = tiles[key] | hf << 10 | vf << 11
            if found is None:
                key = self._pack(cell)
                tiles[key] = found = len(tiles)
                out += key
            new_map.append(found | e & 0xF000)
        if len(tiles) > 0x400:
            raise ImageError("more than 1024 tiles")
        return join_sections(self.pac, [self.pal, struct.pack(f"<{len(new_map)}H", *new_map), bytes(out)])

    def _pack(self, cell: list[list[int]]) -> bytes:
        if self.bpp == 8:
            return bytes(c for r in cell for c in r)
        return bytes(r[x] | r[x + 1] << 4 for r in cell for x in range(0, 8, 2))

    def draw_png(self, png: bytes) -> int:
        """The picture drawn as a PNG (export's, edited; any PNG an editor saves, of the same
        size): each pixel takes the palette index of its colour among those it can take (bank),
        the one it had when the colour is the same, 0 when it is see-through. The number of
        pixels whose colour is not in the palette (they take the nearest)."""
        w, h, rows = read_png(png)
        if (w, h) != (self.w, self.h):
            raise ImageError(f"the PNG is {w}x{h}, the picture {self.w}x{self.h}")
        pal = colours(self.pal)
        span = 256 if self.bpp == 8 else 16
        near = 0
        for y in range(h):
            for x in range(w):
                r, g, b, a = rows[y][x]
                old = self.rows[y][x]
                if a < 128:
                    self.rows[y][x] = self.bank(x, y)
                    continue
                if old < len(pal) and pal[old] == (r, g, b):
                    continue
                first = self.bank(x, y)
                choices = range(first + 1, min(first + span, len(pal)))
                best = min(choices, key=lambda i: (pal[i][0] - r) ** 2 + (pal[i][1] - g) ** 2 + (pal[i][2] - b) ** 2,
                           default=first)
                near += pal[best] != (r, g, b)
                self.rows[y][x] = best
        return near

    def png(self, scale: int = 1) -> bytes:
        """Indexed PNG (index 0 transparent), each pixel scale x scale."""
        pal = colours(self.pal)
        rows = [bytes(c for c in row for _ in range(scale)) for row in self.rows for _ in range(scale)]
        return write_png(self.w * scale, self.h * scale, pal, rows)


def guess_width(p: Picture) -> int:
    """Cells per row of a 2D map: the width whose cells meet with the fewest changes of colour."""
    n, best = len(p.map), None
    for cols in range(2, min(n, 64) + 1):
        if n % cols:
            continue
        rows = p._draw(cols)
        diff = cnt = 0
        for y, row in enumerate(rows):
            for x in range(8, len(row), 8):
                diff += row[x] != row[x - 1]
                cnt += 1
            if y and y % 8 == 0:
                diff += sum(a != b for a, b in zip(row, rows[y - 1]))
                cnt += len(row)
        if cnt and (best is None or diff / cnt < best[0]):
            best = (diff / cnt, cols)
    return best[1] if best else n


# --- fonts and text -------------------------------------------------------------------

class Glyphs:
    """The bitmaps of an NFTR font, by character code (Shift-JIS as the game writes it)."""

    def __init__(self, nftr: bytes):
        from .text import Font
        self.font = Font(nftr)
        finf = struct.unpack_from("<H", nftr, 0x0C)[0]
        cglp, cwdh, _ = struct.unpack_from("<III", nftr, finf + 0x10)
        self.cell_w, self.cell_h = nftr[cglp], nftr[cglp + 1]
        self.size, self.depth = struct.unpack_from("<H", nftr, cglp + 2)[0], nftr[cglp + 6]
        self.data = nftr[cglp + 8:]
        self.left: dict[int, int] = {}
        p = cwdh - 8
        while p:
            first, last, nxt = struct.unpack_from("<HHI", nftr, p + 8)
            for k in range(last - first + 1):
                self.left[first + k] = nftr[p + 0x10 + 3 * k]
            p = nxt - 8 if nxt else 0

    def bitmap(self, code: int) -> list[list[int]]:
        """Rows of the glyph (0 = no ink; >0 = ink, 1..3 in a 2 bpp font)."""
        g = self.font.glyph.get(code)
        if g is None:
            raise ImageError(f"no glyph for {code:#x}")
        bits = self.data[g * self.size:(g + 1) * self.size]
        out = []
        for y in range(self.cell_h):
            row = []
            for x in range(self.cell_w):
                k = (y * self.cell_w + x) * self.depth
                row.append(bits[k // 8] >> (8 - self.depth - k % 8) & ((1 << self.depth) - 1))
            out.append(row)
        return out
