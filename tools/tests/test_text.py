"""Tests for tools/ntr/lz10.py, tools/ntr/text.py and the translations of tools/text.py."""

import json
import random
import struct
import tempfile
import unittest
from pathlib import Path

import text as texttool
from ntr import lz10
from ntr.text import (BOX_ROWS, Font, JAPANESE, PAIRED, TextError, WIDE, decode, encode, lines, paired,
                      problems, read_pack, SPACER,
                      DEBUG_PRINT, objective_cost, objective_fit, objective_layout, scramble, ssd_opcodes,
                      ssd_refs,
                      str_strings, table_bytes, unscramble, wide, write_pack)

ROOT = Path(__file__).resolve().parents[2]


class FixedFont(Font):
    """Every character 6 pixels wide, the pixel after it included (no NFTR needed)."""

    def __init__(self):
        pass

    def measure(self, s: str, gap: int = 1) -> int:
        return (5 + gap) * len(s)


def record(text: bytes, size: int) -> bytes:
    body = text + b"\0"
    return struct.pack(">I", size) + body + bytes(size - 4 - len(body))


class Lz10Test(unittest.TestCase):
    def test_roundtrip(self):
        rng = random.Random(3)
        for data in (b"", b"a", b"abcabcabcabc" * 50, bytes(rng.randrange(4) for _ in range(5000)),
                     bytes(rng.randrange(256) for _ in range(3000))):
            packed = lz10.compress(data)
            self.assertEqual(len(packed) % 4, 0)
            self.assertEqual(lz10.decompress(packed), data)
        self.assertLess(len(lz10.compress(b"\0" * 4096)), 600)

    def test_rejects(self):
        self.assertIsNone(lz10.try_decompress(b"\x11\x00\x00\x00"))
        self.assertIsNone(lz10.try_decompress(b"\x10\x10\x00\x00\x80\x00"))  # copy before the start


class TextTest(unittest.TestCase):
    def test_accents_one_byte(self):
        s = "Você não sabe? É a Ação!"
        b = encode(s)
        self.assertEqual(len(b), len(s))
        self.assertEqual(decode(b), s)
        self.assertEqual(encode("…"), b"...")
        with self.assertRaises(TextError):
            encode("º")

    def test_lines_and_room(self):
        # a line with one furigana marker, its furigana record, then a plain line
        line = "%1F漢字だ".encode("cp932")
        d = record(line, 20) + record("かんじ".encode("cp932"), 12) + record(b"plain", 12)
        got = lines(d)
        self.assertEqual([(at, end) for at, end, _ in got], [(4, 32), (36, 44)])
        self.assertEqual(got[0][2], line)

    def test_pack_roundtrip(self):
        pkh = bytearray(b"PackNum 20080626".ljust(0x30, b"\0") + bytes(24))
        struct.pack_into("<I", pkh, 0x1C, 16)
        h, b = write_pack(bytes(pkh), [(7, b"abc"), (9, b"x" * 40)])
        self.assertEqual(read_pack(h, b), [(7, b"abc"), (9, b"x" * 40)])
        self.assertEqual(struct.unpack_from("<III", h, 0x3C), (9, 16, 40))
        self.assertEqual(struct.unpack_from("<I", h, 0x1C)[0], 48)

    def test_script_pointers(self):
        data = b"\x01\x00\x01\x10@96,44\0\0\0\0\0" + b"\x02\x00\x01\x08\0\0\0\0"
        ssd = bytearray(b"SSD\0" + bytes(28))
        struct.pack_into("<II", ssd, 16, 0, len(data))
        self.assertEqual(ssd_refs(bytes(ssd) + data), {96: 44})

    def test_script_opcodes(self):
        # instructions [u16 id][u16 size][u16 opcode]...: a debug print (0x3070) and who speaks (0x3019)
        code = struct.pack("<HHHH", 1, 8, DEBUG_PRINT, 0) + struct.pack("<HHHH", 2, 8, 0x3019, 0)
        ssd = bytearray(b"SSD\0" + bytes(28))
        struct.pack_into("<II", ssd, 16, len(code), 0)
        self.assertEqual(ssd_opcodes(bytes(ssd) + code), {b"\x01\0": DEBUG_PRINT, b"\x02\0": 0x3019})

    def test_problems(self):
        font = FixedFont()
        self.assertEqual(problems(b"%s", "Oi, %s!", 20, font), [])
        self.assertIn("%s missing or extra", problems(b"%s", "Oi!", 20, font))
        self.assertTrue(problems(b"", "x" * 30, 20, font))  # more bytes than the room
        long = " ".join(["palavra"] * 40)  # wraps into more rows than a box shows
        self.assertTrue(any("rows" in p for p in problems(b"", long, 1000, font)))
        self.assertEqual(len(font.wrap("aa bb")), 1)
        self.assertGreater(BOX_ROWS, 0)
        self.assertIn("still has Japanese", problems(b"", "円堂", 20, font))


