"""Tests for tools/ntr/bps.py."""

import random
import unittest

from ntr import bps


class BpsTest(unittest.TestCase):
    def roundtrip(self, source: bytes, target: bytes) -> bytes:
        patch = bps.encode(source, target)
        self.assertEqual(bps.apply(patch, source), target)
        self.assertEqual(bps.header(patch), (len(source), len(target)))
        return patch

    def test_kinds_of_change(self):
        rng = random.Random(1)
        source = bytes(rng.randrange(256) for _ in range(20000))
        self.roundtrip(source, source)
        self.roundtrip(source, b"")
        self.roundtrip(b"", b"new file")
        self.roundtrip(source, source[:5000] + b"XYZ" + source[5003:])  # same size
        moved = source[:3000] + b"texto traduzido, mais longo" + source[3010:] + b"fim"
        patch = self.roundtrip(source, moved)
        self.assertLess(len(patch), 200)  # copies, not the whole file
        self.roundtrip(source, source[10000:] + source[:10000])

    def test_rejects_another_source_and_damage(self):
        source, target = b"a" * 100 + b"original", b"a" * 100 + b"traduzido"
        patch = bytearray(bps.encode(source, target))
        with self.assertRaisesRegex(bps.BpsError, "another version"):
            bps.apply(bytes(patch), source[:-1] + b"X")
        patch[6] ^= 1
        with self.assertRaisesRegex(bps.BpsError, "damaged"):
            bps.apply(bytes(patch), source)
        with self.assertRaises(bps.BpsError):
            bps.apply(b"IPS1", source)

    def test_reads_target_copy(self):
        # The encoder does not write TargetCopy; other tools (flips, beat) do.
        out = bytearray(b"BPS1")
        for n in (0, 6, 0):
            bps._varint(n, out)
        bps._varint(0 << 2 | 1, out)
        out += b"a"
        bps._varint(4 << 2 | 3, out)
        bps._signed(0, out)
        import struct
        import zlib
        out += struct.pack("<II", zlib.crc32(b""), zlib.crc32(b"aaaaaa"))
        out += struct.pack("<I", zlib.crc32(out))
        self.assertEqual(bps.apply(bytes(out), b""), b"aaaaaa")


if __name__ == "__main__":
    unittest.main()
