# Development

*Em português: [`DEVELOPMENT.pt-BR.md`](DEVELOPMENT.pt-BR.md).*

Details for whoever builds, tests or analyzes the game. The summary is in the [README](../README.md)
([in Portuguese](../README.pt-BR.md)); the mod format, in [`MODS.md`](MODS.md); the PT-BR translation, in
[`TRANSLATION-STATUS.md`](TRANSLATION-STATUS.md).

Requirements: Python 3.12+, CMake 3.28+, a C++20 compiler (clang, GCC or MSVC/clang-cl) and SDL3.

```sh
# identifies the dump by SHA-1, extracts it to build/extract/<variant>/
# and writes config/boej/layout/<variant>.json
python3 tools/extract.py "path/to/your/dump.nds"

# compares a modified dump with the retail one (code x data)
python3 tools/compare_dumps.py retail.nds other.nds --json build/diff.json

# tests (synthetic data, no ROM needed)
python3 -m unittest discover -s tools/tests -t tools
```

Dumps not in `dumps.json` are refused. For experiments, use `--allow-unknown`.

## Repository layout

| Folder | Contents |
|---|---|
| `recompiler/` | `ntr-recomp` (C++20): dsd manifest → C++ |
| `runtime/` | Runtime (C++20): memory, CPU, ARM interpreter, IRQ, timers, threads, PXI/ARM7 in HLE, VRAM, 2D, 3D, sound, SDL3 window |
| `game/` | The `boej` executable and the Windows icon (`icon.ico`/`icon.rc`, also used by the launcher) |
| `launcher/` | `ie3-launcher` (SDL3 + Dear ImGui): prepares, configures, updates and opens the game; the release's `README.txt`, `LEIA-ME.txt` and `TERCEIROS.txt` |
| `runtime/ui/` | `ntr-ui`: Dear ImGui screens used by the game (pause menu) and the launcher (settings panel) |
| `translations/` | Translations, one folder per language (`pt-BR/`: `language.json`, text, fonts and images, no game text), and the bases mods are built on (`bases/CyberLife/`); `tools/translate.py` makes the mod ([`TRANSLATING.md`](TRANSLATING.md)) |
| `tools/` | Python tools (`ntr/` reads the ROM; `extract.py` extracts; `compare_dumps.py` compares dumps; `analyze.py` runs dsd/Ghidra; `mod.py` makes, checks and turns mods into patches; `ntr/bps.py` makes and applies BPS patches; `shaders.py` compiles the shaders of `runtime/shaders/` to SPIR-V and DXIL in `runtime/src/shaders_embedded.h`; `text.py` measures, lists and applies the text translation (formats in `ntr/text.py`, LZ10 in `ntr/lz10.py`); `image.py` and `font.py` do the same with images and fonts (`ntr/image.py`, `ntr/font.py`, PNG in `ntr/png.py`); `translate.py` joins the three and makes a translation mod from `language.json` (`language.py`); `build.py` builds for development; `release.py` assembles the release; `ghidra/` has the Ghidra scripts; `tests/` has the tests) |
| `config/boej/` | `dumps.json` (known dumps), `layout/<variant>.json` (addresses, overlays, hashes), `dsd/` (ds-decomp's functions, sections and relocations), `names.tsv` (identified names, with evidence), `hle.tsv` (functions replaced on the host) and `code_blobs.tsv` (code dsd classifies as data). No game data |
| `mods/` | Official mods, one per folder, only `.bps` patches (no game data); each ships as a `.zip` in the release |
| `docs/` | Diary (`DIARY.pt-BR.md`, in Portuguese), mods (`MODS.md`), translation (`TRANSLATING.md`, `TRANSLATION-STATUS.md`), graphics (`GRAPHICS.md`, `REMASTER.md`), this document and `assets/` (the README logo); each document also in Portuguese (`*.pt-BR.md`) |
| `.github/workflows/` | Release CI (`release.yml`) |
| `build/` | Local output (extraction, generated code). Ignored by git |

## Static analysis

Requires [ds-decomp](https://github.com/AetiasHax/ds-decomp) v0.12.1 and Ghidra 12.1.4 with JDK 21. The paths come
from `DSD`, `GHIDRA_INSTALL_DIR` and `JAVA_HOME`; without these variables, the defaults are in `~/tools`.

```sh
python3 tools/analyze.py dsd retail.nds     # function map -> config/boej/dsd (only accepts the retail dump)
python3 tools/analyze.py names              # applies config/boej/names.tsv to the symbols
python3 tools/analyze.py ghidra             # headless project -> build/ghidra/boej.gpr
python3 tools/analyze.py decompile NitroMain ov132:0x0213acc0
python3 tools/analyze.py export ram itcm    # pseudo-C of whole blocks -> build/ghidra/export/
```

Contents of `build/extract/<variant>/`:

| Path | Contents |
|---|---|
| `arm9.bin` | ARM9 decompressed (BLZ), base 0x02000000 |
| `arm7.bin` | ARM7, base 0x02380000 |
| `autoload/01ff8000.bin`, `autoload/027e0000.bin` | ITCM and DTCM |
| `overlays/ovNNN.bin` | Decompressed overlays |
| `files/` | NitroFS (`data_iz/…`, `dwc/…`) |

## Recompile and run

SDL3 comes from the system (on Arch, the `sdl3` package; on Debian/Ubuntu, `libsdl3-dev` when available).

```sh
python3 tools/extract.py retail.nds          # once: extraction + build/boej.pak
python3 tools/analyze.py dsd retail.nds      # once (function map)
python3 tools/build.py                       # manifest -> ntr-recomp -> build/cmake-rel/game/boej (+ boej.pak next to it)
./build/cmake-rel/game/boej                  # no arguments: uses the boej.pak in the executable's folder
```

The path of a `boej.pak` or of a `.nds` can also be given as an argument.

**Savestates:** 5 slots, each in a file in `savestates/` in the game's folder (`boej-slot1.state` to
`boej-slot5.state`, ~6 MB each). **F5** saves in the chosen slot, **F9** loads, **F6**/**F7** choose the slot; the
pause menu has the **Savestates** page, with each slot's date. Loading does not touch the game's own save
(`saves/boej.sav`). A savestate also keeps the stacks of the recompiled code, so it only opens in the same
`boej.exe` that made it, and with the same mods: after an update (which recompiles the game) or a change of mods,
the old slots show as "from another version of the game or with other mods".

## Settings

The first time, the game creates `boej.ini` next to the executable, with the default values and comments. It is
read when the game opens; delete it to go back to the defaults.

| Section | Options |
|---|---|
| `[video]` | `wide_layout` (default `true`: a 16:9 window by the monitor's height, both screens on the left and the bottom one enlarged on the right; `false` = stacked, centred screens), `wide_side` (`left` or `right`: the side of the two small screens in the wide layout), `window_scale` (initial window size, only without `wide_layout`), `resolution_3d` (1 to 10), `fullscreen`, `filter` (`nearest`, `linear`, `sharp` = crisp pixels without uneven sizes, or `lcd` = `sharp` with the DS screen grid), `api` (`auto` = SDL's default renderer, Direct3D 11 on Windows and OpenGL on Linux; `d3d11` (Windows only) and `opengl` = SDL's renderer on that API; `vulkan` and `d3d12` (Windows only) = SDL_GPU, in beta; if the chosen one does not open, it falls back to the default; applies when the game opens), `integer_scale` (enlarge only by integer multiples, only without `wide_layout`), `post_aa` (`off` or `fxaa`: anti-aliasing of the 3D over the finished image, only with `vulkan` or `d3d12`; the HUD and text stay out through the 3D mask), `upscaler` (`off` or `fsr1`: AMD FSR 1 enlarges the 3D to the window size, instead of the filter; only with `vulkan` or `d3d12`). The plans for GPU, AA and upscalers are in [`GRAPHICS.md`](GRAPHICS.md), and those of the remaster mode (filters and texture packs) in [`REMASTER.md`](REMASTER.md) |
| `[audio]` | `volume` (0 to 100) |
| `[game]` | `fast_forward_speed`: fast-forward speed, 2 to 10 (times normal; default 4); `rewind`, `rewind_seconds`; `save`: the file in `saves/` the game loads and writes (default `boej.sav`; the launcher chooses it in "Play") |
| `[ui]` | `language`: language of the launcher and of the game's pause menu, `en` (default) or `pt`; the game's text does not change. The launcher switches it on the start screen ("Language / Idioma") or in Settings → Launcher. `achievement_popups` (default `true`): a notice in the corner of the screen when an achievement is unlocked. `discord_presence` (default `true`; Settings → Launcher, and Shortcuts in the pause menu): Discord Rich Presence (`runtime/src/discord.cpp`, local IPC with the Discord app, no SDK; only in player sessions; `NTR_DISCORD_APP_ID` changes the application ID) |
| `[mods]` | `enabled`: mods of `mods/` turned on (folder and `.zip` names), comma-separated; when two mods replace the same file, the later one wins (the launcher's Mods page takes care of this; see [`MODS.md`](MODS.md)) |
| `[keyboard]` | one line per DS button (`A`, `B`, ..., `UP`, `DOWN`) and per shortcut (`fast_forward`, `fast_forward_toggle`, `turbo`, `turbo_toggle`, `screenshot`, `debug_dump`, `fullscreen`, `save_state`, `load_state`, `state_slot_prev`, `state_slot_next`, `menu`, `rewind`, `pause`), with SDL key names separated by commas, for example `SELECT = Right Shift, Backspace`. Empty turns it off. |
| `[gamepad]` | the same, with buttons by position (`south`, `east`, `west`, `north`), `start`, `back`, `leftshoulder`, `dpup` etc., and axes with a direction (`lefty-` = analog stick up, `righttrigger`). By default, the left stick also moves the d-pad. |

Invalid values show in the session log (`logs/sessoes/`) with the line number, and the default is used instead.
Without a window (`NTR_HEADLESS`), the file is only read if `NTR_CONFIG` points at it, so tests do not depend on
the player's settings.

## Achievements

Local achievements, nothing online (`runtime/include/ntr/achievements.h`). The game checks the conditions in the
game's own save: once on opening and again every time the game saves (when writes to the save stop for 30
frames). Savestates and rewind do not touch the save, so they neither give nor take achievements. What was
unlocked shows in a notice in the corner of the screen, on a pause menu page and on the launcher's Achievements
page, which can also check an old or imported save ("Check this save").

| File | Contents |
|---|---|
| `achievements/achievements.json` | the definitions (they go in the release, next to the launcher and the game; `tools/build.py` copies them next to the executable) |
| `achievements/icons/*.png` | the icons (8 bits per channel PNG; 64×64) |
| `saves/<save>.achievements.json` | per save: every achievement, `unlocked`, `unlocked_at` (ISO 8601 with time zone, the PC's real time) and `icon`. The launcher renames it together with the `.sav`. An unlocked achievement never goes back, even if it disappears from the definitions |

A definition:

```json
{ "id": "first_save",
  "name": {"en": "Kickoff", "pt": "Pontapé inicial"},
  "description": {"en": "Save the game for the first time.", "pt": "Salve o jogo pela primeira vez."},
  "icon": "icons/first_save.png", "hidden": false,
  "condition": {"source": "save", "field": "saved"} }
```

The condition is a test on the save (`"source": "save"`, the only source for now) or `{"all": [...]}` /
`{"any": [...]}` of other conditions. Fields: `saved` (the save has a saved game), `chapter`, `level` (of the
team), `members`, `play_hours` and `has_unit` (`value` = unit number in the roster), with `op` (`>=` if omitted,
`>`, `==`, `!=`, `<=`, `<`) and `value`. A definition with an error is left out and shows in the log
(`[achievements]`). `hidden: true` shows "???" until unlocked. The fields come from `ntr/savedata.h` (the save
reader, the same as the Saves page); conditions on the live state (RAM) need a new source in `GameFacts`.

The current list (34): first save; chapters 2 to 11 (the game has 11, `EVENTINFO.TXT`); team level 10, 25, 50, 75
and 99; 16, 30, 50 and 100 friends; the captain of the teams faced in the story (`has_unit`; Big Waves and Desert
Lion were left out) and "All Stars", with the captains of the seven national teams of the world tournament. The
captains of Team K, Team Garshield, Tenkuu no Shito, Makai Gundan Z (the same Desta as Dark Angel) and The Ogre stay
hidden until unlocked, so as not to spoil the story. The icons are PNGs in `achievements/icons/`: the captains'
are character portraits, placed by hand; the others (chapters, level, friends, All Stars) are drawn by
`tools/achievement_icons.py` (ImageMagick), which only makes the missing ones and overwrites existing ones with
`--force`.

Where the numbers come from: `logic/team.pkb` has a team every 0x160 bytes (name at +0x00, captain at +0x2E, 16
players of 8 bytes from +0x40, unit number in the u16 of each); in `logic/unitbase.dat` (records of 0x68 bytes, name
at +0x00) the unit number is at +0x4E, not the record's position (the world tournament's players go above 3000).
The story's teams come from the match texts (`script/mcht`, keys 3401xxxx).

## Diagnostic files

Each time the game opens is a session, named by date and time (`2026-10-27_14-03-12`). The game writes, in the
`logs/` folder next to the executable:

| File | Contents |
|---|---|
| `logs/sessoes/<session>.log` | everything that shows in the terminal, with the frame number and the time on each line, plus the files the game opens. `[sessao]` lines mark what the player did: game saves, savestates, pause menu, known screens (table in `runtime/src/cpu.cpp`). Crashes and hangs (no frame for 5 s) go into the log with the machine state, and each second with frames above 16.7 ms (FPS drops, stuttering sound) makes a `[perf]` line. |
| `logs/replays/<session>.txt` + `.sav` | the whole session (buttons and touch per frame, the clock's start time and the save it started from). With them the session repeats exactly: `NTR_REPLAY=logs/replays/<session>.txt NTR_SAVE=<copy of the .sav>`. |
| `logs/historico.txt` | one line when each player session starts (version, mods, save) and another when it ends (duration, frames, reason, saves, savestates, FPS drops, hangs, last screen). A start without an end = the game was killed. Never deleted. |
| `logs/launcher.log` | the launcher's log (the previous one in `launcher.old.log`). |

The 20 most recent sessions are kept. Scripted or windowless runs (`NTR_INPUT`, `NTR_REPLAY`, `NTR_HEADLESS`) do not
go into the history; with `NTR_LOG_FILE` the log goes to the given path and the `logs/` folder is not used at all.
If something goes wrong, press **F8** at the moment (writes the state to the log and a screenshot), close the game
and send these files.

## Windows (cross-compiling from WSL/Linux)

Requires MinGW-w64 (`pacman -S mingw-w64-gcc` on Arch). `ntr-recomp` is still built and run on Linux; only the
game is built for Windows. CMake downloads the official `SDL3-devel-*-mingw` package. The result is `boej.exe`
(static libstdc++) plus `SDL3.dll`, which must be in the same folder. `--dist` copies both and `boej.pak`; the pack
is only copied again when it changes.

```sh
python3 tools/build.py --windows                       # build/cmake-win/game/boej.exe
python3 tools/build.py --windows --dist /mnt/c/Games/ie3  # and copies to C:\Games\ie3
```

Opened from Explorer, `boej.exe` hides the console window (everything goes to the session log in
`logs/sessoes/`); opened from a terminal, the output stays there, and `NTR_CONSOLE=1` keeps the console.

## Release (launcher + SDK)

```sh
python3 tools/release.py                    # build/release/ie3ogre-recomp-<VERSION>-windows.zip, <mod>.zip, sha256sums.txt
python3 tools/release.py --platform linux   # the same for Linux (system compiler and SDL3)
```

It needs no dump. The package carries the launcher, `SDL3.dll` and `sdk/`:
- `ntr-recomp` and `dsd` v0.12.1 (which, on the player's PC, extracts the code from the dump and decrypts ov129's
  DS Protect);
- the function manifest with `{modules}/...` paths (`analyze.py manifest --template`);
- the sources of the runtime, of `game/main.cpp`, of the pause menu (`runtime/ui`) and of Dear ImGui;
- SDL3's headers and import library;
- `toolchain.txt`, with llvm-mingw pinned by URL and SHA-256.

Each official mod (`mods/<name>/` with `mod.ini`) becomes `build/release/mod-<name>-<version>.zip` (`version` from
`mod.ini`) and goes into `sha256sums.txt`. On the release page, the `.zip` shows with that name, without a label
(`build/release/assets.txt`, read by the CI), and the notes list the official mods with their version. The release
is refused if an official mod has a whole game file instead of a `.bps` patch, or if it changed since the previous
tag and the `version` in `mod.ini` did not.

The launcher and `ntr-recomp` are built with the same llvm-mingw (`cmake/toolchains/llvm-mingw-x86_64.cmake`),
downloaded once to `build/tools/`. The CI (`.github/workflows/release.yml`) runs this on receiving a `vX.Y.Z` tag,
which must match the `VERSION` file, and publishes the zips and `sha256sums.txt` in the release. The release text
comes from the commit messages since the previous tag (`tools/changelog.py`; each commit's title as a section, the
body as bullets), followed by how to install and verify the download. To see it before creating the tag:
`python3 tools/changelog.py HEAD`.

On the command line the launcher accepts `--prepare [dump.nds]` (prepares without a window), `--update` (looks
for, downloads and installs the new version), `--install-mod <mod.zip or folder>` (copies into `mods/`) and
`--mod-from-dump hack.nds`. For tests: `IE3_UPDATE_API` changes the releases API URL (accepts `file://`),
`IE3_LAUNCHER_TAB=0..6` opens a page (0 = start screen, 1 Settings, 2 Mods, 3 Load game, 4 Updates, 5 Achievements,
6 Gallery; `IE3_LAUNCHER_VIEW=1` opens the newest screenshot), `IE3_LAUNCHER_PLAY=1` opens the "Play" menu,
`IE3_LAUNCHER_NEW_SAVE=<name>` opens the "New game" box with that name, `IE3_LAUNCHER_MOD=<folder>` selects a mod on
the Mods page and `IE3_LAUNCHER_SHOT=file.bmp` saves the window and closes. The game accepts `--check-mods` (the
mod check the Mods page shows).

`tools/build.py --trace` makes a build with function tracing in `build/cmake`. On Ctrl+C, it prints the last 256
functions called.

Environment variables:

- **Save:** `NTR_SAVE` (default: `saves/<[game] save>` in the executable's folder, `saves/boej.sav` without the key;
  a save that only exists in `saves/` of the current folder, where earlier versions wrote, is copied the first
  time).
- **Settings:** `NTR_CONFIG` (another file instead of `boej.ini`). The variables below take priority over it.
- **Mods:** `NTR_MODS=folder1,mod2.zip` turns these mods on instead of `[mods] enabled` (useful without a window,
  when `boej.ini` is not read).
- **Window and pace:** `NTR_SCALE` (initial scale; default from `boej.ini`, 2); `NTR_FAST` (no 60 Hz limit);
  `NTR_HEADLESS` (no window and no limit).
- **Graphics API:** `NTR_GPU_API=auto|d3d11|opengl|vulkan|d3d12` takes priority over `api` in `boej.ini`. The log
  says the API in use (`[host] renderer: ...`) and, if the chosen one does not open, why.
- **3D resolution:** `NTR_3D_SCALE=1..10` (default: `resolution_3d` from `boej.ini` with a window, 2; 1 without a
  window). Only the 3D gets resolution, and the 2D layers are stretched. The game and the DS screen capture keep
  seeing the native image. Captures (F12, F8, `NTR_SCREENSHOT_FRAMES`) come out at the size shown in the window;
  F12's goes to `screenshots/` (next to the executable) as PNG, the others are BMP. In the 16:9 layout F12 also
  writes `-ampliada` (the enlarged bottom screen) and `-completa` (the whole layout), in the layout's format
  (384x216 times the scale, at least 1920x1080 for the whole one). The 3D uses up to 16 threads
  (`NTR_3D_THREADS=1` turns that off), and the image is the same with any number.
- **Audio:** `NTR_MUTE` (no audio device); `NTR_WAV=file.wav` records the sound output (also works without a
  window).
- **Clock:** the RTC uses the PC's local time; `NTR_RTC="2010-07-01 12:00:00"` starts from a fixed moment and
  advances with emulated time (reproducible tests).
- **Automatic tests:**
  - `NTR_INPUT` scripts the input per frame, for example `300-310:TOUCH@128,96;900-905:A+START`;
  - `NTR_SCREENSHOT_FRAMES` (comma-separated list) with `NTR_SCREENSHOT` (path; `%d` becomes the frame number)
    saves frames as BMP;
  - `NTR_EXIT_FRAME` quits after N frames;
  - `NTR_WINDOW_SHOT=frame:path` saves the whole window at that frame as BMP, with what is drawn over the game
    (the achievement notice, for example);
  - `NTR_STATE_SAVE=N` / `NTR_STATE_LOAD=N` save to / load from savestate slot 1 at frame N.
- **Debugging:**
  - `NTR_LOG_IRQ`, `NTR_LOG_THREAD`, `NTR_LOG_CARD`, `NTR_DUMP_SND` (each SND command), `NTR_LOG_TP`, `NTR_LOG_3D`
    (polygons and GX commands every 60 buffer swaps);
  - `NTR_PEEK=address:size` prints game memory every frame;
  - `NTR_PROFILE=start-end` (only in the `--trace` build) counts the functions called in those frames.
