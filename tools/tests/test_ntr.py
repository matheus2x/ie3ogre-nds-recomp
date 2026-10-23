"""Unit tests for tools/ntr using synthetic data only (no game data needed).

Run: python3 -m unittest discover -s tools/tests -t tools
"""

import struct
import unittest

import tempfile
from pathlib import Path

from ntr import blz
from ntr.pak import PakReader, rom_ranges, write_pak
from ntr.rom import NITROCODE, Rom


def blz_footer(enc_len: int, footer_len: int, extra: int) -> bytes:
    return struct.pack("<II", (footer_len << 24) | enc_len, extra)


class BlzTest(unittest.TestCase):
    def test_literals_and_back_reference(self):
        # The stream is read backwards: flag byte, literals 'a','b','c' (written to
        # the end of the output), then a 3-byte copy at distance 3.
        pair = ((3 - 3) << 12) | (3 - 3)
        enc = struct.pack("<H", pair) + b"cba" + bytes([0b00010000])
        prefix = b"xyz"
        data = prefix + enc + blz_footer(len(enc) + 8, 8, 0)
        out = blz.decompress(data)
        self.assertEqual(out[:3], prefix)
        self.assertEqual(out[-6:], b"cbacba")

    def test_extra_grows_output(self):
        out = blz.decompress(b"\0" * 8 + blz_footer(8, 8, 16))
        self.assertEqual(len(out), 8 + 8 + 16)

    def test_rejects_bad_footer(self):
        with self.assertRaises(blz.BlzError):
            blz.decompress(b"\0" * 4 + blz_footer(0xFFFFFF, 8, 0))


def make_rom() -> bytes:
    rom = bytearray(0x600)
    rom[0:12] = b"TESTGAME\0\0\0\0"
    rom[12:16] = b"BOEJ"
    rom[16:18] = b"HF"

    # Module params at offset 0, NitroCode right after; not compressed.
    arm9 = bytearray(0x40)
    struct.pack_into("<7I", arm9, 0, 0, 0, 0, 0, 0, 0, 0x05047531)
    arm9[0x1C:0x24] = NITROCODE

    # FNT: root (0xF000) contains "dir/", which contains "a.bin" (file id 0).
    fnt = bytearray(struct.pack("<IHH", 0x10, 0, 2) + struct.pack("<IHH", 0x17, 0, 0xF000))
    fnt += bytes([0x83]) + b"dir" + struct.pack("<H", 0xF001) + b"\0"
    fnt += bytes([0x05]) + b"a.bin" + b"\0"

    fat = struct.pack("<II", 0x500, 0x504)
    for off, blob in ((0x200, arm9), (0x300, fnt), (0x400, fat), (0x500, b"DATA")):
        rom[off:off + len(blob)] = blob
    struct.pack_into("<16I", rom, 0x20,
                     0x200, 0x2000800, 0x2000000, len(arm9), 0x200, 0, 0x2380000, 0,
                     0x300, len(fnt), 0x400, len(fat), 0x480, 0, 0, 0)
    return bytes(rom)


class RomTest(unittest.TestCase):
    def test_header_and_fs(self):
        rom = Rom(make_rom())
        self.assertEqual(rom.header.game_code, "BOEJ")
        self.assertEqual(rom.module_params().sdk_version_str, "5.4 (relstep 30001)")
        self.assertEqual(rom.files(), [("dir/a.bin", 0)])
        self.assertEqual(rom.file_data(0), b"DATA")
        self.assertEqual(rom.overlays9(), [])


class PakTest(unittest.TestCase):
    def test_round_trip_keeps_rom_offsets(self):
        data = bytearray(make_rom()) + b"\xff" * 0x10000
        rom = Rom(bytes(data))
        self.assertEqual(rom_ranges(rom)[0][0], 0)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.pak"
            write_pak(rom, "00" * 20, path)
            pak = PakReader(path.read_bytes())
        self.assertEqual(pak.game_code, "BOEJ")
        self.assertEqual(pak.rom_size, len(data))
        self.assertEqual(pak.read(0, 0x600), bytes(data[:0x600]))
        self.assertEqual(pak.read(0x500, 4), b"DATA")
        self.assertEqual(pak.read(0x8000, 4), b"\xff" * 4)  # outside every range



class KnownDumpsTest(unittest.TestCase):
    def test_runtime_table_matches_catalog(self):
        """The executable identifies dumps on its own (runtime/src/setup.cpp)."""
        import json
        import re
        root = Path(__file__).resolve().parents[2]
        catalog = json.loads((root / "config" / "boej" / "dumps.json").read_text())
        table = re.findall(r'\{"([0-9a-f]{40})", "([\w-]+)", (true|false)\}',
                           (root / "runtime" / "src" / "setup.cpp").read_text())
        expected = [(d["sha1"], d["variant"], "true" if d["variant"] == catalog["canonical"] else "false")
                    for d in catalog["dumps"]]
        self.assertEqual(sorted(table), sorted(expected))

if __name__ == "__main__":
    unittest.main()
