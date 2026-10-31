# Translating the game into another language

*Em português: [`TRANSLATING.pt-BR.md`](TRANSLATING.pt-BR.md).*

A translation is a **translation mod**: a `translations/<language>/` folder with `.json` files (the text) and `.png`
files (the font's letters and the redrawn images). Translators edit only these files; `tools/translate.py` checks
them and makes the mod (the `.bps` patches, `mod.ini` and the `.zip`), on your PC. Players get the `.zip` and put it
in the `mods/` folder, like any mod ([`MODS.md`](MODS.md)).

The PT-BR translation (`translations/pt-BR/`, the CyberLifePlus mod) is made this way and serves as a model: it has
every text place already translated, and its status and rules are in
[`TRANSLATION-STATUS.md`](TRANSLATION-STATUS.md).

There are two ways to start a language:

| | On the Japanese game (default) | Duplicating PT-BR |
|---|---|---|
| Command | `translate.py new es --mod ...` | `translate.py new es --mod ... --duplicate pt-BR` |
| What the mod starts from | the original game | the CyberLife base, like PT-BR (below) |
| Starts with | no text; PT-BR's Latin letters over the game's fonts | a copy of all of PT-BR (text, images, fonts) to change bit by bit |
| What is not translated | stays in Japanese | stays in Portuguese |
| Limits | images only through what you translate (`images.json` or PNG) | those of PT-BR |

## What you need

- Python 3.12 or newer (standard library only).
- The game: `boej.pak` (the launcher creates it next to the game; in the repository, `build/boej.pak`) or the
  Japanese `.nds`. `translate.py` finds `boej.pak` by itself; for another path, `--image`.
- An image editor that saves PNG (for the new letters and the images).

## Step by step

```sh
# 1. start: the folder with language.json, PT-BR's Latin letters (font) and an empty texts/
python3 tools/translate.py new es --mod Traduccion-ES --name "Traducción al español" --author "Your name"
#    (or, to start from a copy of PT-BR: ... --duplicate pt-BR)

# 2. the language's letters: "charset" in language.json and their drawing in font/*.png (below)

# 3. what is left to translate, by group, with the Japanese and the PT-BR text next to it (in build/translate/es/todo/)
python3 tools/translate.py todo es --only 3201,item

# 4. copy the todo's .json files to translations/es/texts/, translate the texts and check
python3 tools/translate.py check es

# 5. make the mod, test it in the game and make the .zip to distribute
python3 tools/translate.py build es --install      # in mods/Traduccion-ES (turn it on in the launcher's Mods page)
python3 tools/translate.py build es --zip          # build/translate/Traduccion-ES-0.1.zip
```

`todo` without `--only` lists everything (more than 21 thousand places); `--japanese` adds what is still in
Japanese in every language. The files `todo` writes stay in `build/` because they have the game's Japanese text,
which **never goes into the repository** (`check` refuses a text with Japanese).

## The language folder

```
translations/es/
  language.json            the hub
  texts/*.json             the text, one file per group (3201.json, item.json, overlays.json...)
  font/FONT12.png + .json  the fonts (also FONT12T and FONT8)
  images.json              text written over the game's images (optional)
  images/<file>/<entry>.png   whole redrawn images (optional)
```

`language.json`:

```json
{
  "type": "translation",
  "language": "es",
  "mod": "Traduccion-ES",
  "name": "Traducción al español",
  "version": "0.1",
  "author": "Your name",
  "description": "A sentence about the translation.",
  "charset": {"á": "B2", "é": "BA", "í": "BE", "ó": "C4", "ú": "CA", "ñ": "B5", "Ñ": "D5", "¿": "A1", "¡": "A3"},
  "spacer": "DF",
  "space": "CF",
  "plain": {"…": "...", "“": "\"", "”": "\""},
  "texts": "texts/*.json",
  "images": "images.json",
  "pictures": "images",
  "fonts": "font"
}
```

| Field | What it is |
|---|---|
| `language` | the language code (the folder's name); shows in the launcher |
| `mod` | the name of the mod's folder in `mods/` (no comma) |
| `base` | the layer the mod is built on: without `base`, the original game; `CyberLife` only for PT-BR and its copies (below) |
| `name`, `version`, `author`, `description` | go into `mod.ini` (the launcher shows them). Raise `version` with each distributed version |
| `charset` | the language's letters that are not ASCII, each on a byte from `A1` to `DF` |
| `spacer`, `space` | reserved bytes (a spacer of no width and the 4-pixel space of the subtitles): leave `DF` and `CF` |
| `plain` | what to write in place of characters the font lacks (ellipsis, curly quotes) |
| `texts`, `images`, `pictures`, `fonts` | where the files are (the values above are the default) |

## Letters and font

The game writes Shift-JIS: ASCII takes 1 byte, and bytes `A1`–`DF` (half-width katakana in Japanese) are the only
free one-byte places. Each letter of the language that is not ASCII (accents, `ñ`, `ß`, `¿`) gets one of these
bytes in `charset` and a drawing in that place of the fonts. PT-BR uses `A2`, `AB`–`AD`, `B1`–`B4`, `B8`, `BA`,
`BB`, `BE`, `C4`–`C6`, `CA`, `CC`, `D0`–`D3`, `D7`, `D9`, `DA`; a letter both languages have can stay on the same
byte (`new` copies PT-BR's charset). `CF` and `DF` are reserved.

The game's font has no ASCII letters. That is why a language's font is a **layer** (`"layer": true` in the JSON):
only the Latin letters (ASCII, bytes `A1`–`DF`, punctuation and full-width letters and digits), which `build` draws
over the game's font, where the kana and kanji come from. `new` starts the layer with PT-BR's letters (drawn by
CyberLife); redraw any you like. Each font is a PNG sheet and a JSON:

- `FONT12` (regular text, 1 colour), `FONT12T` (menus, outlined: 3 shades of grey) and `FONT8` (small text).
- On the sheet, 16 letters per row, in the JSON's order, each in its cell of the red grid. Dark is ink; light or
  see-through is not. The bluish columns are outside the letter's width.
- In the JSON, one line per letter: `code` (the byte), `char` (the letter, just to read), `left` (empty columns on
  the left), `width` (the drawing's width) and `advance` (how far the text moves after it; the game adds 1 pixel).
- For a new letter: find its byte's line in the JSON, draw in the matching cell of the sheet (the position on the
  sheet is the order in the JSON) and set `left`, `width` and `advance`. `python3 tools/font.py check es` checks
  that every letter of `charset` has a drawing.

Alphabets that do not fit in 63 letters (Cyrillic, Greek) need two-byte letters: the format accepts new codes (one
more line in the JSON and one more cell on the sheet), but `charset` does not write them yet.

## Text

Each file in `texts/` is `{"place": "text"}`, like `{"evet:32010110:4": "Ah, Endo!\\nBom dia!"}`. The place says
where the text is in the game (file, entry, position); copy it from the `todo`, never make it up. Rules for any
language (details in [`TRANSLATION-STATUS.md`](TRANSLATION-STATUS.md), "Technical text rules"):

- `\n` breaks the line and `\f` starts another dialogue box; the box shows **3 lines** (the game wraps lines that
  are too wide by itself; a fourth one disappears).
- Each place has a room in bytes (the `todo` shows it): a `charset` letter takes 1 byte; Japanese, 2.
- `%s` (the player's name) must stay in the text.
- `""` skips a place on purpose (it stays as in the base).
- Script strings (place names, objectives, who speaks): may go beyond the Japanese room up to the size of the
  longest Japanese string of the same instruction (a place name, 23 bytes; an objective, 43); `build` grows the
  script's record.
- Players (`unitbase.json`): in `unitbase.dat` each one has the full name (`+0`, 27 bytes), the name in the lists
  (`+0x1C`, 15) and the reading (`+0x2C`, 31; in kana in the game); in `unitbase_npc.dat`, name, short name and
  reading (16, 16 and 32 bytes). The place is `unitbase.dat:<position of the field>`. The profile
  (`unitbase.STR:<position>`) goes in pairs, like the item descriptions: up to 127 bytes, about 60 letters in 2
  lines.
- Menus, item names and tables have a width in pixels; `check` measures with your font and says what overflows.

`check` gives, for each problem, the place, the reason and the start of the text. `build` writes nothing while there
are problems.

## Images

Drawn menus (buttons, banners, notices) are images. There are two ways to translate them:

- **Text over the image** (`images.json`): erases the Japanese text in a rectangle and writes yours with the mod's
  font. Format at the top of `tools/image.py`; PT-BR's `images.json` has 184 examples.
- **Whole image** (`images/`): `python3 tools/translate.py images es --only 'pic3d/option'` exports the images as
  PNG to `build/translate/es/images/`; redraw the ones you want and copy them to `translations/es/images/`, in the
  same folders. Use the colours the image already has (each image has a palette of 16 or 256 colours; a colour
  outside it becomes the nearest one, with a warning) and keep the size. The `images.json` of the same image is
  drawn on top.

## On the game or on the CyberLife base

**On the Japanese game** (`new`'s default), the mod starts from the original game: what you do not translate stays
in Japanese. `todo` uses PT-BR as the reference and finds each of its places in the game (CyberLife moved lines and
strings; `todo` converts the positions). Measured with the whole PT-BR text, player names included: all 21,426
lines fit. What PT-BR only has because of the base stays in Japanese until translated here: the images CyberLife
redrew (translate them through `images.json` or PNG) and the tables `text.py` does not read yet (battle route
titles, blog).

**Duplicating PT-BR** (`--duplicate pt-BR`), the mod is built on the **CyberLife base**
(`translations/bases/CyberLife/`, the CyberLife group's translation as it came out of their dump), like PT-BR. It
brings CyberLife's layout (moved lines, scripts with more room), the Latin fonts, the images and the player names.
Therefore:

- **what you do not translate shows as in the base, in Portuguese** (or in English, where CyberLife left it so);
  `todo` lists the texts still equal to PT-BR's;
- each place's room is that of CyberLife's layout; `todo` marks the lines that only exist in the base (`check`
  does not accept them as they are: shorten them);
- the base is CyberLife's work: credit them in `description` and in your mod's readme.

## Distributing

`build --zip` makes the `.zip` players put in `mods/` (or add through the launcher). It only has patches: no game
data goes with it. The launcher shows the mod as "Translation (es)". A file next to `mod.ini` (a `README.md`, for
example) can go along: with `--install`, whatever is already in `mods/<mod>/` besides `files/`, `overlays/` and
`mod.ini` stays.

## What is not possible yet

- **Videos** (`data_iz/movie/*.mods`): only the subtitles (`movie.json`). Replacing the video itself needs knowing
  the format (on the radar).
- **Text in the game's code (ARM9)**: 15 texts; a mod cannot change the ARM9.
- **Script strings longer than the longest Japanese of the same instruction**: the game may copy them into a
  fixed-size field, so `check` does not allow it.
- **Tables without a converter**: battle route titles (`BattleRouteTitle.dat`), blog.
- **Two-byte letters in `charset`** (large alphabets), above.
