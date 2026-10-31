"""Tests for tools/ntr/lz10.py, tools/ntr/text.py and the translations of tools/text.py."""

import json
import random
import struct
import tempfile
import unittest
from pathlib import Path

import language
import text as texttool
from ntr import lz10
from ntr import text as ntrtext
from ntr.text import (BOX_ROWS, Font, JAPANESE, PAIRED, TextError, WIDE, decode, encode, lines, paired,
                      problems, read_pack, SPACER,
                      DEBUG_PRINT, objective_cost, objective_fit, objective_layout, scramble, ssd_opcodes,
                      ssd_records, ssd_refs, ssd_rewrite, ssd_string,
                      str_strings, SUBTITLED, subtitle_file, subtitle_records, subtitled, table_bytes,
                      unscramble, wide, write_pack)

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

    def test_subtitles(self):
        # no SPACER: letters full-width, a space (0xCF) and an accent take the next letter as their pair
        self.assertEqual(subtitled("Ha a"), wide("Ha").encode("cp932") + b"\xcfa")
        self.assertEqual(subtitled("ão!"), encode("ão") + wide("!").encode("cp932"))
        self.assertEqual(subtitled("é"), encode("é" + SPACER))
        self.assertEqual(table_bytes("a", SUBTITLED), wide("a").encode("cp932"))
        with self.assertRaises(TextError):
            subtitled("a\nb")  # one row
        # records [first frame][last frame][size][text, NUL, padding to 4], then 0xFFFFFFFF
        rec = lambda a, b, t: struct.pack("<III", a, b, len(t)) + t  # noqa: E731
        d = rec(1, 2, b"ab\0\0") + rec(3, 4, b"cdefg\0\0\0") + b"\xff" * 4
        self.assertEqual([(at, size) for at, size, _ in subtitle_records(d)], [(0, 16), (16, 20)])
        new = subtitle_file(d, {0: b"xyzw"})
        self.assertEqual(new, rec(1, 2, b"xyzw\0\0\0\0") + d[16:])

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

    def test_overlay_lists(self):
        # a pointer at "aa" gives the list "aa", "bbb", "c" (found after each NUL); "zz" has its own
        d = bytearray(32)
        d[0:9], d[16:18] = b"aa\0bbb\0c\0", b"zz"
        lists = texttool.overlay_lists(bytes(d), [(0, 4), (16, 4)])
        self.assertEqual(lists, {0: ([0, 3, 7], 12)})
        d[0:8] = b"aaaa\0b\0c"  # the mod moved them: the strings are found by walking the NULs
        self.assertEqual(texttool.list_bytes(bytes(d), 0, 3), [b"aaaa", b"b", b"c"])

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


class ScriptRewriteTest(unittest.TestCase):
    @staticmethod
    def script(strings: list[bytes]) -> bytes:
        code = struct.pack("<HHHBB", 1, 8, 0x4037, 0, 0)  # one instruction, id 1
        data = b""
        for k, t in enumerate(strings):
            body = t + b"\0"
            size = (4 + len(body) + 3) & ~3
            data += struct.pack("<HBB", 1, k, size) + body + bytes(size - 4 - len(body))
        head = struct.pack("<8I", int.from_bytes(b"SSD\0", "little"), 0x30001, 0x20 + len(code) + len(data), 7,
                           len(code), len(data), 0, 0)
        return head + code + data

    def test_a_string_grows_and_the_rest_moves(self):
        d = self.script([b"ab", b"@0,96", b"xyz"])
        recs = ssd_records(d)
        new = ssd_rewrite(d, {recs[0][0]: b"Raimon Town"})
        words = struct.unpack_from("<8I", new, 0)
        self.assertEqual(words[2], len(new))
        self.assertEqual(words[3], 7)  # the rest of the header stays
        self.assertEqual(words[5], len(new) - 0x20 - words[4])
        self.assertEqual([ssd_string(new, *r) for r in ssd_records(new)], [b"Raimon Town", b"@0,96", b"xyz"])
        self.assertEqual(ssd_refs(new), ssd_refs(d))
        self.assertEqual(new[:0x20 + words[4]][0x20:], d[0x20:0x20 + words[4]])  # the code does not change
        same = ssd_rewrite(d, {recs[2][0]: b"ab"})  # fits: nothing moves
        self.assertEqual(len(same), len(d))
        with self.assertRaises(TextError):
            ssd_rewrite(d, {recs[0][0]: b"x" * 300})


