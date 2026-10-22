"""Backwards LZ (BLZ) decompression, as used by NitroSDK for ARM9 and overlays.

The compressed region is read from its end towards its start. The last 8 bytes
form a footer:
    u32 [-8]: bits 0-23 = length of the compressed region (counted from the end),
              bits 24-31 = footer length (bytes to skip before the stream)
    u32 [-4]: extra bytes the output grows by after decompression
"""

import struct


class BlzError(ValueError):
    pass


def decompress(data: bytes) -> bytes:
    if len(data) < 8:
        raise BlzError("buffer too small for BLZ footer")

    enc_info, extra = struct.unpack_from("<II", data, len(data) - 8)
    footer_len = enc_info >> 24
    enc_len = enc_info & 0xFFFFFF
    if enc_len > len(data) or footer_len > enc_len:
        raise BlzError(f"invalid footer: enc_len={enc_len:#x} footer_len={footer_len:#x}")

    out = bytearray(data) + bytearray(extra)
    src = len(data) - footer_len
    dst = len(out)
    limit = len(data) - enc_len

    while src > limit:
        src -= 1
        flags = out[src]
        for _ in range(8):
            if src <= limit:
                break
            if flags & 0x80:
                src -= 2
                pair = out[src] | (out[src + 1] << 8)
                length = (pair >> 12) + 3
                disp = (pair & 0xFFF) + 3
                if dst - length < src:
                    raise BlzError("output overran input")
                for _ in range(length):
                    dst -= 1
                    out[dst] = out[dst + disp]
            else:
                src -= 1
                dst -= 1
                out[dst] = out[src]
            flags = (flags << 1) & 0xFF

    return bytes(out)
