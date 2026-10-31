"""Tests for tools/translate.py and tools/language.py that need no ROM."""

import argparse
import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

import font as fonttool
import language
import translate

ROOT = Path(__file__).resolve().parents[2]
PT_BR = ROOT / "translations" / "pt-BR"


class TranslateTest(unittest.TestCase):
    def test_mod_ini_says_what_the_manifest_says(self):
        lang = language.load(PT_BR)
        ini = translate.mod_ini(lang)
        self.assertIn(f"name = {lang.info['name']}\n", ini)
        self.assertIn(f"version = {lang.info['version']}\n", ini)
        self.assertIn("game = BOEJ\ntype = translation\nlanguage = pt-BR\n", ini)
        self.assertIn("translations/pt-BR/language.json", ini.splitlines()[0])

    def new(self, tmp: str, **kw) -> language.Language:
        args = argparse.Namespace(language="es", mod="Traduccion-ES", name="Español", author="Yo", description=None,
                                  copy=str(PT_BR), duplicate=None)
        for k, v in kw.items():
            setattr(args, k, v)
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(translate.cmd_new(args), 0)
        return language.load(Path(tmp) / args.language)

    def test_new_language_is_on_the_game(self):
        with tempfile.TemporaryDirectory() as tmp, self.translations(tmp):
            lang = self.new(tmp)
            self.assertEqual((lang.code, lang.mod, lang.info["name"], lang.base), ("es", "Traduccion-ES", "Español", None))
            self.assertEqual(lang.charset, language.load(PT_BR).charset)
            self.assertEqual(lang.texts, [])
            for name, (j, _) in lang.fonts.items():  # the Latin letters only, over the game's font
                m = json.loads(j.read_text(encoding="utf-8"))
                self.assertTrue(m["layer"], name)
                codes = {int(c, 16) for g in m["glyphs"] for c in g["code"].split()}
                self.assertTrue(codes <= fonttool.LATIN and 0x41 in codes and 0xB2 in codes, name)
            with self.assertRaises(SystemExit):  # already there
                self.new(tmp)
            for bad in ("bases", "a/b"):
                with self.assertRaises(SystemExit):
                    self.new(tmp, language=bad)

    def test_duplicate(self):
        with tempfile.TemporaryDirectory() as tmp, self.translations(tmp):
            (Path(tmp) / language.BASES / "CyberLife" / "files").mkdir(parents=True)
            lang = self.new(tmp, duplicate=str(PT_BR))
            pt = language.load(PT_BR)
            self.assertEqual((lang.code, lang.mod, lang.info["version"], lang.base.name), ("es", "Traduccion-ES", "0.1", "CyberLife"))
            self.assertEqual([f.name for f in lang.texts], [f.name for f in pt.texts])
            self.assertEqual(set(lang.fonts), set(pt.fonts))

    @contextlib.contextmanager
    def translations(self, tmp: str):
        old = translate.TRANSLATIONS
        translate.TRANSLATIONS = Path(tmp)
        try:
            yield
        finally:
            translate.TRANSLATIONS = old

    def test_unknown_base(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp) / "es"
            d.mkdir()
            (d / language.MANIFEST).write_text('{"language": "es", "mod": "X", "base": "Nope"}', encoding="utf-8")
            with self.assertRaises(SystemExit):
                language.load(d)


if __name__ == "__main__":
    unittest.main()
