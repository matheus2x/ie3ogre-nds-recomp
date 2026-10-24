"""LZ10, the LZ77 of the DS BIOS (header 0x10 + decompressed size, 24 bits).

  flags byte, then 8 items: bit set = 2 bytes ((length - 3) << 12 | (distance - 1)),
  bit clear = 1 literal byte. Used by the .pac_/*_ files and by each entry of eve.pkb.
"""

import struct


class Lz10Error(ValueError):
    pass


def decompress(data: bytes) -> bytes:
    if len(data) < 4 or data[0] != 0x10:
        raise Lz10Error("not LZ10")
    size = struct.unpack_from("<I", data, 0)[0] >> 8
    out = bytearray()
    i = 4
    try:
        while len(out) < size:
            flags = data[i]
            i += 1
            for bit in range(8):
                if len(out) >= size:
                    break
                if flags & (0x80 >> bit):
                    x = data[i] << 8 | data[i + 1]
                    i += 2
                    length, distance = (x >> 12) + 3, (x & 0xFFF) + 1
                    if distance > len(out):
                        raise Lz10Error("copy before the start")
                    for _ in range(length):
                        out.append(out[-distance])
                else:
                    out.append(data[i])
                    i += 1
    except IndexError:
        raise Lz10Error("truncated") from None
    return bytes(out[:size])


def try_decompress(data: bytes) -> bytes | None:
    try:
        return decompress(data)
    except Lz10Error:
        return None


def compress(data: bytes) -> bytes:
    """Greedy, with a chain of the last positions of each 3-byte sequence. Distance 1 is never
    used (some decoders write to VRAM 16 bits at a time). Padded to 4 bytes."""
    out = bytearray(struct.pack("<I", 0x10 | (len(data) << 8)))
    heads: dict[bytes, list[int]] = {}
    n = len(data)
    i = 0

    def remember(at: int):
        if at + 3 <= n:
            heads.setdefault(data[at:at + 3], []).append(at)

    while i < n:
        flag_at = len(out)
        out.append(0)
        flags = 0
        for bit in range(8):
            if i >= n:
                break
            best_len = best_dist = 0
            for p in reversed(heads.get(data[i:i + 3], [])[-64:]) if i + 3 <= n else ():
                dist = i - p
                if dist > 0x1000:
                    break
                if dist < 2:
                    continue
                length = 3
                while length < 18 and i + length < n and data[p + length] == data[i + length]:
                    length += 1
                if length > best_len:
                    best_len, best_dist = length, dist
                    if length == 18:
                        break
            if best_len >= 3:
                flags |= 0x80 >> bit
                x = (best_len - 3) << 12 | (best_dist - 1)
                out += bytes((x >> 8, x & 0xFF))
                for k in range(best_len):
                    remember(i + k)
                i += best_len
            else:
                out.append(data[i])
                remember(i)
                i += 1
        out[flag_at] = flags
    while len(out) % 4:
        out.append(0)
    return bytes(out)
