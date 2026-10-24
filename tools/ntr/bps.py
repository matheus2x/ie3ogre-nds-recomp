"""BPS patches (the "beat" format of ROM hacking): a file as the changes over the original.

  "BPS1", varint source size, target size, metadata size, metadata
  actions: varint ((length - 1) << 2) | kind
    0 SourceRead   the source bytes at the current output offset
    1 TargetRead   `length` bytes that follow in the patch
    2 SourceCopy   from the source, at a signed relative offset (varint (abs << 1) | sign)
    3 TargetCopy   from the output written so far, the same way
  CRC-32 of the source, of the target and of the patch before this field (little endian)

The game applies them in runtime/src/mods.cpp (files/<path>.bps, overlays/ovNNN.bin.bps).
The encoder is greedy: same-offset runs, then copies found through an index of the
source's 8-byte blocks; good for translations, where most of a file moves or stays.
"""

import struct
import zlib

MAGIC = b"BPS1"
BLOCK = 8
CANDIDATES = 8  # source offsets kept per block; the zeros of a big file would be thousands
MIN_SOURCE_READ = 4


class BpsError(ValueError):
    pass


def _varint(n: int, out: bytearray):
    while True:
        x = n & 0x7F
        n >>= 7
        if n == 0:
            out.append(0x80 | x)
            return
        out.append(x)
        n -= 1


def _signed(n: int, out: bytearray):
    _varint(abs(n) << 1 | (n < 0), out)


def _match(a: bytes, i: int, b: bytes, j: int) -> int:
    """How many bytes a[i:] and b[j:] have in common at the start."""
    limit = min(len(a) - i, len(b) - j)
    n, step = 0, 16
    while n < limit:
        step = min(step, limit - n)
        if a[i + n:i + n + step] == b[j + n:j + n + step]:
            n += step
            step = min(step * 2, 4096)
        elif step > 1:
            step //= 2
        else:
            break
    return n


def encode(source: bytes, target: bytes, metadata: bytes = b"") -> bytes:
    out = bytearray(MAGIC)
    for n in (len(source), len(target), len(metadata)):
        _varint(n, out)
    out += metadata

    index: dict[bytes, list[int]] = {}
    for s in range(0, len(source) - BLOCK + 1, BLOCK):
        spots = index.setdefault(source[s:s + BLOCK], [])
        if len(spots) < CANDIDATES:
            spots.append(s)

    t = literal = 0  # output offset; start of the pending TargetRead
    source_rel = 0
    delta = 0  # source - target of the last copy: shifted text tends to keep shifting alike

    def flush(end: int):
        if end > literal:
            _varint((end - literal - 1) << 2 | 1, out)
            out.extend(target[literal:end])

    while t < len(target):
        if t < len(source) and source[t] == target[t]:
            n = _match(source, t, target, t)
            if n >= MIN_SOURCE_READ:
                flush(t)
                _varint((n - 1) << 2 | 0, out)
                t += n
                literal = t
                continue
        best_len, best_at = 0, 0
        key = target[t:t + BLOCK]
        if len(key) == BLOCK:
            spots = list(index.get(key, ()))
            if 0 <= t + delta <= len(source) - BLOCK and source[t + delta:t + delta + BLOCK] == key:
                spots.insert(0, t + delta)
            for s in spots:
                n = _match(source, s, target, t)
                if n > best_len:
                    best_len, best_at = n, s
        if best_len < BLOCK:
            t += 1
            continue
        # Grow backwards over the pending literal bytes.
        while t > literal and best_at > 0 and source[best_at - 1] == target[t - 1]:
            t -= 1
            best_at -= 1
            best_len += 1
        flush(t)
        _varint((best_len - 1) << 2 | 2, out)
        _signed(best_at - source_rel, out)
        source_rel = best_at + best_len
        delta = best_at - t
        t += best_len
        literal = t
    flush(len(target))
    out += struct.pack("<II", zlib.crc32(source), zlib.crc32(target))
    out += struct.pack("<I", zlib.crc32(out))
    return bytes(out)


def header(patch: bytes) -> tuple[int, int]:
    """(source size, target size)."""
    if patch[:4] != MAGIC:
        raise BpsError("not a BPS patch")
    at, sizes = 4, []
    for _ in range(2):
        n, at = _read_varint(patch, at)
        sizes.append(n)
    return sizes[0], sizes[1]


def _read_varint(data: bytes, at: int) -> tuple[int, int]:
    n, shift = 0, 1
    while True:
        if at >= len(data):
            raise BpsError("patch is cut short")
        x = data[at]
        at += 1
        n += (x & 0x7F) * shift
        if x & 0x80:
            return n, at
        shift <<= 7
        n += shift


def apply(patch: bytes, source: bytes) -> bytes:
    if len(patch) < 4 + 3 + 12 or patch[:4] != MAGIC:
        raise BpsError("not a BPS patch")
    end = len(patch) - 12
    source_crc, target_crc, patch_crc = struct.unpack("<III", patch[end:])
    if zlib.crc32(patch[:end + 8]) != patch_crc:
        raise BpsError("the patch is damaged")
    at = 4
    source_size, at = _read_varint(patch, at)
    target_size, at = _read_varint(patch, at)
    meta, at = _read_varint(patch, at)
    at += meta
    if len(source) != source_size or zlib.crc32(source) != source_crc:
        raise BpsError("the patch is for another version of this file")
    out = bytearray()
    source_rel = target_rel = 0
    while at < end:
        data, at = _read_varint(patch, at)
        kind, length = data & 3, (data >> 2) + 1
        if kind == 0:
            out += source[len(out):len(out) + length]
        elif kind == 1:
            out += patch[at:at + length]
            at += length
        else:
            rel, at = _read_varint(patch, at)
            rel = -(rel >> 1) if rel & 1 else rel >> 1
            if kind == 2:
                source_rel += rel
                out += source[source_rel:source_rel + length]
                source_rel += length
            else:
                target_rel += rel
                for _ in range(length):  # may overlap what it writes
                    out.append(out[target_rel])
                    target_rel += 1
    if len(out) != target_size or zlib.crc32(out) != target_crc:
        raise BpsError("the patch is damaged")
    return bytes(out)
