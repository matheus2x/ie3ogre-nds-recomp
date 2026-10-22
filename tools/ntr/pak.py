"""Game data package (boej.pak): the parts of the cartridge image the game reads.

The runtime reads the cartridge by ROM offset (boot, overlays, NitroFS), so the package
keeps ROM offsets: it is a table of ROM ranges plus their bytes, without the unused
padding. Reads outside every range return 0xFF, like unused cartridge space.

Layout (little endian):
  0x00  magic "NTRPAK\\0\\0"
  0x08  u32 version (1)
  0x0C  game code (4 ASCII bytes)
  0x10  SHA-1 of the source dump (20 bytes)
  0x24  u32 reserved
  0x28  u64 size of the source dump
  0x30  u32 range count, u32 reserved
  0x38  ranges: u64 rom_offset, u64 size, u64 offset in this file
  data  each range starts on a 4 KiB boundary
"""

import struct
from pathlib import Path

from .rom import Rom

MAGIC = b"NTRPAK\0\0"
VERSION = 1
HEADER = struct.Struct("<8sI4s20sIQII")
RANGE = struct.Struct("<QQQ")
ALIGN = 0x1000
MERGE_GAP = 0x1000  # neighbouring ranges closer than this are stored as one


class PakError(ValueError):
    pass


def rom_ranges(rom: Rom) -> list[tuple[int, int]]:
    """(start, end) of every region the game can read, merged and sorted."""
    h = rom.header
    spans = [(0, 0x4000)]  # header and secure area
    for off, size in ((h.arm9_offset, h.arm9_size), (h.arm7_offset, h.arm7_size), (h.fnt_offset, h.fnt_size),
                      (h.fat_offset, h.fat_size), (h.ovt9_offset, h.ovt9_size), (h.ovt7_offset, h.ovt7_size)):
        if size:
            spans.append((off, off + size))
    banner = struct.unpack_from("<I", rom.data, 0x68)[0]
    if banner:
        spans.append((banner, banner + 0x1240))  # largest banner version
    spans += [(start, end) for start, end in rom.fat if end > start]

    merged: list[tuple[int, int]] = []
    for start, end in sorted(spans):
        end = min(end, len(rom.data))
        if start >= end:
            continue
        if merged and start <= merged[-1][1] + MERGE_GAP:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    return merged


def write_pak(rom: Rom, source_sha1: str, path: Path) -> int:
    """Writes the package; returns its size in bytes."""
    ranges = rom_ranges(rom)
    table_end = HEADER.size + RANGE.size * len(ranges)
    pos = (table_end + ALIGN - 1) // ALIGN * ALIGN
    entries = []
    for start, end in ranges:
        entries.append((start, end - start, pos))
        pos = (pos + end - start + ALIGN - 1) // ALIGN * ALIGN

    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "wb") as f:
        f.write(HEADER.pack(MAGIC, VERSION, rom.header.game_code.encode("ascii"), bytes.fromhex(source_sha1), 0,
                            len(rom.data), len(entries), 0))
        for entry in entries:
            f.write(RANGE.pack(*entry))
        for start, size, offset in entries:
            f.seek(offset)
            f.write(rom.data[start:start + size])
        f.truncate(pos)
    tmp.replace(path)
    return pos


class PakReader:
    """Reads a package by ROM offset (used by the tests and for checks)."""

    def __init__(self, data: bytes):
        magic, version, code, sha1, _, rom_size, count, _ = HEADER.unpack_from(data, 0)
        if magic != MAGIC or version != VERSION:
            raise PakError("not a version 1 game data package")
        self.game_code = code.decode("ascii")
        self.source_sha1 = sha1.hex()
        self.rom_size = rom_size
        self.data = data
        self.ranges = [RANGE.unpack_from(data, HEADER.size + i * RANGE.size) for i in range(count)]

    def read(self, offset: int, size: int) -> bytes:
        out = bytearray(b"\xff" * size)
        for start, length, at in self.ranges:
            lo, hi = max(offset, start), min(offset + size, start + length)
            if lo < hi:
                out[lo - offset:hi - offset] = self.data[at + lo - start:at + hi - start]
        return bytes(out)
