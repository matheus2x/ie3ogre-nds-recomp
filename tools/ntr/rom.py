"""Nintendo DS (NTR) ROM parsing: header, file system (FNT/FAT) and overlay tables."""

import struct
from dataclasses import dataclass, field

from . import blz

NITROCODE = struct.pack("<II", 0xDEC00621, 0x2106C0DE)
OVERLAY_FLAG_COMPRESSED = 0x01


class RomError(ValueError):
    pass


@dataclass(frozen=True)
class Header:
    title: str
    game_code: str
    maker_code: str
    unit_code: int
    rom_version: int
    arm9_offset: int
    arm9_entry: int
    arm9_ram: int
    arm9_size: int
    arm7_offset: int
    arm7_entry: int
    arm7_ram: int
    arm7_size: int
    fnt_offset: int
    fnt_size: int
    fat_offset: int
    fat_size: int
    ovt9_offset: int
    ovt9_size: int
    ovt7_offset: int
    ovt7_size: int
    rom_size: int

    @classmethod
    def parse(cls, raw: bytes) -> "Header":
        if len(raw) < 0x200:
            raise RomError("file too small for an NDS header")
        regions = struct.unpack_from("<16I", raw, 0x20)
        return cls(
            title=raw[0x00:0x0C].rstrip(b"\0").decode("ascii", "replace"),
            game_code=raw[0x0C:0x10].decode("ascii", "replace"),
            maker_code=raw[0x10:0x12].decode("ascii", "replace"),
            unit_code=raw[0x12],
            rom_version=raw[0x1E],
            arm9_offset=regions[0], arm9_entry=regions[1], arm9_ram=regions[2], arm9_size=regions[3],
            arm7_offset=regions[4], arm7_entry=regions[5], arm7_ram=regions[6], arm7_size=regions[7],
            fnt_offset=regions[8], fnt_size=regions[9],
            fat_offset=regions[10], fat_size=regions[11],
            ovt9_offset=regions[12], ovt9_size=regions[13],
            ovt7_offset=regions[14], ovt7_size=regions[15],
            rom_size=struct.unpack_from("<I", raw, 0x80)[0],
        )


@dataclass(frozen=True)
class Overlay:
    id: int
    ram_address: int
    ram_size: int
    bss_size: int
    sinit_start: int
    sinit_end: int
    file_id: int
    compressed_size: int
    flags: int

    @property
    def compressed(self) -> bool:
        return bool(self.flags & OVERLAY_FLAG_COMPRESSED)

    @classmethod
    def parse(cls, raw: bytes, offset: int) -> "Overlay":
        oid, ram, size, bss, sinit_start, sinit_end, file_id, packed = struct.unpack_from("<8I", raw, offset)
        return cls(oid, ram, size, bss, sinit_start, sinit_end, file_id,
                   compressed_size=packed & 0xFFFFFF, flags=packed >> 24)


@dataclass(frozen=True)
class ModuleParams:
    """NitroSDK _start_ModuleParams, located just before the NitroCode magic."""
    offset: int
    autoload_list: int
    autoload_list_end: int
    autoload_start: int
    static_bss_start: int
    static_bss_end: int
    compressed_static_end: int
    sdk_version: int

    @property
    def sdk_version_str(self) -> str:
        v = self.sdk_version
        return f"{v >> 24}.{(v >> 16) & 0xFF} (relstep {v & 0xFFFF})"


