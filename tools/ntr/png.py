"""PNG files without a library: indexed ones written for the game's pictures and fonts, and any
non-interlaced PNG an image editor saves read back as RGBA (greyscale, RGB, indexed, with or
without alpha; 1 to 16 bits)."""

import struct
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PngError(ValueError):
    pass


def write_png(w: int, h: int, pal: list[tuple[int, int, int]], rows: list[bytes],
              transparent: bool = True) -> bytes:
    """An 8-bit indexed PNG; with transparent, index 0 is see-through."""
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
            + chunk(b"PLTE", bytes(v for c in pal for v in c)) + (chunk(b"tRNS", b"\0") if transparent else b"")
            + chunk(b"IDAT", zlib.compress(b"".join(b"\0" + r for r in rows))) + chunk(b"IEND", b""))


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c


def read_png(data: bytes) -> tuple[int, int, list[list[tuple[int, int, int, int]]]]:
    """(width, height, rows of (r, g, b, a) pixels, 0-255)."""
    if data[:8] != SIGNATURE:
        raise PngError("not a PNG file")
    p, idat, pal, trns, head = 8, bytearray(), [], b"", None
    while p + 8 <= len(data):
        size, kind = struct.unpack_from(">I4s", data, p)
        body = data[p + 8:p + 8 + size]
        if kind == b"IHDR":
            head = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            pal = [tuple(body[i:i + 3]) for i in range(0, len(body) - 2, 3)]
        elif kind == b"tRNS":
            trns = body
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        p += 12 + size
    if head is None:
        raise PngError("no IHDR")
    w, h, depth, ctype, _, _, interlace = head
    if interlace:
        raise PngError("interlaced PNG: save it without interlacing")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(ctype)
    if channels is None or depth not in {0: (1, 2, 4, 8, 16), 2: (8, 16), 3: (1, 2, 4, 8), 4: (8, 16), 6: (8, 16)}[ctype]:
        raise PngError(f"unsupported PNG (colour type {ctype}, {depth} bits)")
    raw = zlib.decompress(bytes(idat))
    bpp = max(1, channels * depth // 8)  # bytes per pixel for the filters
    stride = (w * channels * depth + 7) // 8
    rows, prev, q = [], bytearray(stride), 0
    for _ in range(h):
        kind, line = raw[q], bytearray(raw[q + 1:q + 1 + stride])
        q += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            line[i] = (line[i] + (0, a, b, (a + b) // 2, _paeth(a, b, c))[kind]) & 0xFF if kind else line[i]
        prev = line
        rows.append(bytes(line))
    out = []
    top = (1 << depth) - 1
    for line in rows:
        if depth < 8:
            vals = [line[k * depth // 8] >> (8 - depth - k * depth % 8) & top for k in range(w)]
        elif depth == 8:
            vals = list(line)
        else:
            vals = [line[i] for i in range(0, len(line), 2)]  # the high byte of each 16-bit sample
        row = []
        for x in range(w):
            if ctype == 3:
                i = vals[x]
                if i >= len(pal):
                    raise PngError(f"colour {i} is not in the palette")
                row.append((*pal[i], trns[i] if i < len(trns) else 255))
            elif ctype in (0, 4):
                v = vals[x * channels] * 255 // top if depth < 8 else vals[x * channels]
                alpha = vals[x * 2 + 1] if ctype == 4 else 0 if trns and depth == 8 and v == trns[1] else 255
                row.append((v, v, v, alpha))
            else:
                r, g, b = vals[x * channels:x * channels + 3]
                alpha = vals[x * 4 + 3] if ctype == 6 else 255
                row.append((r, g, b, alpha))
        out.append(row)
    return w, h, out