class TranslationsTest(unittest.TestCase):
    def tearDown(self):
        ntrtext.set_charset(ntrtext.LETTERS)  # the other tests are in PT-BR

    def test_charset(self):
        ntrtext.set_charset({"ñ": 0xB1, "Ñ": 0xD1})
        self.assertEqual(encode("Año"), b"A\xb1o")
        self.assertEqual(decode(b"A\xb1o\xcf"), "Año ")
        with self.assertRaises(TextError):
            encode("ã")  # a letter of PT-BR, not of this language
        for bad in ({"ñ": 0x41}, {"ñ": 0xDF}, {"ñ": 0xB1, "Ñ": 0xB1}, {"n": 0xB1}):
            with self.assertRaises(TextError):
                ntrtext.set_charset(bad)

    def test_language_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            (d / "texts").mkdir()
            (d / "texts" / "a.json").write_text(json.dumps({"evet:1:4": "Año"}), encoding="utf-8")
            (d / "notes.json").write_text("{}", encoding="utf-8")  # not in "texts"
            m = {"type": "translation", "language": "es", "mod": "Spanish", "name": "Español",
                 "version": "0.1", "charset": {"ñ": "B1"}, "texts": "texts/*.json"}
            (d / "language.json").write_text(json.dumps(m), encoding="utf-8")
            lang = language.load(d)
            self.assertEqual((lang.code, lang.mod, lang.charset), ("es", "Spanish", {"ñ": 0xB1}))
            self.assertEqual(lang.info, {"name": "Español", "version": "0.1"})
            self.assertEqual(lang.texts, [d / "texts" / "a.json"])
            self.assertEqual(texttool.load_translations(d), {"evet:1:4": "Año"})
            self.assertEqual(encode("ñ"), b"\xb1")  # loading the lines made the language active
            for bad in ({"charset": {"ñ": "41"}}, {"charset": {"ñ": "zz"}}, {"mod": "a/b"}, {"colour": 1},
                        {"type": "font"}, {"language": ""}):
                (d / "language.json").write_text(json.dumps(m | bad), encoding="utf-8")
                with self.assertRaises(SystemExit, msg=bad):
                    language.load(d)

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
        """translations/ is versioned: only places and the translated text, never the game's Japanese."""
        folders = [d for d in sorted((ROOT / "translations").iterdir()) if d.is_dir() and d.name != language.BASES]
        self.assertTrue(folders)
        for d in folders:
            lang = language.load(d)
            self.assertTrue(lang.manifest, f"{d.name}: no {language.MANIFEST}")
            self.assertTrue(lang.texts, d)
            fonts = {f for pair in lang.fonts.values() for f in pair}
            for f in sorted(d.rglob("*")):
                if f.is_file():
                    self.assertIn(f, {*lang.texts, lang.images, d / language.MANIFEST, *fonts, *lang.pictures.values()},
                                  f"{f}: not in the manifest")
            manifest = (d / language.MANIFEST).read_text(encoding="utf-8")
            self.assertIsNone(JAPANESE.search(manifest), f"{d.name}/{language.MANIFEST}")
            if lang.images:  # archive -> entry -> edits; the texts are what is drawn
                data = json.loads(lang.images.read_text(encoding="utf-8"))
                texts = [e["text"] for entries in data.values() for edits in entries.values()
                         for e in (edits["edits"] if isinstance(edits, dict) else edits) if "text" in e]
                self.assertTrue(texts)
                for t in texts:
                    self.assertIsNone(JAPANESE.search(t), f"{lang.images.name}: {t}")
            for f in lang.texts:
                for sid, t in json.loads(f.read_text(encoding="utf-8")).items():
                    self.assertRegex(sid, texttool.ID.pattern)
                    self.assertIsNone(JAPANESE.search(t), f"{f.name}: {sid}")

    def test_official_mods_match_their_language(self):
        """The mod.ini of an official translation mod says what its language.json says."""
        for d in sorted((ROOT / "translations").iterdir()):
            if not d.is_dir() or d.name == language.BASES:
                continue
            lang = language.load(d)
            ini = ROOT / "mods" / lang.mod / "mod.ini"
            if not ini.exists():
                continue
            fields = dict(line.split("=", 1) for line in ini.read_text(encoding="utf-8").splitlines()
                          if "=" in line and not line.startswith(";"))
            fields = {k.strip(): v.strip() for k, v in fields.items()}
            for key, value in lang.info.items():
                self.assertEqual(fields.get(key), value, f"{ini}: {key}")

    def test_extract_writes_only_under_build(self):
        with self.assertRaises(SystemExit):
            texttool.under_build(ROOT / "docs" / "text")
        texttool.under_build(ROOT / "build" / "text")
        texttool.under_build(Path(tempfile.gettempdir()) / "text")


if __name__ == "__main__":
    unittest.main()
