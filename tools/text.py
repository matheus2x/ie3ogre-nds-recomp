#!/usr/bin/env python3
"""Translation of the game's text into a mod (formats in tools/ntr/text.py, state in docs/TRADUCAO.md).

  python3 tools/text.py survey boej.pak mods/CyberLifePlus
  python3 tools/text.py extract boej.pak mods/CyberLifePlus build/text [--only 3300,3611] [--translations DIR]
  python3 tools/text.py lookup build/text 円堂 ゴッドハンド
  python3 tools/text.py check boej.pak mods/CyberLifePlus translations/CyberLifePlus
  python3 tools/text.py apply boej.pak mods/CyberLifePlus translations/CyberLifePlus

survey measures how much of the game a mod still leaves in Japanese, by area. extract lists
the lines still in Japanese, with the room each one has and the translated lines around
it, and lookup shows how a term was translated in the lines already done; both write the
game's Japanese text, so they only write under build/ (never versioned).

A translation is a folder of .json files, {"evet:32010110:4": "Ah, Endo!\\nBom dia!", ...}:
the place of the line (file, entry, offset of the text; for a table, "item.STR:offset" or
"item.dat:record") and the Portuguese, without any Japanese. "" skips a line on purpose (debug, unused furigana). check tells what does not
fit; apply writes the lines into the mod as patches (the mod keeps carrying no game data).
"""

import argparse
import collections
import json
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import mod as modtool  # noqa: E402
from ntr import bps, lz10  # noqa: E402
from ntr.text import (COMMAND_DESC, COMMAND_NAME, COMMAND_SIZE, FURIGANA, ITEM_NAME, ITEM_SIZE, JAPANESE,  # noqa: E402
                      MOVIE_SIZE, MOVIE_TITLE, MOVIE_WIDTH, PAIRED, SPACER, WIDE,
                      OBJECTIVE_PARTS, STR_READ, STR_STEP, fit_font, objective_fit, ssd_objectives, Font, TextError, codes, decode, encode, japanese_count, lines, problems, read_pack, scramble, ssd_records,
                      ssd_refs, ssd_string, str_strings, table_bytes, unscramble, write_pack,
                      DEBUG_PRINT, ssd_opcodes)

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = "data_iz/script"
LOGIC = "data_iz/logic"
FONT = "data_iz/font/FONT12.NFTR"
SCRIPTS = ("eve", "mch")  # event and match scripts (SSD, LZ10)
PACKS = ("evet", "mcht") + SCRIPTS  # text lists, then the scripts
# Tables of fixed records whose strings stay in their field (place: "file:byte offset"); most are
# labels next to the id of a team, an item or a player. file -> (record size, ((field, bytes with
# the NUL), ...), how Latin text is written).
RECORDS = {**{f"PracticeGame{n}F.dat": (0x4A, ((0x0C, 22), (0x26, 22)), None) for n in range(10)},
           "premiumcondition.dat": (0x3C, ((0x04, 24),), None),
           "premiumcondition_hurri.dat": (0x40, ((0x04, 24),), None),
           "BinderData_O.dat": (0x1C, ((0x04, 18),), None),
           # the goal of a match: the fan translation wrote it for the furigana parser
           "gamerule.dat": (0x120, ((0x20, 64),), PAIRED),
           # what wins and what opens a route of matches (ov104, the furigana parser)
           "ClearCondition.dat": (0x51, ((0x01, 0x50),), PAIRED),
           "OpenCondition.dat": (0x51, ((0x01, 0x50),), PAIRED)}
# item.dat brings item.STR (names, then descriptions), command.dat the descriptions of command.STR;
# "ov" is the strings of the overlays
TABLES = ("item.dat", "tacticscmd.STR", "games.STR", "command.dat", "sp_binder.dat", "movie_view_f.dat", "ov") + tuple(RECORDS)
ID = re.compile(r"(evet|mcht|eve|mch):(\d+):(\d+)"
                r"|(item\.dat|item\.STR|tacticscmd\.STR|games\.STR|command\.dat|command\.STR|sp_binder\.dat|movie_view_f\.dat|"
                + "|".join(map(re.escape, RECORDS)) + r"):(\d+)"
                r"|(ov\d{3}):(0x[0-9a-f]+)")
ITEM_DESC = 0x2A  # item.dat (unscrambled): the index of the description in item.STR
TACTIC_SIZE, TACTIC_DESC = 0x14, 0x0E  # tacticscmd.dat: the index of the description in tacticscmd.STR
BINDER_SIZE, BINDER_TEXT, BINDER_READ = 12, 10, 0x100  # sp_binder.dat: u16 count, records; the copy in ov112
COMMAND_WIDTH = 124  # px of the widest technique name of the fan translation ("Honoo no Kazamidori")
# Overlay strings written as plain text: button labels whose neighbours the fan translation wrote
# so ("Defend", "Look for other"); the messages go through the furigana parser and are PAIRED.
PLAIN_LABELS = {"ov050:0x46c4", "ov050:0x46cc", "ov050:0x46d4", "ov050:0x46dc", "ov050:0x46e8",
                "ov057:0xa4dc"}


