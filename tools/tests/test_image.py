"""Tests for tools/ntr/image.py and the edits of tools/image.py, with synthetic data only."""

import struct
import unittest

import image as imagetool
from ntr import lz10
from ntr.image import Picture, guess_width, read_spf, read_spl, sections, write_spf, write_spl


def pac(parts: list[bytes]) -> bytes:
    """A PAC as the game writes it: count, (offset, size) per section, end - 0x20; 0x20-aligned."""
    head = struct.pack("<I", len(parts))
    body = b""
    for p in parts:
        head += struct.pack("<II", 0x20 + len(body), len(p))
        body += p + bytes(-len(p) % 0x20)
    end = 0x20 + len(body) - (-len(parts[-1]) % 0x20)
    return head + struct.pack("<I", end - 0x20) + bytes(0x20 - len(head) - 4) + body


def sfp(entries: list[tuple[str, bytes]]) -> tuple[bytes, bytes]:
    """An index and its data block (the .SPL/.SPD pair)."""
    names = b"".join(n.encode() + b"\0" for n, _ in entries)
    first = 0x20 + 16 * len(entries)
    index = bytearray(b"SFP\0" + struct.pack("<III", 0, 5, 0x20) + bytes(16))
    blob = bytearray(b"SFP\0" + struct.pack("<III", 0, 5, 0x20) + bytes(0x400 - 16))
    at = first
    for name, d in entries:
        index += struct.pack("<IIII", at, len(d), len(blob) // 0x20, 0)
        at += len(name) + 1
        blob += d + bytes(-len(d) % 0x20)
    index += names
    struct.pack_into("<I", index, 0x10, len(index))
    struct.pack_into("<I", blob, 0x10, len(blob))
    return bytes(index), bytes(blob)


def texture(w: int, h: int, frames: list[tuple[int, int, int, int]]) -> bytes:
    px = bytes((x + y) % 16 | ((x + 1 + y) % 16) << 4 for y in range(h) for x in range(0, w, 2))
    params = struct.pack("<HBBI", len(frames), w.bit_length() - 4, h.bit_length() - 4, 3)
    params += b"".join(struct.pack("<4H", *f) for f in frames)
    return pac([px, struct.pack("<16H", *range(16)), params])


def tiled(cols: int, rows: int) -> bytes:
    """A 4 bpp 2D picture: a gradient tile, its mirror image and a plain one, in palette bank 1."""
    grad = bytes(x // 2 | (x // 2) << 4 for x in range(32))
    plain = bytes([0x33] * 32)
    cells = [(0 | 1 << 12) if (c + r) % 3 == 0 else (0 | 1 << 10 | 1 << 12) if (c + r) % 3 == 1 else (1 | 1 << 12)
             for r in range(rows) for c in range(cols)]
    return pac([struct.pack("<32H", *range(32)), struct.pack(f"<{len(cells)}H", *cells), grad + plain])


class PacTest(unittest.TestCase):
    def test_texture_round_trip(self):
        d = texture(16, 8, [(0, 0, 16, 4), (0, 4, 16, 4)])
        p = Picture(d, "3d")
        self.assertEqual((p.w, p.h, p.frames), (16, 8, [(0, 0, 16, 4), (0, 4, 16, 4)]))
        self.assertEqual(p.rows[1][:3], [1, 2, 3])
        self.assertEqual(p.encode(), d)
        p.rows[0][0] = 9
        self.assertEqual(Picture(p.encode(), "3d").rows[0][0], 9)

    def test_tiles_round_trip_and_width(self):
        d = tiled(6, 2)
        p = Picture(d, "2d", 6)
        self.assertEqual(p.rows[0][:8], [16] * 4 + [17] * 4)
        self.assertEqual(p.rows[0][8:16], [17] * 4 + [16] * 4)  # the same tile, flipped
        self.assertEqual(Picture(p.encode(), "2d", 6).rows, p.rows)
        self.assertEqual(len(sections(p.encode())[2]), 64)  # the flipped cells reuse the tile

    def test_guess_width(self):
        # two plain tiles: 4 cells per row draw A A B B, any other width breaks the halves
        cells = [0, 0, 1, 1] * 2
        d = pac([bytes(32), struct.pack("<8H", *cells), bytes([0x11] * 32) + bytes([0x22] * 32)])
        self.assertEqual(guess_width(Picture(d, "2d", 4)), 4)

    def test_new_tile_keeps_bank(self):
        p = Picture(tiled(6, 2), "2d", 6)
        p.rows[0][0] = 16 + 7
        q = Picture(p.encode(), "2d", 6)
        self.assertEqual(q.rows, p.rows)
        self.assertEqual(len(sections(p.encode())[2]), 96)


class SfpTest(unittest.TestCase):
    def test_replace_entry_moves_the_rest(self):
        a, b = texture(16, 8, [(0, 0, 16, 8)]), texture(16, 16, [(0, 0, 16, 16)])
        spl, spd = sfp([("A.PAC_", lz10.compress(a)), ("B.PAC_", b)])
        self.assertEqual([n for n, _ in read_spl(spl, spd)], ["A.PAC_", "B.PAC_"])
        bigger = lz10.compress(texture(32, 32, [(0, 0, 32, 32)]))
        spl2, spd2 = write_spl(spl, spd, {"A.PAC_": bigger})
        self.assertEqual(dict(read_spl(spl2, spd2)), {"A.PAC_": bigger, "B.PAC_": b})
        self.assertEqual(struct.unpack_from("<I", spd2, 0x10)[0], len(spd2))
        self.assertEqual(write_spl(spl, spd, {}), (spl, spd))

    def test_spf_index_and_data_in_one(self):
        spl, spd = sfp([("X.PAC", tiled(6, 2))])
        index = bytearray(spl + bytes(-len(spl) % 0x20))
        struct.pack_into("<I", index, 0x10, len(index))
        spf = lz10.compress(bytes(index) + spd)
        self.assertEqual(read_spf(spf), [("X.PAC", tiled(6, 2))])
        new = write_spf(spf, {"X.PAC": tiled(3, 4)})
        self.assertEqual(read_spf(new), [("X.PAC", tiled(3, 4))])


class EditTest(unittest.TestCase):
    def test_frames_are_relative(self):
        p = Picture(texture(16, 8, [(0, 0, 16, 4), (0, 4, 16, 4)]), "3d")
        out = imagetool.placed(p, {"frames": [1], "erase": [2, 1, 3, 2], "from": 9})
        self.assertEqual(out, [{"erase": [2, 5, 3, 2], "from": 9}])
        with self.assertRaises(imagetool.ImageError):
            imagetool.placed(p, {"frames": [0], "box": [0, 0, 16, 5]})

    def test_erase(self):
        painter = imagetool.Painter(None)
        p = Picture(texture(16, 8, [(0, 0, 16, 8)]), "3d")
        p.rows = [[5] * 16 for _ in range(8)]
        p.rows[2][4:7] = [15, 15, 1]
        painter.paint(p, {"erase": [3, 2, 6, 1], "ink": [15, 1]})
        self.assertEqual(p.rows[2], [5] * 16)
        p.rows[3] = list(range(16))
        painter.paint(p, {"erase": [2, 3, 4, 1], "from": 10})
        self.assertEqual(p.rows[3][:7], [0, 1, 10, 10, 10, 10, 6])


if __name__ == "__main__":
    unittest.main()
