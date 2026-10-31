"""Tests for tools/ntr/font.py and tools/ntr/png.py (no ROM needed)."""

import json
import struct
import unittest
import zlib

from ntr.font import FontError, Nftr
from ntr.png import PngError, read_png, write_png


def tiny(version: int = 0x0102) -> Nftr:
    """Three glyphs of 4x5, 1 bpp: "A" and "B" in a type-0 block, 0xB1 in a type-1 block."""
    f = Nftr(version, [0, 5, 0, 0, 4, 4, 2] + ([5, 4, 4, 0] if version == 0x0102 else []), (4, 5), 4, 4, 1, 0)
    f.glyphs = [f.pack([[1, 0, 0, 1]] * 5), f.pack([[0, 1, 1, 0]] * 5), f.pack([[1, 1, 1, 1]] + [[0] * 4] * 4)]
    f.widths = [(0, 4, 5), (0, 3, 4), (1, 2, 3)]
    f.cmap = [[0x41, 0x42, 0], [0xA1, 0xDF, 1]]
    f.codes = {0x41: 0, 0x42: 1, 0xB1: 2}
    return f


def rgb_png(w: int, h: int, px) -> bytes:
    """An RGB PNG with the Sub filter on every row, as an image editor may save it."""
    raw = b""
    for y in range(h):
        line = bytes(v for x in range(w) for v in px(x, y))
        raw += b"\x01" + bytes((line[i] - (line[i - 3] if i >= 3 else 0)) & 0xFF for i in range(len(line)))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


class FontTest(unittest.TestCase):
    def test_round_trip(self):
        for version in (0x0101, 0x0102):
            f = tiny(version)
            data = f.write()
            self.assertEqual(data[:4], b"RTFN")
            self.assertEqual(struct.unpack_from("<I", data, 8)[0], len(data))
            back = Nftr.read(data)
            self.assertEqual((back.glyphs, back.widths, back.codes, back.cmap), (f.glyphs, f.widths, f.codes, f.cmap))
            self.assertEqual(back.write(), data)

    def test_json_and_sheet(self):
        f = tiny()
        j = f.to_json({0x41: "A", 0x42: "B", 0xB1: "ñ"})
        self.assertEqual(json.loads(j)["glyphs"][2], {"code": "B1", "char": "ñ", "left": 1, "width": 2, "advance": 3})
        back = Nftr.from_json(j)
        back.glyphs = back.read_sheet(f.sheet())
        self.assertEqual(back.write(), f.write())

    def test_sheet_from_an_editor(self):
        """An RGB sheet: dark is ink, the tint and the grid are not."""
        f = tiny()
        _, _, rows = read_png(f.sheet())
        drawn = rgb_png(len(rows[0]), len(rows), lambda x, y: rows[y][x][:3])
        self.assertEqual(f.read_sheet(drawn), f.glyphs)
        with self.assertRaises(FontError):
            f.read_sheet(rgb_png(3, 3, lambda x, y: (0, 0, 0)))  # not the size of the sheet

    def test_new_code_goes_to_a_type_2_block(self):
        f = tiny()
        f.glyphs.append(f.pack([[1] * 4] * 5))
        f.widths.append((0, 4, 5))
        f.codes[0x8250] = 3  # a full-width letter no block covers
        back = Nftr.read(f.write())
        self.assertEqual(back.codes[0x8250], 3)
        self.assertEqual(back.cmap[-1], [0, 0xFFFF, 2])

    def test_layer_over_a_font(self):
        """The letters of a translation over the game's font: replaced, added, a larger cell."""
        game = tiny(0x0101)
        layer = Nftr(0x0102, [0, 6, 0, 0, 4, 4, 2, 6, 4, 5, 0], (4, 6), 5, 4, 1, 0, layer=True)
        layer.glyphs = [layer.pack([[1, 1, 1, 1]] * 6), layer.pack([[0, 0, 0, 0]] * 5 + [[1, 1, 1, 1]])]
        layer.widths = [(0, 4, 5), (0, 2, 3)]
        layer.codes = {0x41: 0, 0x20: 1}  # "A" drawn anew, " " added
        f = Nftr.read(game.with_layer(layer).write())
        self.assertEqual((f.cell, f.baseline, f.version, len(f.glyphs)), ((4, 6), 5, 0x0102, 4))
        self.assertEqual(f.codes[0x41], 0)  # in place: the game's indices stay
        self.assertEqual(f.bitmap(0), [[1, 1, 1, 1]] * 6)
        self.assertEqual(f.bitmap(1)[:5], [[0, 1, 1, 0]] * 5)  # the game's "B", one empty row below
        self.assertEqual(f.bitmap(f.codes[0x20])[5], [1, 1, 1, 1])
        self.assertEqual(f.widths[f.codes[0x20]], (0, 2, 3))
        sub = f.subset({0x20, 0x41})
        self.assertTrue(sub.layer)
        self.assertEqual(set(sub.codes), {0x20, 0x41})
        self.assertTrue(json.loads(sub.to_json())["layer"])

    def test_bad_fonts(self):
        f = tiny()
        del f.codes[0x42]  # a type-0 block needs every code
        with self.assertRaises(FontError):
            f.write()
        j = json.loads(tiny().to_json())
        for bad in ({"code": "zz", "left": 0, "width": 1, "advance": 1},
                    {"code": "41", "left": 0, "width": 1, "advance": 1},  # 41 is also glyph 1
                    {"code": "C3", "left": 3, "width": 3, "advance": 1}):  # wider than the cell
            with self.assertRaises(FontError, msg=bad):
                Nftr.from_json(json.dumps(j | {"glyphs": j["glyphs"] + [bad]}))


class PngTest(unittest.TestCase):
    def test_indexed_and_rgb(self):
        pal = [(255, 255, 255), (0, 0, 0)]
        png = write_png(3, 2, pal, [bytes((0, 1, 0)), bytes((1, 1, 0))], transparent=False)
        w, h, rows = read_png(png)
        self.assertEqual((w, h), (3, 2))
        self.assertEqual(rows[1][0], (0, 0, 0, 255))
        self.assertEqual(read_png(write_png(1, 1, pal, [b"\0"]))[2][0][0][3], 0)  # index 0 see-through
        _, _, rows = read_png(rgb_png(4, 2, lambda x, y: (x * 60, y * 100, 7)))
        self.assertEqual(rows[1][3], (180, 100, 7, 255))
        with self.assertRaises(PngError):
            read_png(b"GIF89a")


if __name__ == "__main__":
    unittest.main()
