"""Tests for tools/mod.py with synthetic ROMs (no game data needed)."""

import argparse
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

import mod
from ntr.rom import Overlay
from tests.test_ntr import make_rom


def modified_rom() -> bytes:
    rom = bytearray(make_rom())
    rom[0x500:0x504] = b"MODS"
    return bytes(rom)


class ModTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        (self.dir / "retail.nds").write_bytes(make_rom())
        (self.dir / "hack.nds").write_bytes(modified_rom())

    def tearDown(self):
        self.tmp.cleanup()

    def args(self, **kw):
        defaults = {"name": None, "author": None, "description": None, "patch": False}
        return argparse.Namespace(**{**defaults, **kw})

    def test_from_dump_keeps_only_changed_files(self):
        out = self.dir / "mods" / "hack"
        mod.cmd_from_dump(self.args(retail=self.dir / "retail.nds", modified=self.dir / "hack.nds", out=out))
        self.assertEqual((out / "files" / "dir" / "a.bin").read_bytes(), b"MODS")
        self.assertIn("game = BOEJ", (out / "mod.ini").read_text(encoding="utf-8"))
        errors, warnings = mod.check(out, mod.load_image(self.dir / "retail.nds"))
        self.assertEqual((errors, warnings), ([], []))

    def test_patches_carry_the_same_files(self):
        out, whole = self.dir / "mods" / "hack", self.dir / "whole"
        retail = self.dir / "retail.nds"
        mod.cmd_from_dump(self.args(retail=retail, modified=self.dir / "hack.nds", out=out, patch=True))
        self.assertFalse((out / "files" / "dir" / "a.bin").exists())
        self.assertTrue((out / "files" / "dir" / "a.bin.bps").exists())
        self.assertEqual(mod.check(out, mod.load_image(retail)), ([], []))
        mod.cmd_expand(argparse.Namespace(dir=out, image=retail, out=whole))
        self.assertEqual((whole / "files" / "dir" / "a.bin").read_bytes(), b"MODS")
        self.assertTrue((whole / "mod.ini").exists())

        # A whole file replaces the patch of its path; one equal to the original removes it.
        (out / "files" / "dir" / "a.bin").write_bytes(b"NEW!")
        errors, _ = mod.check(out, mod.load_image(retail))
        self.assertTrue(any("change the same file" in e for e in errors))
        mod.cmd_patch(argparse.Namespace(dir=out, image=retail))
        self.assertEqual([p.name for p in (out / "files" / "dir").iterdir()], ["a.bin.bps"])
        mod.cmd_expand(argparse.Namespace(dir=out, image=retail, out=self.dir / "again"))
        self.assertEqual((self.dir / "again" / "files" / "dir" / "a.bin").read_bytes(), b"NEW!")
        (out / "files" / "dir" / "a.bin").write_bytes(mod.original(mod.load_image(retail), "files/dir/a.bin"))
        mod.cmd_patch(argparse.Namespace(dir=out, image=retail))
        self.assertEqual(list((out / "files" / "dir").iterdir()), [])

    def test_check_reports_unknown_paths_and_missing_ini(self):
        folder = self.dir / "mods" / "x"
        (folder / "files" / "dir").mkdir(parents=True)
        (folder / "files" / "dir" / "nope.bin").write_bytes(b"1")
        errors, warnings = mod.check(folder, mod.load_image(self.dir / "retail.nds"))
        self.assertEqual(errors, ["missing mod.ini"])
        self.assertTrue(any("nope.bin" in w for w in warnings))

    def test_code_change_finds_the_first_code_byte(self):
        ov = Overlay(id=1, ram_address=0x1000, ram_size=8, bss_size=0, sinit_start=0, sinit_end=0, file_id=0,
                     compressed_size=0, flags=0)
        retail = bytes(range(8))
        with mock.patch.object(mod, "code_ranges", return_value=[(0x1004, 0x1008)]):
            self.assertIsNone(mod.code_change(ov, retail, b"\xff\xff\xff\xff" + retail[4:]))  # data only
            self.assertEqual(mod.code_change(ov, retail, retail[:5] + b"\xff"), 0x1005)
            self.assertIsNone(mod.code_change(ov, retail, retail[:4]))  # shorter: the rest is the original

    def test_new_and_pack(self):
        folder = self.dir / "meu-mod"
        mod.cmd_new(self.args(dir=folder, name="Meu mod"))
        (folder / "files" / "dir").mkdir()
        (folder / "files" / "dir" / "a.bin").write_bytes(b"NEW!")
        zip_path = self.dir / "meu-mod.zip"
        mod.cmd_pack(argparse.Namespace(dir=folder, out=zip_path))
        with zipfile.ZipFile(zip_path) as z:
            self.assertIn("meu-mod/mod.ini", z.namelist())
            self.assertEqual(z.read("meu-mod/files/dir/a.bin"), b"NEW!")

    def test_check_reads_a_zip_with_the_mod_in_a_folder(self):
        folder = self.dir / "meu-mod"
        mod.cmd_new(self.args(dir=folder, name="Meu mod"))
        (folder / "files" / "dir").mkdir()
        (folder / "files" / "dir" / "a.bin").write_bytes(b"NEW!")
        zip_path = self.dir / "meu-mod.zip"
        mod.cmd_pack(argparse.Namespace(dir=folder, out=zip_path))
        code = mod.cmd_check(argparse.Namespace(dir=zip_path, image=self.dir / "retail.nds"))
        self.assertEqual(code, 0)


if __name__ == "__main__":
    unittest.main()
