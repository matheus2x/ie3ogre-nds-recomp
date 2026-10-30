"""The text of the game and of the PT-BR translation (docs/TRADUCAO.md).

Packs (.pkh + .pkb): "PackNum 20080626", u32 size of the .pkh at 0x10, u16 count at 0x16,
u32 alignment (16) at 0x18, u32 largest stored entry (rounded up) at 0x1C, then from 0x30
entries of (u32 key, u32 offset, u32 size). The key is the event number (eve31010000.ssd
-> 31010000). Entries of eve.pkb are LZ10; those of evet.pkb and mcht.pkb are not.

SSD (event scripts, eve.pkb): 0x20-byte header, the code (size in word 4) and the data
(size in word 5), records of [u16 id][u8 index][u8 size][contents]. Strings end in NUL;
"@offset,length" points at a line of the evet entry with the same key. The code is
instructions [u16 id][u16 size][u16 opcode][u8][u8 args]...; the strings of an instruction
are the data records with its id (ssd_opcodes). Opcode 0x3070 only prints its string to the
debug log; 0x3019 shows who speaks (record 2, "Senhor K"). Opcode 0x201D sets the objective shown on the top screen
(its string is record 3); the game draws it into 4 sprites 64 pixels wide, placed every 60
pixels (objective_layout): a letter goes to the next sprite only when it does not fit in the
64, so the last 4 pixels of a sprite show over the start of the next one (Japanese, 5
letters of 12 pixels per sprite, never reaches them).

Text lists (evet.pkb, mcht.pkb): records [u32 BE size][text, NUL, padding]. Each %nF in
a line takes the next record as its furigana. A line may use the whole "@offset,length"
span (its record plus those furigana records): the game reads it up to the NUL.

Tables (data_iz/logic): a .STR holds strings at 32-byte steps, found by index * 32 from a
record of its .dat, and the game reads at most 128 bytes of one ([kanji/kana] is furigana,
a real newline breaks the row). item.dat is 1024 records of 0x2C bytes, scrambled
(unscramble/scramble): the name in 0x00-0x1F and the index of the description at 0x2A.
command.dat (the techniques) is 512 plain records of 0x24 bytes, the index of the name in
command.STR at 0x18; the names there are full-width (wide), as the fan translation wrote them.
movie_view_f.dat (the movie gallery) is 58 records of 0x74 bytes: u16 id, the movie's file name at
0x02, its number ("ＮＯ．１") at 0x0C and its title, with furigana, from 0x1B to the end of the record.
The gallery parses the furigana with a signed char (func_ov016_020f24e8): it copies every byte
from 0x80 up together with the next one and drops the rest of the ASCII, so a Latin title is
written paired (each one-byte character next to SPACER), and it draws with no pixel between letters.

The translation writes Latin text: ASCII, the accents below as one byte each (where the
half-width kana were; the font was redrawn) and \\n / \\f as two characters.
"""

import functools
import re
import struct

ACCENTS = {
    "à": 0xB1, "á": 0xB2, "â": 0xB3, "ã": 0xB4, "ç": 0xB8, "é": 0xBA, "ê": 0xBB, "í": 0xBE,
    "ó": 0xC4, "ô": 0xC5, "õ": 0xC6, "ú": 0xCA, "ü": 0xCC,
    "À": 0xD0, "Á": 0xD1, "Â": 0xD2, "Ã": 0xD3, "Ç": 0xD7, "É": 0xD9, "Ê": 0xDA,
    "Ó": 0xAB, "Ô": 0xAC, "Õ": 0xAD, "Ú": 0xA2,
    "\u200a": 0xDF,  # a blank of no width (fit_font): spaces objectives, pairs PAIRED text
}
SPACER = "\u200a"
# 0xCF is the space of the fan translation's font, 4 pixels wide: its subtitles, scripts and
# tables write "letter, 0xCF, letter" (fit_font keeps its width).
FAN_SPACE, FAN_SPACE_WIDTHS = 0xCF, bytes((0, 0, 3))
_FROM_BYTE = {v: k for k, v in ACCENTS.items()} | {FAN_SPACE: " "}
_PLAIN = {"…": "...", "“": '"', "”": '"', "‘": "'", "’": "'", "—": "-", "–": "-"}
FURIGANA = re.compile(rb"%\d+F")
JAPANESE = re.compile(r"[ぁ-ヿ一-鿿]")
BOX_WIDTH = 226  # pixels of a dialogue line before the game wraps it
BOX_ROWS = 3     # rows shown in a dialogue box (a 4th one is not drawn)
OBJECTIVE_STEP, OBJECTIVE_WIDTH, OBJECTIVE_PARTS = 60, 64, 4  # sprites of the objective
STR_STEP, STR_READ = 32, 0x80  # a .STR string starts at index * 32; the game reads 128 bytes
ITEM_SIZE, ITEM_NAME = 0x2C, 0x20
COMMAND_SIZE, COMMAND_NAME, COMMAND_DESC = 0x24, 0x18, 0x1A
MOVIE_SIZE, MOVIE_TITLE = 0x74, 0x1B  # movie_view_f.dat: a scene of the movie gallery
MOVIE_WIDTH = 229  # px of a gallery title from x = 16 to the edge of its box (no pixel between letters)
_SWAPS = ((2, 3), (4, 5), (6, 7), (1, 2))  # (distance, step) of the byte swaps of item.dat