@dataclass
class Rom:
    data: bytes
    header: Header = field(init=False)

    def __post_init__(self):
        self.header = Header.parse(self.data)
        h = self.header
        for name, off, size in (("arm9", h.arm9_offset, h.arm9_size), ("arm7", h.arm7_offset, h.arm7_size),
                                ("fnt", h.fnt_offset, h.fnt_size), ("fat", h.fat_offset, h.fat_size),
                                ("ovt9", h.ovt9_offset, h.ovt9_size)):
            if off + size > len(self.data):
                raise RomError(f"{name} region {off:#x}+{size:#x} is outside the file")

    @classmethod
    def load(cls, path) -> "Rom":
        with open(path, "rb") as f:
            return cls(f.read())

    # --- raw regions -------------------------------------------------------

    def _slice(self, offset: int, size: int) -> bytes:
        return self.data[offset:offset + size]

    @property
    def arm9(self) -> bytes:
        return self._slice(self.header.arm9_offset, self.header.arm9_size)

    @property
    def arm7(self) -> bytes:
        return self._slice(self.header.arm7_offset, self.header.arm7_size)

    @property
    def fat(self) -> list[tuple[int, int]]:
        raw = self._slice(self.header.fat_offset, self.header.fat_size)
        return [struct.unpack_from("<II", raw, i) for i in range(0, len(raw) - 7, 8)]

    def file_data(self, file_id: int) -> bytes:
        start, end = self.fat[file_id]
        return self.data[start:end]

    # --- ARM9 --------------------------------------------------------------

    def module_params(self) -> ModuleParams:
        arm9 = self.arm9
        magic = arm9.find(NITROCODE)
        if magic < 0x1C:
            raise RomError("NitroCode magic not found in ARM9")
        offset = magic - 0x1C
        return ModuleParams(offset, *struct.unpack_from("<7I", arm9, offset))

    def arm9_decompressed(self) -> bytes:
        arm9 = self.arm9
        end = self.module_params().compressed_static_end
        if not end:
            return arm9
        split = end - self.header.arm9_ram
        return blz.decompress(arm9[:split]) + arm9[split:]

    def autoloads(self, arm9: bytes) -> list[tuple[int, int, int, bytes]]:
        """Split the autoload blocks (ITCM/DTCM/...) out of a decompressed ARM9.

        Returns (ram_address, size, bss_size, data) per block. The blocks are stored
        back to back from autoload_start; the list itself sits at autoload_list.
        """
        p = self.module_params()
        base = self.header.arm9_ram
        pos = p.autoload_start - base
        blocks = []
        for entry in range(p.autoload_list - base, p.autoload_list_end - base, 12):
            ram, size, bss = struct.unpack_from("<3I", arm9, entry)
            if pos + size > len(arm9):
                raise RomError(f"autoload block {ram:#x} runs past the ARM9 image")
            blocks.append((ram, size, bss, arm9[pos:pos + size]))
            pos += size
        return blocks

    # --- overlays ----------------------------------------------------------

    def overlays9(self) -> list[Overlay]:
        raw = self._slice(self.header.ovt9_offset, self.header.ovt9_size)
        return [Overlay.parse(raw, i) for i in range(0, len(raw), 32)]

    def overlay_decompressed(self, ov: Overlay) -> bytes:
        raw = self.file_data(ov.file_id)
        if not ov.compressed:
            return raw
        return blz.decompress(raw[:ov.compressed_size] if ov.compressed_size else raw)

    # --- file system -------------------------------------------------------

    def files(self) -> list[tuple[str, int]]:
        """Return (path, file_id) for every named file in the FNT."""
        fnt = self._slice(self.header.fnt_offset, self.header.fnt_size)
        out: list[tuple[str, int]] = []

        def walk(dir_id: int, prefix: str, depth: int):
            if depth > 64:
                raise RomError("FNT directory nesting too deep")
            sub_off, first_id, _ = struct.unpack_from("<IHH", fnt, (dir_id & 0xFFF) * 8)
            pos, file_id = sub_off, first_id
            while True:
                entry = fnt[pos]
                pos += 1
                if entry == 0:
                    return
                name = fnt[pos:pos + (entry & 0x7F)].decode("shift_jis", "replace")
                pos += entry & 0x7F
                if entry & 0x80:
                    child = struct.unpack_from("<H", fnt, pos)[0]
                    pos += 2
                    walk(child, f"{prefix}{name}/", depth + 1)
                else:
                    out.append((f"{prefix}{name}", file_id))
                    file_id += 1

        walk(0xF000, "", 0)
        return out