class TableTest(unittest.TestCase):
    def test_item_records(self):
        rng = random.Random(5)
        for _ in range(20):
            rec = bytes(rng.randrange(256) for _ in range(0x2C))
            self.assertEqual(unscramble(scramble(rec)), rec)
        self.assertEqual(scramble(bytes(0x2C)), b"\xad" * 0x2C)  # zeros read as 0xAD in the file

    def test_str_strings(self):
        d = bytearray(32 * 8)
        d[0:3], d[64:70], d[96:100] = b"abc", b"x\ny\0zz", b"long"  # "zz" is the tail of the 2nd one
        self.assertEqual(str_strings(bytes(d)), [(0, 64), (64, 32), (96, 128)])
        self.assertEqual(table_bytes("a\\nb"), b"a\nb")
        self.assertEqual(wide("God Hand!"), "Ｇｏｄ\u3000Ｈａｎｄ！")
        self.assertEqual(table_bytes("Go", WIDE), "Ｇｏ".encode("cp932"))

    def test_paired_for_the_gallery(self):
        # a one-byte character read alone gets the SPACER; an accent carries the next letter
        sp = encode(SPACER)
        self.assertEqual(paired(encode("Aé")), sp + b"A" + encode("é") + sp)
        self.assertEqual(paired(encode("éa b")), encode("éa") + sp + b" " + sp + b"b")
        self.assertEqual(paired(b"%s!"), b"%s" + sp + b"!")  # sprintf sees %s whole
        b = "Ｂ".encode("cp932")
        self.assertEqual(paired(b + b"\n" + encode("ú")), b + b"\n" + encode("ú") + sp)
        self.assertEqual(table_bytes("a b", PAIRED), sp + b"a" + sp + b" " + sp + b"b")
        # the gallery draws with no pixel between letters, and the pairs take twice the bytes
        font = FixedFont()
        self.assertEqual(problems(b"x", "abc", 6, font, (1, 18, PAIRED)), [])
        self.assertEqual(len(problems(b"x", "abcd", 6, font, (1, 18, PAIRED))), 2)

    def test_command_name_moves_when_it_does_not_fit(self):
        dat = bytearray(0x24 * 2)
        struct.pack_into("<H", dat, 0x24 + 0x18, 1)  # record 1: name at index 1
        strs = bytearray(32 * 3)
        strs[32:35], strs[64:67] = b"abc", b"xyz"
        tables = {"data_iz/logic/command.dat": dat, "data_iz/logic/command.STR": strs}
        texttool.put_command_name(tables, None, 1, b"short\0")
        self.assertEqual(bytes(strs[32:38]), b"short\0")
        texttool.put_command_name(tables, None, 1, b"x" * 40 + b"\0")
        self.assertEqual(struct.unpack_from("<H", dat, 0x24 + 0x18)[0], 3)  # moved to the end
        self.assertEqual(bytes(strs[96:137]), b"x" * 40 + b"\0")
        self.assertEqual(bytes(strs[64:67]), b"xyz")  # the next string is intact

    def test_leftovers_and_unused_strings_are_not_counted(self):
        # "Oi" over "abcdef": the "ef" after its NUL is the end of the original, never shown
        retail, cur = b"\0abcdef\0", b"\0Oi\0\0ef\0"
        self.assertEqual(list(texttool.string_runs(b"\0" + "円堂".encode("cp932") + b"\0")),
                         [(1, "円堂".encode("cp932"))])
        self.assertTrue(texttool.leftover(retail, cur, 5))
        self.assertFalse(texttool.leftover(retail, cur, 1))
        self.assertFalse(texttool.leftover(b"\0abcd\0ef\0", b"\0Oi\0\0\0ef\0", 6))  # its own string
        dat = bytearray(0x24 * 2)
        struct.pack_into("<HH", dat, 0x24 + 0x18, 3, 4)  # record 1: name at index 3, description at 4
        files = {"data_iz/logic/command.dat": bytes(dat)}
        self.assertEqual(texttool.shown(files.get, "data_iz/logic/command.STR"), {0, 96, 128})
        self.assertIsNone(texttool.shown(files.get, "data_iz/logic/item.STR"))

    def test_record_tables(self):
        self.assertRegex("PracticeGame3F.dat:1518", texttool.ID.pattern)
        self.assertRegex("command.STR:7232", texttool.ID.pattern)
        self.assertNotRegex("PracticeGameF.dat:1", texttool.ID.pattern)
        for name, (size, fields, _) in texttool.RECORDS.items():
            self.assertTrue(all(f + n <= size for f, n in fields), name)

    def test_description_moves_when_it_does_not_fit(self):
        class Fake:  # the retail tables; the mod's are already in `tables`
            def __init__(self, files):
                self.files = files

            def retail(self, rel):
                return self.files[rel]
        dat = bytearray(0x14 * 2)
        struct.pack_into("<H", dat, 0x14 + 0x0E, 1)  # tactic 1: description at index 1
        strs = bytearray(32 * 3)
        strs[32:35], strs[64:67] = b"abc", b"xyz"
        tables = {"data_iz/logic/tacticscmd.dat": dat, "data_iz/logic/tacticscmd.STR": strs}
        game = Fake({"data_iz/logic/tacticscmd.dat": bytes(dat)})
        texttool.put_description(tables, game, "tacticscmd.STR", 32, b"short\0")
        self.assertEqual(bytes(strs[32:38]), b"short\0")
        texttool.put_description(tables, game, "tacticscmd.STR", 32, b"y" * 50 + b"\0")
        self.assertEqual(struct.unpack_from("<H", dat, 0x14 + 0x0E)[0], 3)  # moved to the end
        self.assertEqual(bytes(strs[96:147]), b"y" * 50 + b"\0")
        texttool.put_description(tables, game, "tacticscmd.STR", 32, b"z" * 50 + b"\0")
        self.assertEqual((struct.unpack_from("<H", dat, 0x14 + 0x0E)[0], len(strs)), (3, 160))  # moved once

    def test_binder_is_written_again(self):
        class Fake:
            def modded(self, rel):
                return b"one\0two\0"
        dat = bytearray(2 + 12 * 2)
        struct.pack_into("<H", dat, 0, 2)
        struct.pack_into("<H", dat, 2 + 12 + 10, 4)  # record 1 at "two"
        tables = {"data_iz/logic/sp_binder.dat": dat}
        texttool.put_binder(tables, Fake(), {0: b"longer one\0"})
        self.assertEqual(bytes(tables["data_iz/logic/sp_binder.STR"]), b"longer one\0two\0")
        self.assertEqual(struct.unpack_from("<H", dat, 2 + 12 + 10)[0], 11)

    def test_table_problems(self):
        font = FixedFont()
        self.assertEqual(problems(b"", "Leite\\nRecupera GP.", 40, font, (2, 120)), [])
        self.assertTrue(problems(b"", "a\\nb\\nc", 40, font, (2, 120)))  # 3 rows
        self.assertTrue(problems(b"", "x" * 21, 40, font, (2, 120)))  # 126 px, no wrap in tables
        self.assertTrue(problems(b"", "[a/b]", 40, font, (2, 120)))