class Game:
    """The retail files and the same files with a mod applied."""

    def __init__(self, image: Path, mod: Path):
        self.rom = modtool.load_image(image)
        self.mod = mod
        self._packs: dict[tuple[str, bool], dict[int, bytes]] = {}
        self.overlays = {ov.id: ov for ov in self.rom.overlays9() if ov.id not in modtool.PROTECTED_OVERLAYS}

    def retail(self, rel: str) -> bytes:
        data = modtool.original(self.rom, "files/" + rel)
        if data is None:
            raise SystemExit(f"error: {rel} is not a file of the game")
        return data

    def modded(self, rel: str) -> bytes:
        whole, patch = self.mod / "files" / rel, self.mod / "files" / (rel + modtool.PATCH)
        if whole.exists():
            return whole.read_bytes()
        if patch.exists():
            return bps.apply(patch.read_bytes(), self.retail(rel))
        return self.retail(rel)

    def pack(self, name: str, modded: bool) -> dict[int, bytes]:
        """Entries of script/<name>.pkb, decompressed."""
        if (name, modded) not in self._packs:
            get = self.modded if modded else self.retail
            entries = read_pack(get(f"{SCRIPT}/{name}.pkh"), get(f"{SCRIPT}/{name}.pkb"))
            self._packs[name, modded] = {k: (lz10.try_decompress(d) if d[:1] == b"\x10" else None) or d
                                         for k, d in entries}
        return self._packs[name, modded]

    def overlay(self, oid: int, modded: bool) -> bytes:
        """An overlay as it is in RAM (decompressed)."""
        data = self.rom.overlay_decompressed(self.overlays[oid])
        patch = self.mod / "overlays" / f"ov{oid:03d}.bin{modtool.PATCH}"
        whole = patch.with_name(f"ov{oid:03d}.bin")
        if modded and whole.exists():
            return whole.read_bytes()
        return bps.apply(patch.read_bytes(), data) if modded and patch.exists() else data

    def font(self) -> Font:
        return Font(fit_font(self.modded(FONT)))


class Slot:
    """A place that holds one line: where it is, how many bytes fit, the original and what the mod has.
    A table string has table = (rows, width): the most rows and pixels of the table's own strings."""

    def __init__(self, sid: str, capacity: int, source: bytes, current: bytes, context: list[str],
                 table: tuple[int, int] | None = None, objective: bool = False):
        self.id, self.capacity, self.source, self.current, self.context = sid, capacity, source, current, context
        self.table, self.objective = table, objective
        name, *key, offset = sid.split(":")
        self.file, self.key, self.offset = name, int(key[0]) if key else 0, int(offset, 0)
        self.group = name.split(".")[0] if table else name if name != "evet" else key[0][:4]


def text_at(d: bytes, at: int) -> bytes:
    end = d.find(b"\0", at)
    return d[at:end if end >= 0 else len(d)]


def slots(game: Game, names=PACKS + TABLES):
    """Every line of the dialogue packs, every string of the scripts' data and of the tables."""
    for name in names:
        if name in TABLES:
            yield from table_slots(game, name)
            continue
        if name in SCRIPTS:
            yield from script_slots(game, name)
            continue
        retail, modded = game.pack(name, False), game.pack(name, True)
        scripts = game.pack("eve", True) if name == "evet" else {}
        retail_scripts = game.pack("eve", False) if name == "evet" else {}
        for key, base in retail.items():
            cur = modded[key]
            # The lines of evet are where its script points ("@offset,length"): the fan translation
            # moved the lines of some entries (and their pointers), and translated lines run over
            # the furigana records after them.
            refs = ssd_refs(scripts[key]) if key in scripts else {}
            same = key not in scripts or refs == ssd_refs(retail_scripts[key])
            places = ([(off + 4, off + length, text_at(base if same else cur, off + 4))
                       for off, length in sorted(refs.items())] if refs else lines(base))
            context: list[str] = []
            for at, end, raw in places:
                text = text_at(cur, at)
                yield Slot(f"{name}:{key}:{at}", end - at - 1, FURIGANA.sub(b"", raw), text, context[-3:])
                if not japanese_count(text):
                    context = context + [decode(text)]


def script_slots(game: Game, name: str):
    """Strings of the scripts' data, but the "@offset,length" pointers and the debug prints."""
    retail, modded = game.pack(name, False), game.pack(name, True)
    for key, cur in modded.items():
        base = retail[key]
        objectives, ops = ssd_objectives(cur), ssd_opcodes(cur)
        for at, size in ssd_records(cur):
            text = ssd_string(cur, at, size)
            if not text or text.startswith(b"@") or text[:1] < b" " or ops.get(cur[at:at + 2]) == DEBUG_PRINT:
                continue
            source = ssd_string(base, at, size) if len(base) == len(cur) else text
            yield Slot(f"{name}:{key}:{at + 4}", size - 5, FURIGANA.sub(b"", source), text, [],
                       objective=at in objectives)


RUBY = re.compile(r"\[([^/\]]*)/[^\]]*\]")


def visible(raw: bytes) -> str:
    """A table string as the game shows it: no furigana or SPACER, \\n for the newline."""
    return RUBY.sub(r"\1", decode(raw)).replace("\n", "\\n").replace(SPACER, "")


def table_rules(font: Font, sources: list[bytes]) -> tuple[int, int]:
    rows = [r for raw in sources for r in visible(raw).split("\\n")]
    return max(len(visible(raw).split("\\n")) for raw in sources), max(map(font.measure, rows))


