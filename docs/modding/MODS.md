# Mods

*Em português: [`MODS.pt-BR.md`](MODS.pt-BR.md).*

Each mod is **a folder or a `.zip`** inside the `mods/` folder, next to `boej.exe` and the launcher. To add a mod,
just put it there: the launcher shows that folder's mods on the **Mods** page, where each one is turned on, off
and ordered. The game reads the mod's files instead of the original ones, straight from the folder or from inside
the `.zip` (without extracting it); `boej.pak` is never changed.

## What a mod can change

| Can | How |
|---|---|
| Any NitroFS file (`data_iz/...`): text, fonts, images, models, sounds, videos, tables | `files/<path in the game>`, the whole file; its size may change |
| Overlay data (text and tables kept inside the code, like the menus) | `overlays/ovNNN.bin`, the overlay as it sits in RAM (decompressed) |
| The same, only the difference | `files/<path>.bps` or `overlays/ovNNN.bin.bps`: a BPS patch over the original file (see **Patches** below) |

| Cannot (for now) | Why |
|---|---|
| The game's code (ARM9, overlay code) | the code was recompiled for the PC; changing its bytes would have no effect. The game checks this and refuses the mod |
| Overlay 129 | it is the copy protection, encrypted in the ROM |
| The ARM7, the banner and the header | the ARM7 is reimplemented on the PC; the others are not read by the game |

## Layout

```
boej.exe
ie3-launcher.exe
mods/
  bigger-fonts.zip                       a mod as a .zip (same content as the folder below)
  my-translation/                        a mod as a folder
    mod.ini
    files/
      data_iz/logic/CapsuleComment.dat   replaces /data_iz/logic/CapsuleComment.dat
      data_iz/font/FONT12.NFTR.bps       patch over the original /data_iz/font/FONT12.NFTR
    overlays/
      ov022.bin                          the whole overlay 22, decompressed
      ov023.bin.bps                      patch over the decompressed overlay 23
```

`mod.ini`:

```ini
[mod]
name = My translation
version = 1.0
author = Your name
description = What the mod does, in a sentence or two.
game = BOEJ
```

Only `game` is checked (if present, it must be `BOEJ`); the rest shows in the launcher. A translation mod made by
`tools/translate.py` also has `type = translation` and `language = <language>`, and the launcher shows it as
"Translation (<language>)" ([`TRANSLATING.md`](TRANSLATING.md)). The name of the folder or of the `.zip`
identifies the mod and cannot have a comma. Files next to `mod.ini` (a readme, a picture) are ignored.

**A mod as a `.zip`.** The zip has the same content as the folder: `mod.ini` at the root of the zip or inside a
single folder (what `tools/mod.py pack` makes and what Windows does when it "Compresses" a folder). "Deflate"
compression or none; password-protected zips and zip64 (above 4 GB) are not accepted. Compressed files are
decompressed in memory as soon as the game opens, in the background (the fan translation, 77 MB, takes less than
1 s); a folder mod is read straight from disk. To make and test a mod, the folder is handier; to distribute it,
the `.zip`.

**Order.** The mods turned on are in `boej.ini`, in the order of the launcher's list:

```ini
[mods]
enabled = my-translation, bigger-fonts.zip
```

When two mods replace the same file (or the same overlay), the later one wins, and the launcher shows a warning on
the other. For tests without touching `boej.ini`, the variable `NTR_MODS=folder1,mod2.zip` takes priority.

**Overlays.** The file is the whole overlay, as the game puts it in RAM. Only the bytes outside the code may
change: the game compares the mod with the original overlay in the ranges the recompiled code depends on
(function bodies, literal pools and jump tables, recorded by `ntr-recomp` in `registry.cpp`) and refuses the mod
if any byte there changed, saying the address. An overlay smaller than the original is completed with the
original bytes; a larger one grows the overlay's space (up to 64 KiB more).