class ObjectiveFont(Font):
    """FONT12 widths of the letters measured in the game: a/b/c/d/e/u 5, t 3, space 3,
    SPACER 0 (each plus the 1-pixel gap)."""

    def __init__(self):
        pass

    def measure(self, s: str) -> int:
        return sum({"t": 4, " ": 4, SPACER: 1}.get(ch, 6) for ch in s)


class ObjectiveTest(unittest.TestCase):
    """Positions measured in the game (pixels from the left of the first sprite)."""

    def places(self, text):
        return [p * 60 + x for ch, (p, x, _) in zip(text, objective_layout(text, ObjectiveFont()))
                if ch not in (" ", SPACER)]

    def test_layout_matches_the_game(self):
        self.assertEqual(self.places("aaaaaaaaa" + SPACER * 3 + "b" + SPACER * 2 + "cc dd"),
                         [0, 6, 12, 18, 24, 30, 36, 42, 48, 57, 60, 66, 76, 82])
        self.assertEqual(self.places("a" * 10 + SPACER * 5 + "b" * 10 + "  cc"),
                         list(range(0, 120, 6)) + [124, 130])
        self.assertEqual(self.places("tete"), [0, 4, 10, 14])

    def test_fit(self):
        font = ObjectiveFont()
        self.assertIsNone(objective_cost("aaaaaaaaa tt", font))  # t at 58-61, the 2nd t over it
        fixed = objective_fit("aaaaaaaaa tt", font)
        self.assertIsNotNone(objective_cost(fixed, font))
        self.assertEqual(fixed.replace(SPACER, ""), "aaaaaaaaa tt")
        self.assertEqual(objective_fit("aa bb", font), "aa bb")