def table_slots(game: Game, name: str):
    """Strings of a table, in place: a .STR string has the room up to the next one (and the
    128 bytes the game reads); an item name has its 0x20 bytes."""
    font = game.font()
    if name == "item.dat":
        retail, cur = game.retail(f"{LOGIC}/item.dat"), game.modded(f"{LOGIC}/item.dat")
        recs = [(k, unscramble(retail[k * ITEM_SIZE:(k + 1) * ITEM_SIZE]),
                 unscramble(cur[k * ITEM_SIZE:(k + 1) * ITEM_SIZE])) for k in range(len(retail) // ITEM_SIZE)]
        recs = [(k, a, b) for k, a, b in recs if a[0]]
        name_of = lambda r: r[:ITEM_NAME].split(b"\0")[0]  # noqa: E731
        rules = table_rules(font, [name_of(a) for _, a, _ in recs])
        sr, sc = game.retail(f"{LOGIC}/item.STR"), game.modded(f"{LOGIC}/item.STR")
        room = dict(str_strings(sr))
        # the description box draws with the furigana parser, which drops ASCII (seen in the game)
        desc_rules = table_rules(font, [text_at(sr, at) for at in room]) + (PAIRED,)
        for k, a, b in recs:
            yield Slot(f"item.dat:{k}", ITEM_NAME - 1, name_of(a), name_of(b), [], rules)
            at = struct.unpack_from("<H", a, ITEM_DESC)[0] * STR_STEP
            if at in room:  # the place is the original's; apply may move the text (put_description)
                yield Slot(f"item.STR:{at}", STR_READ - 1, text_at(sr, at),
                           text_at(sc, struct.unpack_from("<H", b, ITEM_DESC)[0] * STR_STEP),
                           [visible(name_of(b))], desc_rules)
        return
    if name == "command.dat":
        # A technique's name or description may outgrow its place: apply moves it to the end of
        # command.STR (the fan translation reordered it). A description's place is its offset in
        # the original; the game reads it into the box of the item descriptions (ov109).
        dr, dc = game.retail(f"{LOGIC}/command.dat"), game.modded(f"{LOGIC}/command.dat")
        sr, sc = game.retail(f"{LOGIC}/command.STR"), game.modded(f"{LOGIC}/command.STR")
        index = lambda d, k, field: struct.unpack_from("<H", d, k * COMMAND_SIZE + field)[0] * STR_STEP  # noqa: E731
        recs = range(len(dr) // COMMAND_SIZE)
        descs = {index(dr, k, COMMAND_DESC): text_at(sr, index(dr, k, COMMAND_DESC)) for k in recs}
        # the box of the item descriptions: the widest of both tables
        items = game.retail(f"{LOGIC}/item.STR")
        desc_rules = table_rules(font, [t for t in descs.values() if t] + [text_at(items, at) for at, _ in str_strings(items)])
        desc_rules += (PAIRED,)
        for k in recs:
            source = text_at(sr, index(dr, k, COMMAND_NAME))
            if source:
                yield Slot(f"command.dat:{k}", STR_READ - 1, source, text_at(sc, index(dc, k, COMMAND_NAME)),
                           [], (1, COMMAND_WIDTH, WIDE))
            at = index(dr, k, COMMAND_DESC)
            if descs.pop(at, None):
                yield Slot(f"command.STR:{at}", STR_READ - 1, text_at(sr, at), text_at(sc, index(dc, k, COMMAND_DESC)),
                           [visible(text_at(sc, index(dc, k, COMMAND_NAME)))], desc_rules)
        return
    if name in RECORDS:
        size, fields, form = RECORDS[name]
        dr, dc = game.retail(f"{LOGIC}/{name}"), game.modded(f"{LOGIC}/{name}")
        recs = range(len(dr) // size)
        # the rows and width of the fields in all the tables of the same kind (PracticeGame0F–9F)
        kind = [game.retail(f"{LOGIC}/{n}") for n, spec in RECORDS.items() if spec == RECORDS[name]]
        rules = table_rules(font, [text_at(d, at) for d in kind for f, _ in fields
                                   for at in range(f, len(d), size) if japanese_count(text_at(d, at))])
        rules += (form, 1) if form else ()
        for field, room in fields:
            # the original's Shift-JIS strings (a name of full-width letters too), not the "0" of an empty field
            places = [k * size + field for k in recs if max(text_at(dr, k * size + field), default=0) >= 0x81]
            if not places:
                continue
            for at in places:
                base = at - field
                yield Slot(f"{name}:{at}", room - 1, text_at(dr, at), text_at(dc, at),
                           [visible(text_at(dc, base + f)) for f, _ in fields if f != field], rules)
        return
    if name == "ov":
        yield from overlay_slots(game, font)
        return
    if name == "sp_binder.dat":
        # The text of the binder of techniques (ov112): sp_binder.dat points at each string of
        # sp_binder.STR by its byte offset, and the game copies it into 0x100 bytes and draws it
        # with the furigana parser (PAIRED). apply writes sp_binder.STR again, string after string.
        dr, dc = game.retail(f"{LOGIC}/{name}"), game.modded(f"{LOGIC}/{name}")
        sr, sc = game.retail(f"{LOGIC}/sp_binder.STR"), game.modded(f"{LOGIC}/sp_binder.STR")
        recs = range(struct.unpack_from("<H", dr, 0)[0])
        source = lambda k: text_at(sr, struct.unpack_from("<H", dr, 2 + k * BINDER_SIZE + BINDER_TEXT)[0])  # noqa: E731
        rules = table_rules(font, [source(k) for k in recs]) + (PAIRED,)
        items = game.modded(f"{LOGIC}/item.dat")
        for k in recs:
            item = struct.unpack_from("<H", dr, 2 + k * BINDER_SIZE + 2)[0]
            yield Slot(f"{name}:{k}", BINDER_READ - 1, source(k),
                       text_at(sc, struct.unpack_from("<H", dc, 2 + k * BINDER_SIZE + BINDER_TEXT)[0]),
                       [visible(unscramble(items[item * ITEM_SIZE:(item + 1) * ITEM_SIZE])[:ITEM_NAME].split(b"\0")[0])],
                       rules)
        return
    if name == "movie_view_f.dat":
        dr, dc = game.retail(f"{LOGIC}/{name}"), game.modded(f"{LOGIC}/{name}")
        recs = range(len(dr) // MOVIE_SIZE)
        at = lambda k: k * MOVIE_SIZE + MOVIE_TITLE  # noqa: E731
        rules = (table_rules(font, [text_at(dr, at(k)) for k in recs])[0], MOVIE_WIDTH, PAIRED)
        for k in recs:
            yield Slot(f"{name}:{k}", MOVIE_SIZE - MOVIE_TITLE - 1, text_at(dr, at(k)), text_at(dc, at(k)),
                       [decode(text_at(dr, k * MOVIE_SIZE + 0x0C))], rules)
        return
    retail, cur = game.retail(f"{LOGIC}/{name}"), game.modded(f"{LOGIC}/{name}")
    strs = str_strings(retail)
    rules = table_rules(font, [text_at(retail, at) for at, _ in strs if not DEBUG.search(visible(text_at(retail, at)))])
    # the descriptions of the tactics go to the box of the item descriptions (PAIRED)
    descs: dict[int, int] = {}  # original place -> where the mod has it
    if name == "tacticscmd.STR":
        dat, mdat = game.retail(f"{LOGIC}/tacticscmd.dat"), game.modded(f"{LOGIC}/tacticscmd.dat")
        descs = {struct.unpack_from("<H", dat, k + TACTIC_DESC)[0] * STR_STEP:
                 struct.unpack_from("<H", mdat, k + TACTIC_DESC)[0] * STR_STEP for k in range(0, len(dat), TACTIC_SIZE)}
        desc_rules = table_rules(font, [text_at(retail, at) for at, _ in strs if at in descs]) + (PAIRED,)
        rules = table_rules(font, [text_at(retail, at) for at, _ in strs if at not in descs])
    context: list[str] = []
    for at, room in strs:
        text = text_at(cur, descs.get(at, at))
        yield Slot(f"{name}:{at}", STR_READ - 1 if at in descs else room - 1, text_at(retail, at), text,
                   context[-2:], desc_rules if at in descs else rules)
        context = context + [visible(text)]


def overlay_strings(game: Game, oid: int):
    """(offset, room) of the strings of an overlay's data that its code points at (a 32-bit word
    with the address): the code was recompiled with those addresses, so a translation stays in
    place. The room is the string's own, up to its NUL and the padding to 4 bytes (the zero
    words after that could be data)."""
    ov, data = game.overlays[oid], game.overlay(oid, False)
    code = [(a - ov.ram_address, b - ov.ram_address) for a, b in modtool.code_ranges(ov)]
    in_code = lambda at: any(a <= at < b for a, b in code)  # noqa: E731
    targets = sorted({w - ov.ram_address for w in struct.unpack_from(f"<{len(data) // 4}I", data)
                      if 0 <= w - ov.ram_address < len(data)})
    out = []
    for k, at in enumerate(targets):
        source = text_at(data, at)
        if in_code(at) or not jp_text(source):
            continue
        limit = targets[k + 1] if k + 1 < len(targets) else len(data)
        out.append((at, min((at + len(source) + 4) & ~3, limit) - at))
    return out


def overlay_slots(game: Game, font: Font):
    """Strings of the overlays (menus and messages). A message goes through the furigana parser
    of the message boxes and is written PAIRED, with the rows and width of the overlay's own
    messages; a plain label (PLAIN_LABELS) fits the width of its original letters."""
    for oid in sorted(game.overlays):
        retail, cur = game.overlay(oid, False), game.overlay(oid, True)
        places = overlay_strings(game, oid)
        sids = {at: f"ov{oid:03d}:{at:#x}" for at, _ in places}
        messages = [text_at(retail, at) for at, _ in places if sids[at] not in PLAIN_LABELS]
        rules = (table_rules(font, messages) if messages else (1, 0)) + (PAIRED, 1)
        for at, room in places:
            source = text_at(retail, at)
            # a label's button was made for Japanese letters of 12 pixels
            table = (1, 12 * len(visible(source))) if sids[at] in PLAIN_LABELS else rules
            yield Slot(sids[at], room - 1, source, text_at(cur, at), [], table)


def under_build(path: Path):
    """Files with the game's Japanese text never go where git would see them."""
    path = path.resolve()
    if path.is_relative_to(ROOT) and not path.is_relative_to(ROOT / "build"):
        sys.exit(f"error: {path} is in the repository; this writes the game's text, use build/")


def load_translations(folder: Path) -> dict[str, str]:
    out: dict[str, str] = {}
    for f in sorted(folder.glob("*.json")):
        if f.name == "images.json":  # the text drawn on images (tools/image.py)
            continue
        for sid, text in json.loads(f.read_text(encoding="utf-8")).items():
            if not ID.fullmatch(sid):
                sys.exit(f"error: {f.name}: {sid!r} is not a place (evet|mcht|eve|mch:entry:offset, "
                         "item.STR:offset, item.dat:record, movie_view_f.dat:record…)")
            if JAPANESE.search(text):
                sys.exit(f"error: {f.name}: {sid} has Japanese; translations carry only Portuguese")
            if sid in out and out[sid] != text:
                sys.exit(f"error: {sid} is in two files with different texts")
            out[sid] = text
    return out


# --- survey ------------------------------------------------------------------------------

def strings(d: bytes):
    """Runs of valid Shift-JIS that end in NUL, with 2+ Japanese characters."""
    return (raw for _, raw in string_runs(d))


def string_runs(d: bytes):
    """(offset, run) of strings()."""
    i = start = 0
    while i < len(d):
        c = d[i]
        if c == 0:
            if i > start and japanese_count(d[start:i]) >= 2:
                yield start, d[start:i]
            i += 1
            start = i
        elif (0x81 <= c <= 0x9F or 0xE0 <= c <= 0xFC) and i + 1 < len(d) and 0x40 <= d[i + 1] <= 0xFC \
                and d[i + 1] != 0x7F:
            i += 2
        elif 0x20 <= c < 0x7F or c in (9, 10, 13) or 0xA1 <= c <= 0xDF:
            i += 1
        else:
            i += 1
            start = i


# (a dated note of a developer: "…2010/4/8 j_kuramoto")
DEBUG = re.compile(r"[■★⇒◆●]|%d|%x|%\d|ERROR|-----|＝|=|\.SAD|sCmd|dbg|DEBUG|デバッグ|テスト|ダミー|\d{4}/\d{1,2}/\d{1,2}")


def plausible(t: str) -> bool:
    """Text and not code or graphics read as Shift-JIS: kana, common kanji, punctuation."""
    kana = common = rare = good = 0
    for ch in t:
        o = ord(ch)
        if 0x3041 <= o <= 0x30FE:
            kana += 1
            good += 1
        elif 0x4E00 <= o <= 0x9FFF:
            try:
                first = ch.encode("cp932")[0] <= 0x98
            except UnicodeEncodeError:
                first = False
            common, rare, good = common + first, rare + (not first), good + first
        elif ch.isalnum() or ch in " \n\t　、。！？…・「」『』（）～ー－：%/()[]!?.,'\"-+×☆♪○●◆■\\" \
                or 0xFF10 <= o <= 0xFF5A:
            good += 1
    if not t or rare > max(1, (kana + common) // 4) or good / len(t) < 0.85:
        return False
    if kana >= 2:
        return True
    return common >= 2 and len(t) <= 12 and rare == 0


def jp_text(raw: bytes) -> tuple[str, int] | None:
    """Visible Japanese of a string (furigana and markers out) and its kana/kanji, or None."""
    t = RUBY.sub(r"\1", decode(FURIGANA.sub(b"", raw)))
    n = len(JAPANESE.findall(t))
    if n < 2 or DEBUG.search(t) or not plausible(t):
        return None
    return t, n


AREAS = [  # (group or file, name)
    ("31", "História: aberturas de capítulo (evet 31xx)"),
    ("3212", "Capítulo 12 / epílogo (evet 3212)"),
    ("3250", "Eventos extras (evet 3250/3251)"), ("3251", "Eventos extras (evet 3250/3251)"),
    ("32", "História, capítulos 1–11 (evet 3201–3211)"),
    ("3300", "Subeventos: telefonemas, recrutamento (evet 3300)"),
    ("35", "Mensagens de campo, lojas, máquina de busca (evet 35xx)"),
    ("3611", "Scout e rotas de batalha (evet 3611/3613/3618)"),
    ("3613", "Scout e rotas de batalha (evet 3611/3613/3618)"),
    ("3618", "Scout e rotas de batalha (evet 3611/3613/3618)"),
    ("36", "NPCs das cidades (evet 36xx)"),
    ("3700", "Falas dos jogadores nas partidas (evet 3700)"),
    ("mcht", "Recrutar/convidar e eventos das partidas (mcht.pkb)"),
    ("mch", "Outros scripts (ações da partida, ajuda…)"),
    ("ov", "Menus no código (overlays)"),
    ("eve", "Strings dos scripts de evento (eve.pkb)"),
]
FILES = [
    ("data_iz/logic/unitbase.STR", "Perfis dos jogadores (unitbase.STR)"),
    ("data_iz/logic/item.STR", "Descrições de itens (item.STR)"),
    ("data_iz/logic/unitbase", "Nomes de jogadores (unitbase*.dat, sem as leituras em kana)"),
    ("data_iz/logic/tacticscmd", "Táticas (tacticscmd.STR)"),
    ("data_iz/logic/fieldinf", "Nomes de locais (fieldinf)"),
    ("data_iz/logic/movie_view_f", "Títulos da galeria de vídeos (movie_view_f)"),
    ("data_iz/logic/usearch", None),  # "未定" placeholders
    ("data_iz/logic/", "Outras tabelas (golpes, torneios, condições…)"),
    ("data_iz/script/blog", "Blog (blogpost/blogres)"),
    ("data_iz/script/", "Outros scripts (ações da partida, ajuda…)"),
]
KANA_ONLY = re.compile(r"^[ぁ-ヿ　 ]+$")


def blobs(get, path: str):
    """The data of a file: the entries of a pack, decompressed, or the file itself."""
    data = get(path)
    try:
        entries = read_pack(get(path[:-4] + ".pkh"), data) if path.endswith(".pkb") else [(0, data)]
    except TextError:  # a .pkb with another kind of index
        entries = [(0, data)]
    if path.endswith("/item.dat"):
        entries = [(0, b"".join(unscramble(data[k:k + ITEM_SIZE]) for k in range(0, len(data), ITEM_SIZE)))]
    for _, d in entries:
        yield (lz10.try_decompress(d) if d[:1] == b"\x10" else None) or d


def leftover(retail: bytes, cur: bytes, at: int) -> bool:
    """A run of the mod that starts in the middle of an original string, after the NUL of a
    shorter translation: what is left of the original (sometimes touched by an older, longer
    translation), which the game never shows."""
    return 0 < at and len(retail) == len(cur) and retail[at - 1] != 0 and retail[at] != 0


def shown(get, path: str) -> set[int] | None:
    """The strings of a table that the game reads, when another table says which (None: all).
    command.STR keeps the Japanese names the fan translation moved away from, unused."""
    if path != f"{LOGIC}/command.STR":
        return None
    dat = get(f"{LOGIC}/command.dat")
    return {struct.unpack_from("<H", dat, k + f)[0] * STR_STEP
            for k in range(0, len(dat), COMMAND_SIZE) for f in (COMMAND_NAME, COMMAND_DESC)}


def area_of_group(group: str) -> str:
    return next((name for prefix, name in AREAS if group.startswith(prefix)), "Outros eventos")


def cmd_survey(args) -> int:
    game = Game(args.image, args.dir)
    found: dict[str, list[collections.Counter]] = collections.defaultdict(
        lambda: [collections.Counter(), collections.Counter()])  # area -> [retail, mod] text -> count

    for s in slots(game, PACKS):
        area = area_of_group(s.group)
        for side, raw in ((0, s.source), (1, s.current)):
            if got := jp_text(raw):
                found[area][side][got[0]] += 1

    packs = {f"{SCRIPT}/{n}.{ext}" for n in PACKS for ext in ("pkh", "pkb")}
    for path, _ in game.rom.files():
        area = next((name for prefix, name in FILES if path.startswith(prefix)), None)
        if area is None or path in packs or path.endswith(".pkh"):
            continue
        retail = list(blobs(game.retail, path))
        for side, get in ((0, game.retail), (1, game.modded)):
            used = shown(get, path)
            for k, blob in enumerate(blobs(get, path)):
                base = retail[k] if k < len(retail) else b""
                if blob[:4] == b"SSD\0":
                    ops = ssd_opcodes(blob)
                    runs = [(at, ssd_string(blob, at, size)) for at, size in ssd_records(blob)
                            if ops.get(blob[at:at + 2]) != DEBUG_PRINT]
                else:
                    runs = [(at, raw) for at, raw in string_runs(blob)
                            if (used is None or at in used) and not (side and leftover(base, blob, at))]
                for _, raw in runs:
                    if (got := jp_text(raw)) and not (area.startswith("Nomes") and KANA_ONLY.match(got[0])):
                        found[area][side][got[0]] += 1

    for side_ov in (0, 1):
        for ov in game.rom.overlays9():
            if ov.id in modtool.PROTECTED_OVERLAYS:
                continue
            data = game.rom.overlay_decompressed(ov)
            if side_ov:
                p = args.dir / "overlays" / f"ov{ov.id:03d}.bin"
                if p.exists():
                    data = p.read_bytes()
                elif p.with_name(p.name + modtool.PATCH).exists():
                    data = bps.apply(p.with_name(p.name + modtool.PATCH).read_bytes(), data)
            for raw in strings(data):
                if got := jp_text(raw):
                    found["Menus no código (overlays)"][side_ov][got[0]] += 1

    def chars(c: collections.Counter, unique: bool) -> int:
        return sum(len(JAPANESE.findall(t)) * (1 if unique else n) for t, n in c.items())

    print(f"{'área':62} {'japonês: original → agora':>27} {'traduzido':>9}")
    total = [0, 0, 0, 0]
    for area, (base, cur) in sorted(found.items()):
        a, b = chars(base, False), chars(cur, False)
        total = [total[0] + a, total[1] + b, total[2] + chars(base, True), total[3] + chars(cur, True)]
        print(f"{area:62} {a:>12} → {b:<12} {100 * (1 - b / a) if a else 100:8.1f}%")
    print(f"\ntraduzido, com repetições: {100 * (1 - total[1] / total[0]):.1f}% "
          f"({total[1]} de {total[0]} caracteres japoneses ainda no jogo)")
    print(f"traduzido, textos únicos:  {100 * (1 - total[3] / total[2]):.1f}% "
          f"({total[3]} de {total[2]})")
    print("\nFora da conta: imagens, legendas dos vídeos e o ARM9 (veja docs/TRADUCAO.md).")
    return 0


# --- extract and lookup ------------------------------------------------------------------

def cmd_extract(args) -> int:
    under_build(args.out)
    game = Game(args.image, args.dir)
    done = load_translations(args.translations) if args.translations else {}
    only = set(args.only.split(",")) if args.only else None
    items, pairs = [], []
    for s in slots(game):
        show = visible if s.table else decode
        jp = japanese_count(s.current)
        if not jp and japanese_count(s.source) and s.current.strip():
            pairs.append([s.id, show(s.source), show(s.current)])
        # --redo also lists what the mod has in another language (the tables came from an English patch)
        redo = args.redo and s.current != s.source and japanese_count(s.source)
        if not (jp or redo) or s.id in done or (only and not {s.group, s.file, s.group[:2]} & only):
            continue
        items.append({"id": s.id, "group": s.group, "capacity": s.capacity, "jp": show(s.source),
                      "now": "" if jp else show(s.current), "context": s.context})
    args.out.mkdir(parents=True, exist_ok=True)
    for old in args.out.glob("*.txt"):  # lists of a previous run
        old.unlink()
    (args.out / "items.json").write_text(json.dumps(items, ensure_ascii=False, indent=0), encoding="utf-8")
    (args.out / "pairs.json").write_text(json.dumps(pairs, ensure_ascii=False), encoding="utf-8")
    by_group = collections.defaultdict(list)
    for it in items:
        by_group[it["group"]].append(it)
    for group, its in sorted(by_group.items()):
        with open(args.out / f"{group}.txt", "w", encoding="utf-8") as f:
            for it in its:
                f.write(f"### {it['id']}  ({it['capacity']} bytes)\n")
                for c in it["context"][-2:]:
                    f.write(f"  antes: {c[:110]}\n")
                f.write(f"  JP: {it['jp']}\n")
                if it["now"]:
                    f.write(f"  agora: {it['now']}\n")
    counts = collections.Counter(it["group"] for it in items)
    print(f"{args.out}: {len(items)} lines still in Japanese ({len({i['jp'] for i in items})} different), "
          f"{len(pairs)} translated lines for lookup")
    for group, n in sorted(counts.items()):
        print(f"  {group}: {n}  ({area_of_group(group)})")
    return 0


def cmd_lookup(args) -> int:
    pairs = json.loads((args.out / "pairs.json").read_text(encoding="utf-8"))
    words = lambda s: set(re.findall(r"[A-ZÀ-Ú][\wÀ-ú'’\-]+(?: [A-ZÀ-Ú][\wÀ-ú'’\-]+)*", s))  # noqa: E731
    base = collections.Counter()
    for _, _, pt in pairs:
        base.update(words(pt))
    for term in args.terms:
        hits = [(jp, pt) for _, jp, pt in pairs if term in jp]
        c = collections.Counter()
        for _, pt in hits:
            c.update(words(pt))
        best = sorted(((n * n / (base[w] + 1), w, n) for w, n in c.items() if n >= max(1, len(hits) // 4)),
                      reverse=True)[:5]
        print(f"== {term}: {len(hits)} lines; names in them: " + ", ".join(f"{w} ({n})" for _, w, n in best))
        seen = set()
        for jp, pt in hits:
            if pt not in seen and len(seen) < args.examples:
                seen.add(pt)
                print(f"   {jp[:60]}  =>  {pt[:110]}".replace("\\n", " / ").replace("\n", " / "))
    return 0


# --- check and apply ---------------------------------------------------------------------

def checked(game: Game, folder: Path) -> tuple[dict[str, Slot], dict[str, str], int]:
    tr = load_translations(folder)
    wanted = {"item.dat" if sid.startswith("item.") else "command.dat" if sid.startswith("command.")
              else "ov" if sid.startswith("ov") else sid.split(":")[0] for sid in tr}
    by_id = {s.id: s for s in slots(game, [n for n in PACKS + TABLES if n in wanted])}
    font = game.font()
    bad = 0
    for sid, text in tr.items():
        s = by_id.get(sid)
        if s is None:
            print(f"!! {sid}: there is no line there")
            bad += 1
        elif text and s.objective and objective_fit(text, font) is None:
            print(f"!! {sid}: wider than the {OBJECTIVE_PARTS} sprites of the objective\n   {text}")
            bad += 1
        elif text and (p := problems(s.source, placed(s, text, font), s.capacity, font, s.table)):
            print(f"!! {sid}: {'; '.join(p)}\n   {text[:100]}")
            bad += 1
    skipped = sum(1 for t in tr.values() if not t)
    print(f"{len(tr) - bad - skipped} lines ok, {skipped} skipped on purpose, {bad} with problems")
    return by_id, tr, bad


def placed(s: Slot, text: str, font: Font) -> str:
    """The text as written into the game: an objective gets the spaces of objective_fit."""
    return objective_fit(text, font) if s.objective else text


def cmd_check(args) -> int:
    _, _, bad = checked(Game(args.image, args.dir), args.translations)
    return 1 if bad else 0


def put_command_name(tables: dict[str, bytearray], game: Game, record: int, data: bytes):
    """The name of a technique, in its place in command.STR or, if it does not fit, at the end."""
    for name in ("command.dat", "command.STR"):
        if f"{LOGIC}/{name}" not in tables:
            tables[f"{LOGIC}/{name}"] = bytearray(game.modded(f"{LOGIC}/{name}"))
    dat, strs = tables[f"{LOGIC}/command.dat"], tables[f"{LOGIC}/command.STR"]
    at = record * COMMAND_SIZE + COMMAND_NAME
    off = struct.unpack_from("<H", dat, at)[0] * STR_STEP
    room = dict(str_strings(bytes(strs))).get(off, 0)
    if len(data) > room:
        strs += bytes(-len(strs) % STR_STEP)
        off, room = len(strs), (len(data) + STR_STEP - 1) // STR_STEP * STR_STEP
        strs += bytes(room)
        struct.pack_into("<H", dat, at, off // STR_STEP)
    strs[off:off + room] = data + bytes(room - len(data))


# A table of descriptions -> the table that points at them: (file, record size, field of the index, scrambled)
DESCRIPTIONS = {"item.STR": ("item.dat", ITEM_SIZE, ITEM_DESC, True),
                "command.STR": ("command.dat", COMMAND_SIZE, COMMAND_DESC, False),
                "tacticscmd.STR": ("tacticscmd.dat", TACTIC_SIZE, TACTIC_DESC, False)}


def put_description(tables: dict[str, bytearray], game: Game, name: str, at: int, data: bytes):
    """A description (its place is the original's offset), where the mod has it or, if it does
    not fit there, at the end of the .STR, the records that point at it changed to match."""
    dat_name, size, field, scrambled = DESCRIPTIONS[name]
    for rel in (name, dat_name):
        if f"{LOGIC}/{rel}" not in tables:
            tables[f"{LOGIC}/{rel}"] = bytearray(game.modded(f"{LOGIC}/{rel}"))
    strs, dat = tables[f"{LOGIC}/{name}"], tables[f"{LOGIC}/{dat_name}"]
    retail = game.retail(f"{LOGIC}/{dat_name}")
    read = (lambda d, k: unscramble(bytes(d[k * size:(k + 1) * size]))) if scrambled else \
        (lambda d, k: bytes(d[k * size:(k + 1) * size]))  # noqa: E731
    recs = [k for k in range(len(retail) // size) if struct.unpack_from("<H", read(retail, k), field)[0] * STR_STEP == at]
    now = {struct.unpack_from("<H", read(dat, k), field)[0] * STR_STEP for k in recs}
    off = now.pop() if len(now) == 1 else at
    room = dict(str_strings(bytes(strs))).get(off, 0)
    if len(data) > room:
        strs += bytes(-len(strs) % STR_STEP)
        off, room = len(strs), (len(data) + STR_STEP - 1) // STR_STEP * STR_STEP
        strs += bytes(room)
        for k in recs:
            rec = bytearray(read(dat, k))
            struct.pack_into("<H", rec, field, off // STR_STEP)
            dat[k * size:(k + 1) * size] = scramble(bytes(rec)) if scrambled else rec
    strs[off:off + room] = data + bytes(room - len(data))


def put_binder(tables: dict[str, bytearray], game: Game, new: dict[int, bytes]):
    """sp_binder.STR written again with the new strings (NUL included), each record of
    sp_binder.dat pointing at its own."""
    if f"{LOGIC}/sp_binder.dat" not in tables:
        tables[f"{LOGIC}/sp_binder.dat"] = bytearray(game.modded(f"{LOGIC}/sp_binder.dat"))
    dat = tables[f"{LOGIC}/sp_binder.dat"]
    old = game.modded(f"{LOGIC}/sp_binder.STR")
    out = bytearray()
    for k in range(struct.unpack_from("<H", dat, 0)[0]):
        at = 2 + k * BINDER_SIZE + BINDER_TEXT
        data = new.get(k) or text_at(old, struct.unpack_from("<H", dat, at)[0]) + b"\0"
        if len(out) > 0xFFFF:
            raise TextError("sp_binder.STR grew past the 16-bit offsets of sp_binder.dat")
        struct.pack_into("<H", dat, at, len(out))
        out += data
    tables[f"{LOGIC}/sp_binder.STR"] = out


def write_file(mod: Path, game: Game, rel: str, data: bytes) -> bool:
    """A file of the mod as a patch (none if it is the retail file); False if it was already so."""
    dst = mod / "files" / rel
    patch = dst.with_name(dst.name + modtool.PATCH)
    if data == game.modded(rel) and not dst.exists():
        return False
    dst.unlink(missing_ok=True)
    if data == game.retail(rel):
        patch.unlink(missing_ok=True)
    else:
        modtool.write(dst, game.retail(rel), data, True)
    return True


def write_overlay(mod: Path, game: Game, oid: int, data: bytes) -> bool:
    """An overlay of the mod as a patch, like write_file."""
    dst = mod / "overlays" / f"ov{oid:03d}.bin"
    patch = dst.with_name(dst.name + modtool.PATCH)
    if data == game.overlay(oid, True) and not dst.exists():
        return False
    dst.unlink(missing_ok=True)
    if data == game.overlay(oid, False):
        patch.unlink(missing_ok=True)
    else:
        modtool.write(dst, game.overlay(oid, False), data, True)
    return True


def cmd_apply(args) -> int:
    game = Game(args.image, args.dir)
    by_id, tr, bad = checked(game, args.translations)
    if bad:
        print("nothing written: fix the problems first")
        return 1
    font = game.font()
    changed: dict[str, dict[int, bytearray]] = collections.defaultdict(dict)
    tables: dict[str, bytearray] = {}
    overlays: dict[int, bytearray] = {}
    binder: dict[int, bytes] = {}  # record of sp_binder.dat -> its new string
    for sid, text in tr.items():
        if not text:
            continue
        s = by_id[sid]
        room = s.capacity + 1
        if s.file.startswith("ov"):
            oid = int(s.file[2:])
            d = overlays.setdefault(oid, bytearray(game.overlay(oid, True)))
            data = table_bytes(text, s.table[2] if len(s.table) > 2 else None) + b"\0"
            d[s.offset:s.offset + room] = data + bytes(room - len(data))
            continue
        if s.table:
            rel = f"{LOGIC}/{s.file}"
            d = tables.setdefault(rel, bytearray(game.modded(rel)))
            data = table_bytes(text, s.table[2] if len(s.table) > 2 else None) + b"\0"
            if s.file == "command.dat":
                put_command_name(tables, game, s.offset, data)
            elif s.file in DESCRIPTIONS and PAIRED in s.table[2:3]:
                put_description(tables, game, s.file, s.offset, data)
            elif s.file == "sp_binder.dat":
                binder[s.offset] = data
            elif s.file == "movie_view_f.dat":
                at = s.offset * MOVIE_SIZE + MOVIE_TITLE
                d[at:at + room] = data + bytes(room - len(data))
            elif s.file == "item.dat":
                at = s.offset * ITEM_SIZE
                rec = bytearray(unscramble(d[at:at + ITEM_SIZE]))
                rec[:room] = data + bytes(room - len(data))
                d[at:at + ITEM_SIZE] = scramble(bytes(rec))
            else:
                d[s.offset:s.offset + room] = data + bytes(room - len(data))
            continue
        d = changed[s.file].setdefault(s.key, bytearray(game.pack(s.file, True)[s.key]))
        data = encode(placed(s, text, font)) + b"\0"
        d[s.offset:s.offset + room] = data + bytes(room - len(data))
    if any(by_id[sid].objective or PAIRED in (by_id[sid].table or ())[2:3] for sid, text in tr.items() if text):
        tables[FONT] = bytearray(fit_font(game.modded(FONT)))  # the SPACER without ink
    if binder:
        put_binder(tables, game, binder)
    for rel, data in tables.items():
        if write_file(args.dir, game, rel, bytes(data)):
            print(f"{rel}: changed")
    for oid, data in overlays.items():
        if write_overlay(args.dir, game, oid, bytes(data)):
            print(f"overlay {oid}: changed")
    for name, entries in changed.items():
        pkh = game.modded(f"{SCRIPT}/{name}.pkh")
        stored = read_pack(pkh, game.modded(f"{SCRIPT}/{name}.pkb"))
        new = []
        updated = 0
        for key, data in stored:
            if key in entries and bytes(entries[key]) != game.pack(name, True)[key]:
                data = lz10.compress(bytes(entries[key])) if data[:1] == b"\x10" else bytes(entries[key])
                updated += 1
            new.append((key, data))
        if not updated:
            continue
        h, b = write_pack(pkh, new)
        for ext, data in ((".pkh", h), (".pkb", b)):
            write_file(args.dir, game, f"{SCRIPT}/{name}{ext}", data)
        print(f"{name}: {updated} entries changed")
    print(f"{args.dir}: done; raise the version in mod.ini and check with tools/mod.py check")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="command", required=True)

    p = sub.add_parser("survey", help="how much of the game is still in Japanese, by area")
    p.add_argument("image", type=Path, help="boej.pak or the retail .nds")
    p.add_argument("dir", type=Path, help="the mod folder")
    p.set_defaults(fn=cmd_survey)

    p = sub.add_parser("extract", help="the lines still in Japanese, to translate (writes under build/)")
    p.add_argument("image", type=Path)
    p.add_argument("dir", type=Path)
    p.add_argument("out", type=Path, help="a folder under build/")
    p.add_argument("--only", help="groups, e.g. 3300,3611,mcht,eve, ov057 or ov (all the overlays)")
    p.add_argument("--translations", type=Path, help="leave out the lines this translation already has")
    p.add_argument("--redo", action="store_true", help="also the lines the mod has in another language")
    p.set_defaults(fn=cmd_extract)

    p = sub.add_parser("lookup", help="how terms were translated in the lines already done")
    p.add_argument("out", type=Path, help="the folder of extract")
    p.add_argument("terms", nargs="+")
    p.add_argument("--examples", type=int, default=3)
    p.set_defaults(fn=cmd_lookup)

    for name, fn, text in (("check", cmd_check, "what in a translation does not fit"),
                           ("apply", cmd_apply, "write a translation into the mod, as patches")):
        p = sub.add_parser(name, help=text)
        p.add_argument("image", type=Path)
        p.add_argument("dir", type=Path)
        p.add_argument("translations", type=Path, help="folder of .json files")
        p.set_defaults(fn=fn)

    args = ap.parse_args()
    try:
        return args.fn(args)
    except TextError as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    sys.exit(main())