**Patches.** A `<path>.bps` file in place of the whole file is a [BPS](https://www.romhacking.net/documents/746/)
patch (the format of beat and Floating IPS) applied over the game's original file; for an overlay, over it
decompressed. The mod carries only what changed (translated text, edited pixels), not the game's data: it is the
format for distributing a mod. The patch checks the CRC-32 of the original, of the result and of itself; a patch
made for another file, or damaged, gives an error and leaves the mod out. The file's size may change.

In one mod, a path has either the whole file or the patch, not both. The patch is always over the game's
original, never over another mod's file (the last mod in the list that replaces the file wins, as always).

The patches are applied when the game opens, in the background, like the compressed files of a `.zip`. The whole
translation (512 files, 9 MB of patches for 76 MB of files) costs about 0.1 s more at start.

**A mod with an error** is left out entirely (the game opens without it). Files with a path the game does not have
are ignored, with a warning. Errors and warnings show on the Mods page and in the session log in `logs/sessoes/`
(`[mods]` lines).

**Savestates** keep which mods were in use: a slot saved with other mods (or with another version of one of the
mod's files) does not open, because the game had already read the old files.

## Tools

### Launcher (Mods page)

- The list shows what is in the `mods/` folder and updates by itself when something is put there or taken out.
- **Add mod (.zip):** copies the `.zip` into the `mods/` folder (the same as copying it by hand). A `.zip` or a mod
  folder can also be dragged onto the window. A mod with the same name is replaced.
- **Make from a modified dump:** choose a `.nds` of a translation or hack. The launcher compares it with the
  original game (`boej.pak`) and makes a mod with the files and overlays that changed. It only works if the dump's
  code is the retail one (the case of translations that only change text, fonts and images).
- **Remove** (on the mod's row or in the details, with confirmation) deletes the folder or the `.zip` inside
  `mods/`.
- Adding and removing only touch the copy in `mods/`: the `.zip` or folder the mod was added from is never changed
  or deleted. The page and the remove confirmation say so.
- Turn on/off and reorder with the arrows.

From the command line: `ie3-launcher --install-mod mod.zip` and `ie3-launcher --mod-from-dump hack.nds`. The game
itself does the check with `boej --check-mods` (one line per mod and per problem).

### `tools/mod.py` (Python 3.12+)

```sh
# empty folder with the mod.ini
python3 tools/mod.py new mods/my-translation --name "My translation" --author "Me"

# original files to edit (the retail can be boej.pak or the .nds)
python3 tools/mod.py unpack boej.pak build/original --only 'data_iz/logic/*'
python3 tools/mod.py unpack boej.pak build/original --overlays

# mod from a modified dump (the same as the launcher's button); --patch: only .bps patches
python3 tools/mod.py from-dump boej.pak hack.nds mods/hack --name "Hack" --patch

# the mod's whole files become patches (a file equal to the original goes, with its patch)
python3 tools/mod.py patch mods/hack boej.pak

# copy of the mod with the patches applied (whole files), to edit
python3 tools/mod.py expand mods/hack boej.pak build/hack

# looks for problems (paths that do not exist, overlays with changed code); folder or .zip
python3 tools/mod.py check mods/my-translation boej.pak

# .zip to distribute (just put it in the mods/ folder)
python3 tools/mod.py pack mods/my-translation
```

The Python `check` uses ds-decomp's function sizes (`config/boej/dsd`); the check that counts is the game's,
which uses exactly what the recompiler used.

### Finding which file to change

The session log (`logs/sessoes/<date and time>.log`) records every file the game opens, with the frame number and
the time:

```
  12345 14:03:12| [fs] open /data_iz/logic/CapsuleComment.dat
```

Play up to the scene you care about, press **F8** (marks the moment in the log) and look at the last files opened.

## Workflow

1. `tools/mod.py unpack` to get the originals; edit them with the format's tools.
2. Put only the changed files in `mods/<your-mod>/files/` (same path as in the game).
3. Turn the mod on in the launcher and open the game. Nothing needs recompiling: the mod is read when the game
   opens.
4. Check the Mods page (or the session log) if something does not show.
5. `tools/mod.py patch` to turn the whole files into patches and `tools/mod.py pack` (or "Compress" on Windows) to
   make the `.zip` players put in the `mods/` folder.

**Distribution.** A mod with whole files carries game data (the modified original file); with patches, only what
changed. Distribute mods as patches. The launcher's "Make from a modified dump" makes whole files, for use on
your own PC.

## Official mods

The repository's [`mods/`](../../mods) folder has the mods the project maintains, one per folder, patches only. Each
one ships in the release as `mod-<name>-<version>.zip` (`tools/release.py`). The file name changes with each
version, but the launcher recognizes the same mod by the `name` in `mod.ini`: added through the launcher, the new
one deletes the old copy from the `mods` folder and takes its place in the list (on, if it was); put there by
hand, a newer version takes the old one's place, which stays in the folder, off. The version is the `version` in
`mod.ini`: it shows in the `.zip` name, in the "Official mods" section of the notes (with "updated in this
version" when the mod changed) and, in the launcher, in the list and the details of the Mods page. The release is
refused if an official mod has a whole game file or changed since the previous release without a new `version`.

| Mod | What it is |
|---|---|
| [`CyberLifePlus`](../../mods/CyberLifePlus/README.md) | PT-BR translation, originally made by the CyberLife group and continued here; made from `translations/pt-BR/` by `tools/translate.py`. Its `README.md` has the credits and how to continue the translation |

## Planned kinds of mod

What file replacement can already do, and what is still missing in the port for each kind:

| Kind | Today | Missing |
|---|---|---|
| **Translation** | yes, as its own kind: a `translations/<language>/` folder with only `.json` (text) and `.png` (fonts and images), and `tools/translate.py` makes the mod (patches, `mod.ini`, `.zip`) from the original game, or from the CyberLife base for PT-BR and its copies ([`TRANSLATING.md`](TRANSLATING.md)). A ready-made translation as a `.nds` becomes a mod through the "Make from a modified dump" button (or `tools/mod.py from-dump --patch`) and runs the same as the translated dump | videos (`.mods`; today only the subtitles), tables without a converter (`BattleRouteTitle.dat`, blog), two-byte letters in the charset |
| **Sprite and image replacement** | yes, by replacing the file (`data_iz/pic2d`, `obj2d`, `spr`, `face2d`, `pic3d`) in the game's format | document the formats and have PNG converters |
| **High-resolution textures (upscaling)** | no: a game file can only have the size that fits in the DS's VRAM | texture packs by hash (`textures/` in the mod), applied by the 3D renderer at the internal resolution and by the 2D composition (plan in [`REMASTER.md`](../graphics/REMASTER.md)) |
| **Technique manager (Mini Hack style)** | partly: the player and technique tables are files and can be replaced whole | map the tables' format and make an editor that produces the mod folder |

Next infrastructure steps, in order:

1. ~~Per-file patches~~ (done: `.bps`, see **Patches** in Layout).
2. **Documented formats** and converters in `tools/`: text, font and image done (`text.py`, `font.py`, `image.py`,
   together in `translate.py`); some tables (`BattleRouteTitle.dat`, blog, techniques) and video are missing.
3. **Texture packs** for upscaling.
4. **Technique editor** producing a mod.
5. Mods that change behaviour (C++ hooks called by the recompiled code), in the long run.

## How it works inside

`runtime/src/mods.cpp`: on opening, the game reads the mods turned on, resolves each path through the NitroFS FNT
and places the mod's files at ROM addresses after the end of the image. A copy of the FAT (and, for overlays, of
the overlay table, with the overlay marked as not compressed) points the game at them (`rom_override` in
`rom.cpp`). The game reads the mod's files through the usual paths, without knowing they came from outside.
Folders and zips go through `ModPackage` (`runtime/src/package.cpp`, with its own zip reader and inflate, checked
by CRC-32). BPS patches are applied by `bps_apply` (same file) over the original bytes of `boej.pak`;
`tools/ntr/bps.py` makes and applies the same patches in Python. The files of a folder and the ones stored
uncompressed in a zip are memory-mapped; compressed ones are decompressed by a background thread, or on the spot if
the game asks for one before it.

**Text kept in the save.** The save keeps the place, the label ("もくてき") and the objective of the top strip as
ready text, not as an index. That is why a save made with a translation shows "????" without it (the original font
does not have those letters), and a save from before it shows Japanese. This text comes from the scripts'
instructions (`data_iz/script/eve.pkb`: place in opcode 0x4037, label in 0x402F, objective in 0x201C and 0x201D). On
opening, a background thread reads the game's scripts and those of every mod in the `mods/` folder, on or not, and
builds a table that takes the text of each version to the text of the same instruction in the scripts in use. When
the top strip is about to be drawn (a hook on `Font_DrawString`, `hle.cpp`), a text that is not from the scripts in
use is replaced by the right one, in the drawn copy and in the structure at `0x020AA044` that goes to the save. The
load screen's card reads the save directly and only changes after saving again. A save made with a mod that is no
longer in the folder is not recognized.