class TranslationsTest(unittest.TestCase):
    def test_rejects_japanese_and_bad_places(self):
        with tempfile.TemporaryDirectory() as tmp:
            f = Path(tmp) / "a.json"
            f.write_text(json.dumps({"evet:1:4": "Oi"}), encoding="utf-8")
            self.assertEqual(texttool.load_translations(Path(tmp)), {"evet:1:4": "Oi"})
            f.write_text(json.dumps({"evet:1:4": "円堂"}), encoding="utf-8")
            with self.assertRaises(SystemExit):
                texttool.load_translations(Path(tmp))
            f.write_text(json.dumps({"item.dat:3": "Leite", "tacticscmd.STR:64": "Oi"}), encoding="utf-8")
            self.assertEqual(len(texttool.load_translations(Path(tmp))), 2)
            f.write_text(json.dumps({"logic:1:4": "Oi"}), encoding="utf-8")
            with self.assertRaises(SystemExit):
                texttool.load_translations(Path(tmp))

    def test_versioned_translations_have_no_game_text(self):
        """translations/ is versioned: only places and Portuguese, never the game's Japanese."""
        for f in sorted((ROOT / "translations").rglob("*")):
            if f.is_file():
                self.assertEqual(f.suffix, ".json", f)
                data = json.loads(f.read_text(encoding="utf-8"))
                for sid, t in data.items():
                    self.assertRegex(sid, texttool.ID.pattern)
                    self.assertIsNone(JAPANESE.search(t), f"{f.name}: {sid}")

    def test_extract_writes_only_under_build(self):
        with self.assertRaises(SystemExit):
            texttool.under_build(ROOT / "docs" / "text")
        texttool.under_build(ROOT / "build" / "text")
        texttool.under_build(Path(tempfile.gettempdir()) / "text")


if __name__ == "__main__":
    unittest.main()