class TextError(ValueError):
    pass


def is_lead(c: int) -> bool:
    return 0x81 <= c <= 0x9F or 0xE0 <= c <= 0xFC


def codes(b: bytes):
    """Character codes of game text: one byte, or two for Shift-JIS."""
    i = 0
    while i < len(b):
        if is_lead(b[i]) and i + 1 < len(b):
            yield b[i] << 8 | b[i + 1]
            i += 2
        else:
            yield b[i]
            i += 1


def japanese_count(b: bytes) -> int:
    """Kana and kanji (two-byte Shift-JIS) in game text."""
    return sum(1 for c in codes(b) if 0x829F <= c <= 0x82F1 or 0x8340 <= c <= 0x8396
               or 0x889F <= c <= 0x9FFC or 0xE040 <= c <= 0xEAFC or c == 0x815B)


def encode(s: str) -> bytes:
    out = bytearray()
    for ch in s:
        if ch in ACCENTS:
            out.append(ACCENTS[ch])
        elif ch in _PLAIN:
            out += _PLAIN[ch].encode()
        elif ord(ch) < 0x7F:
            out.append(ord(ch))
        else:
            try:
                out += ch.encode("cp932")
            except UnicodeEncodeError:
                raise TextError(f"{ch!r} is not in the font") from None
    return bytes(out)


def decode(b: bytes) -> str:
    out = []
    i = 0
    while i < len(b):
        c = b[i]
        if is_lead(c) and i + 1 < len(b):
            out.append(b[i:i + 2].decode("cp932", "replace"))
            i += 2
            continue
        out.append(_FROM_BYTE.get(c) or (chr(c) if c < 0x80 else f"<{c:02x}>"))
        i += 1
    return "".join(out)


# --- packs ---------------------------------------------------------------------------

def read_pack(pkh: bytes, pkb: bytes) -> list[tuple[int, bytes]]:
    """(key, stored bytes) in the order of the .pkh."""
    if pkh[:7] != b"PackNum":
        raise TextError("not a PackNum index")
    out = []
    for at in range(0x30, len(pkh) - 11, 12):
        key, off, size = struct.unpack_from("<III", pkh, at)
        out.append((key, pkb[off:off + size]))
    return out


def write_pack(pkh: bytes, entries: list[tuple[int, bytes]]) -> tuple[bytes, bytes]:
    """New .pkh and .pkb for the same keys (16-byte aligned, largest entry updated)."""
    h = bytearray(pkh)
    b = bytearray()
    largest = struct.unpack_from("<I", pkh, 0x1C)[0]
    for k, (key, data) in enumerate(entries):
        struct.pack_into("<III", h, 0x30 + 12 * k, key, len(b), len(data))
        b += data
        b += bytes(-len(b) % 16)
        largest = max(largest, (len(data) + 15) // 16 * 16)
    struct.pack_into("<I", h, 0x1C, largest)
    return bytes(h), bytes(b)


# --- tables ----------------------------------------------------------------------------

def str_strings(d: bytes) -> list[tuple[int, int]]:
    """(offset, room in bytes, NUL included) of each string of a .STR table."""
    starts = [i for i in range(0, len(d), STR_STEP) if d[i] and (i == 0 or not d[i - 1])]
    return [(at, min(nxt, at + STR_READ) - at)
            for at, nxt in zip(starts, starts[1:] + [len(d)])]


def wide(text: str) -> str:
    """ASCII as full-width characters (a space is the ideographic one)."""
    return "".join(chr(ord(c) + 0xFEE0) if "!" <= c <= "~" else "\u3000" if c == " " else c for c in text)


WIDE, PAIRED, SUBTITLED = "wide", "paired", "subtitled"  # how a table writes Latin text (table_bytes)


def paired(data: bytes) -> bytes:
    """Text for a reader that takes every byte from 0x80 up as the first of two and drops
    the other ASCII (the furigana parser of the message boxes, func_ov016_020f24e8): a
    one-byte character that would be read alone gets SPACER, which draws nothing, as its
    pair; an accent carries the letter after it (the parser copies the second byte as is)."""
    out = bytearray()
    cs = list(codes(data))
    i = 0
    while i < len(cs):
        c = cs[i]
        i += 1
        if c > 0xFF:
            out += bytes((c >> 8, c & 0xFF))
        elif c == 0x0A:
            out.append(c)  # the parser keeps a newline by itself
        elif c == 0x25 and i < len(cs) and 0x61 <= cs[i] <= 0x7A:
            out += bytes((c, cs[i]))  # %s stays whole for sprintf, which runs before the parser
            i += 1
        elif c < 0x80:
            out += bytes((ACCENTS[SPACER], c))
        elif i < len(cs) and cs[i] <= 0xFF:
            out += bytes((c, cs[i]))
            i += 1
        else:
            out += bytes((c, ACCENTS[SPACER]))
    return bytes(out)


def subtitled(text: str) -> bytes:
    """A subtitle (movie/txt) for the furigana parser, without SPACER: the subtitles put a pixel
    after every letter, SPACER too, but centre the row without those pixels (seen in the game), so
    SPACER pushes a row to the right. As the fan translation did: a space is FAN_SPACE and takes
    the next letter, half-width, as its pair; an accent takes the next one-byte character; any other
    letter goes full-width (two bytes)."""
    out = bytearray()
    cs = [FAN_SPACE if c == 0x20 else c for c in codes(encode(text))]
    i = 0
    while i < len(cs):
        c = cs[i]
        i += 1
        if c > 0xFF:
            out += bytes((c >> 8, c & 0xFF))
        elif c >= 0x80:  # a space or an accent: the next one-byte character is its pair
            if i < len(cs) and cs[i] <= 0xFF:
                out += bytes((c, cs[i]))
                i += 1
            else:
                out += bytes((c, ACCENTS[SPACER]))
        else:
            w = encode(wide(chr(c)))
            if len(w) != 2:
                raise TextError(f"{chr(c)!r} has no full-width form")
            out += w
    return bytes(out)


def table_bytes(text: str, form: str | None = None) -> bytes:
    """A table string: \\n of the translation is a real newline there."""
    text = text.replace("\\n", "\n")
    if form == WIDE:
        return encode(wide(text))
    if form == SUBTITLED:
        return subtitled(text)
    return paired(encode(text)) if form == PAIRED else encode(text)


def unscramble(rec: bytes) -> bytes:
    """A record of item.dat as the game reads it (func_020748c8)."""
    b = bytearray(((x ^ 0xAD) >> 2 | (x ^ 0xAD) << 6) & 0xFF for x in rec)
    for dist, step in _SWAPS:
        for i in range(0, len(b) - dist, step):
            b[i], b[i + dist] = b[i + dist], b[i]
    return bytes(b)


def scramble(rec: bytes) -> bytes:
    b = bytearray(rec)
    for dist, step in reversed(_SWAPS):
        for i in reversed(range(0, len(b) - dist, step)):
            b[i], b[i + dist] = b[i + dist], b[i]
    return bytes(((x << 2 | x >> 6) & 0xFF) ^ 0xAD for x in b)


# --- event scripts ----------------------------------------------------------------------

def ssd_records(d: bytes) -> list[tuple[int, int]]:
    """(offset, size) of the records of the data section of an SSD script."""
    if d[:4] != b"SSD\0":
        return []
    words = struct.unpack_from("<8I", d, 0)
    i, end = 0x20 + words[4], min(len(d), 0x20 + words[4] + words[5])
    out = []
    while i + 4 <= end:
        size = d[i + 3]
        if size < 4:
            break
        out.append((i, size))
        i += size
    return out


def ssd_string(d: bytes, at: int, size: int) -> bytes:
    return d[at + 4:at + size].split(b"\0")[0]


def ssd_refs(d: bytes) -> dict[int, int]:
    """"@offset,length" of a script: offset of a line in its evet entry -> length."""
    out = {}
    for at, size in ssd_records(d):
        m = re.fullmatch(rb"@(\d+),(\d+)", ssd_string(d, at, size))
        if m:
            out[int(m[1])] = int(m[2])
    return out


DEBUG_PRINT = 0x3070  # SSD instruction whose string goes only to the debug log ("■試合終了")


def ssd_opcodes(d: bytes) -> dict[bytes, int]:
    """Opcode of each instruction of an SSD script, by its id (the first 2 bytes of its records)."""
    code = d[0x20:0x20 + struct.unpack_from("<I", d, 16)[0]] if d[:4] == b"SSD\0" else b""
    out, i = {}, 0
    while i + 6 <= len(code):
        size, op = struct.unpack_from("<HH", code, i + 2)
        if size < 4:
            break
        out.setdefault(code[i:i + 2], op)
        i += size
    return out


def ssd_objectives(d: bytes) -> set[int]:
    """Offsets of the records that hold an objective (record 3 of an instruction 0x201D)."""
    if d[:4] != b"SSD\0":
        return set()
    code = d[0x20:0x20 + struct.unpack_from("<I", d, 16)[0]]
    ids = {code[i:i + 2] for i in range(0, len(code) - 7, 4) if code[i + 4:i + 6] == b"\x1d\x20"
           and struct.unpack_from("<H", code, i + 2)[0] >= 8}
    return {at for at, _ in ssd_records(d) if d[at:at + 2] in ids and d[at + 2] == 3}


def objective_layout(text: str, font: "Font") -> list[tuple[int, int, int]]:
    """(sprite, x, width) of each character of an objective, as the game places them: the
    1-pixel gap goes before a character that does not start a sprite, and a character that
    does not fit in the 64 pixels of the sprite starts the next one."""
    out, x, part = [], 0, 0
    for ch in text:
        w = font.measure(ch) - 1
        at = x + 1 if x else 0
        if at + w > OBJECTIVE_WIDTH:
            part, at = part + 1, 0
        out.append((part, at, w))
        x = at + w
    return out


def objective_scan(text: str, font: "Font") -> tuple[int | None, int]:
    """(index of the first character laid out wrong, or None; letters of a word that touch).
    Wrong: letters overlapping on screen, a gap between words under 4 pixels, a 5th sprite."""
    cost, prev, blank = 0, None, False  # prev: end of the last letter, in pixels on screen
    for k, (ch, (part, x, w)) in enumerate(zip(text, objective_layout(text, font))):
        if part >= OBJECTIVE_PARTS:
            return k, cost
        if ch in (" ", SPACER, "\u3000"):
            blank = blank or ch != SPACER
            continue
        start = part * OBJECTIVE_STEP + x
        if prev is not None:
            gap = start - prev
            if gap < 0 or (blank and gap < 4):
                return k, cost
            cost += gap == 0
        prev, blank = start + w, False
    return None, cost


def objective_cost(text: str, font: "Font") -> int | None:
    """How much an objective's layout is off (letters that touch), or None if it is wrong."""
    bad, cost = objective_scan(text, font)
    return None if bad is not None or not text else cost


@functools.lru_cache(maxsize=None)
def _objective_fit(text: str, font: "Font") -> str | None:
    def good(t: str) -> tuple[int, int]:  # (letters laid out before the first error, cost)
        bad, cost = objective_scan(t, font)
        return len((t if bad is None else t[:bad]).replace(SPACER, "")) + (bad is None), cost

    total = len(text) + 1
    while (now := good(text)[0]) < total:
        best = None
        for at in sorted({k + d for k, ch in enumerate(text) if ch == " " for d in (0, 1)}):
            if len(text[:at].replace(SPACER, "")) > now:
                break
            for n in range(1, 2 * OBJECTIVE_STEP):
                cand = text[:at] + SPACER * n + text[at:]
                reach, cost = good(cand)
                if reach > now:
                    score = (-min(reach, now + 12), n + 4 * cost)
                    if best is None or score < best[0]:
                        best = (score, cand)
                    break
        if best is None:
            return None
        text = best[1]
    return text


def objective_fit(text: str, font: "Font") -> str | None:
    """The objective with 1-pixel blanks (SPACER) next to its spaces where objective_scan
    needs them, the fewest pixels found (letters that touch count as 4); None if there is
    no such placement. Blanks before a space can push the space to the start of the next
    sprite, where it still counts (a SPACER there does not)."""
    return _objective_fit(text.replace(SPACER, ""), font)


def fit_font(nftr: bytes) -> bytes:
    """The font with SPACER of no width (no ink; the game adds 1 pixel after each letter) and
    the fan translation's space as it drew it (version 1.3 had zeroed it, and its subtitles
    lost their spaces)."""
    font, out = Font(nftr), bytearray(nftr)
    widths = {font.glyph[ACCENTS[SPACER]]: bytes(3), font.glyph[FAN_SPACE]: FAN_SPACE_WIDTHS}
    finf = struct.unpack_from("<H", nftr, 0x0C)[0]
    p = struct.unpack_from("<I", nftr, finf + 0x14)[0] - 8
    while p:
        first, last, nxt = struct.unpack_from("<HHI", nftr, p + 8)
        for glyph, cwdh in widths.items():
            if first <= glyph <= last:
                at = p + 0x10 + 3 * (glyph - first)
                out[at:at + 3] = cwdh
        p = nxt - 8 if nxt else 0
    return bytes(out)


# --- text lists -----------------------------------------------------------------------

def text_records(d: bytes) -> list[tuple[int, int]]:
    out = []
    i = 0
    while i + 4 <= len(d):
        size = struct.unpack_from(">I", d, i)[0]
        if size < 4 or size % 4 or i + size > len(d):
            break
        out.append((i, size))
        i += size
    return out


def subtitle_records(d: bytes) -> list[tuple[int, int, int]]:
    """Subtitles of a movie (movie/txt/*.dat): records [u32 first frame][u32 last frame][u32 size]
    [text, NUL, padding to 4], until a first frame of 0xFFFFFFFF. (offset, size, offset of the text)."""
    out = []
    i = 0
    while i + 12 <= len(d) and struct.unpack_from("<I", d, i)[0] != 0xFFFFFFFF:
        size = struct.unpack_from("<I", d, i + 8)[0]
        if size % 4 or i + 12 + size > len(d):
            raise TextError(f"subtitle record at {i:#x} runs past the file")
        out.append((i, 12 + size, i + 12))
        i += 12 + size
    return out


def subtitle_file(d: bytes, texts: dict[int, bytes]) -> bytes:
    """The subtitles with new texts (record -> bytes without the NUL); the frames and the end stay."""
    recs = subtitle_records(d)
    out = bytearray()
    for k, (at, size, text) in enumerate(recs):
        if k not in texts:
            out += d[at:at + size]
            continue
        body = texts[k] + b"\0"
        body += bytes(-len(body) % 4)
        out += d[at:at + 8] + struct.pack("<I", len(body)) + body
    end = recs[-1][0] + recs[-1][1] if recs else 0
    return bytes(out + d[end:])


def lines(d: bytes) -> list[tuple[int, int, bytes]]:
    """Lines of a text list: (offset of the text, end of its span, text with markers)."""
    recs = text_records(d)
    out = []
    k = 0
    while k < len(recs):
        at, size = recs[k]
        raw = d[at + 4:at + size].split(b"\0")[0]
        k2 = k + 1 + len(FURIGANA.findall(raw))
        end = recs[k2][0] if k2 < len(recs) else len(d)
        out.append((at + 4, end, raw))
        k = k2
    return out


# --- font and dialogue box ------------------------------------------------------------

class Font:
    """Advance widths of an NFTR font (the glyph width + 1 pixel, as the game draws)."""

    def __init__(self, nftr: bytes):
        if nftr[:4] not in (b"RTFN", b"NFTR"):
            raise TextError("not an NFTR font")
        finf = struct.unpack_from("<H", nftr, 0x0C)[0]
        _, cwdh, cmap = struct.unpack_from("<III", nftr, finf + 0x10)
        self.glyph: dict[int, int] = {}
        p = cmap - 8
        while p:
            first, last, kind, _, nxt = struct.unpack_from("<HHHHI", nftr, p + 8)
            q = p + 0x14
            if kind == 0:
                base = struct.unpack_from("<H", nftr, q)[0]
                for c in range(first, last + 1):
                    self.glyph[c] = base + c - first
            elif kind == 1:
                for k, c in enumerate(range(first, last + 1)):
                    g = struct.unpack_from("<H", nftr, q + 2 * k)[0]
                    if g != 0xFFFF:
                        self.glyph[c] = g
            else:
                for k in range(struct.unpack_from("<H", nftr, q)[0]):
                    c, g = struct.unpack_from("<HH", nftr, q + 2 + 4 * k)
                    self.glyph[c] = g
            p = nxt - 8 if nxt else 0
        self.width: dict[int, int] = {}
        p = cwdh - 8
        while p:
            first, last, nxt = struct.unpack_from("<HHI", nftr, p + 8)
            for k in range(last - first + 1):
                self.width[first + k] = nftr[p + 0x10 + 3 * k + 2]
            p = nxt - 8 if nxt else 0

    def advance(self, code: int) -> int:
        g = self.glyph.get(code)
        return 12 if g is None else self.width.get(g, 11) + 1

    def measure(self, s: str, gap: int = 1) -> int:
        """Pixels of a row; gap is what the drawer adds after each letter (advance() has 1)."""
        return sum(self.advance(c) - 1 + gap for c in codes(encode(s)))

    def wrap(self, row: str) -> list[str]:
        """The game's own word wrap: a word that does not fit goes, with its space, to a new row."""
        out = [""]
        for k, word in enumerate(row.split(" ")):
            cand = out[-1] + (" " if k else "") + word
            if out[-1] and self.measure(cand) > BOX_WIDTH:
                out.append(" " + word)
            else:
                out[-1] = cand
        return out


def problems(source: bytes, text: str, capacity: int, font: Font, table: tuple | None = None) -> list[str]:
    """What keeps a translated line from fitting where the original was. A table string
    (table = (rows, width[, WIDE or PAIRED[, pixel between letters]])) is not wrapped by the
    game: each row must fit as written."""
    out = []
    if JAPANESE.search(text):
        out.append("still has Japanese")
    try:
        data = table_bytes(text, table[2] if len(table) > 2 else None) if table else encode(text)
    except TextError as e:
        return out + [str(e)]
    if len(data) > capacity:
        out.append(f"{len(data)} bytes, room for {capacity}")
    if table:
        rows = text.split("\\n")
        if len(rows) > table[0]:
            out.append(f"{len(rows)} rows (the table's strings have up to {table[0]})")
        if "\\f" in text or "[" in text:
            out.append("\\f or [ (furigana) in a table string")
        for r in rows:
            form = table[2] if len(table) > 2 else None
            gap = table[3] if len(table) > 3 else 0 if form == PAIRED else 1
            if form == SUBTITLED:  # what is drawn: full-width letters, FAN_SPACE
                drawn = list(codes(subtitled(r)))
                px = sum(font.advance(c) - 1 + gap for c in drawn)
                if missing := {decode(c.to_bytes(2 if c > 0xFF else 1, "big")) for c in drawn if c not in font.glyph}:
                    out.append(f"not in the font: {' '.join(sorted(missing))}")
            else:
                px = font.measure(wide(r) if form == WIDE else r, gap)
            if px > table[1]:
                out.append(f"row of {px} px (the table's widest is {table[1]}): {r[:40]}")
    for page in text.split("\\f") if not table else ():
        rows = sum(len(font.wrap(r)) for r in page.split("\\n"))
        if rows > BOX_ROWS:
            out.append(f"{rows} rows in a box (the game shows {BOX_ROWS})")
    if source.count(b"%s") != data.count(b"%s"):
        out.append("%s missing or extra")
    return out
