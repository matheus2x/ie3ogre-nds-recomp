Inazuma Eleven 3 - Sekai heno Chousen!! The Ogre (native port by recompilation)
================================================================================

This package does NOT contain the game: no data and no code. The launcher
prepares everything on your computer from a dump of YOUR cartridge.
(Em português: LEIA-ME.txt.)

LANGUAGE
The launcher and the pause menu are in English. For Brazilian Portuguese,
pick "Português (Brasil)" under "Language / Idioma", in the corner of the
home screen, or under Settings > Launcher. The game itself is the Japanese
version; the CyberLifePlus mod translates it to Brazilian Portuguese.

FIRST TIME
1. Extract this folder somewhere you can write to (Documents, for example;
   avoid "Program Files").
2. Open ie3-launcher.exe and pick the dump (.nds, code BOEJ, unmodified),
   or drag the file onto the launcher's window.
3. The launcher:
   - checks the dump's SHA-1;
   - creates boej.pak (the game's data, ~450 MB) in this folder;
   - downloads the llvm-mingw compiler (~190 MB, only the first time);
   - recompiles the game's code and compiles boej.exe (a few minutes,
     depending on the processor).
   After that, the dump is no longer needed. Just click "Play".

UPDATES
The Updates page downloads the new version and checks it against the
release's sha256sums.txt. The game is compiled again automatically, without
asking for the dump. Your saves and settings are not touched.

MODS
Each mod is a folder or a .zip. To add one, put it in the "mods" folder,
next to boej.exe (or drag it onto the launcher's window). Then, under Mods,
turn mods on and off, order and remove them. Add only copies the mod
to the "mods" folder, and Remove only deletes that copy: the original file
or folder is not changed. The page also creates a mod from a modified dump
(a translation, for example), as long as its code is the original game's.
Changes apply the next time the game starts. How to make a mod: see
docs/MODS.md in the project's repository (address at the end of this file).
Official mods ship as separate .zip files in the releases (for example
mod-CyberLifePlus-<version>.zip, the PT-BR translation): put the .zip in
the "mods" folder without extracting it. To update a mod, add the new .zip
under Mods: it takes the old one's place and stays on.

SAVES
Saves live in the "saves" folder, next to boej.exe, one .sav file per save
(64 KB, the same format as the cartridge's save). On the launcher's Saves
page, click the save the game will use; the page shows each one's player
name, chapter, team level, members, play time and team faces. "New save"
adds a blank save with the name you type: click it and start a New Game;
the other saves do not change. To import a save (dumped from the cartridge
or from another emulator), put the .sav in the "saves" folder or drag it
onto the launcher's window (it is copied; the original is not touched).
Files are always named <name>-<number>.sav, with the name in lowercase and
only letters and numbers: "Second Run" becomes secondrun-1.sav, and a save
imported as My Save.sav becomes mysave-1.sav (with the game open, this
waits for it to close). The game writes over the chosen save: make a copy
before using an important one.

ACHIEVEMENTS
Each save has its own achievements, on your PC only (nothing online). The
game checks them when it starts and every time you save; a new one shows
up in the corner of the screen (turn that off in boej.ini with
[ui] achievement_popups = false). The list is in the pause menu, under
"Achievements", and in the launcher's "Achievements" page, which can also
check an older or imported save ("Check this save"). They are kept in
saves\<save>.achievements.json, next to the .sav: each achievement,
whether it was unlocked, when, and its icon.

FILES CREATED IN THIS FOLDER
- boej.exe, boej.pak  the game, prepared from your dump (personal use only:
                      do not distribute)
- cache\, toolchain\  code extracted from the dump, and the compiler
- saves\              the saves (boej.sav at first; the launcher names each
                      one after its player, and the Saves page picks the one
                      the game uses) and each one's achievements
                      (.achievements.json)
- savestates\         the savestates (slots 1 to 5)
- boej.ini            settings (also editable in the launcher)
- logs\               records for reporting problems: launcher.log,
                      historico.txt (every game session), sessoes\ and
                      replays\ (log and recording of each of the last 20)
- mods\               the mods (one folder or .zip per mod)

DEFAULT CONTROLS (change them in the game's menu or the launcher's Settings)
  D-pad       arrow keys (controller: D-pad or left stick)
  A B X Y     X Z S A
  L R         Q W
  Start       Enter        Select   Right Shift or Backspace
  Touch       left mouse button on the bottom screen
  Esc         pauses and opens the menu (settings apply at once, reset
              the game, quit)
  F11 fullscreen, F12 screenshot
  F5 saves a savestate, F9 loads it, F6/F7 change the slot (1 to 5); also
      in the pause menu, under "Savestates". They only work in this build
      of the game: an update recompiles it and old savestates do not load.
  Tab (hold) or F2: fast-forward     C (hold) or F3: auto-A
  R (hold): rewind, up to 30 seconds by default; turn it on or off and
      change the time in the menu, under "Shortcuts"
  F8: if something goes wrong, press it right away and send, from the logs
      folder, the session's .log (sessoes\) and the .txt and .sav with the
      same name (replays\)

PROJECT
Source code, documentation, issues and releases:
  https://github.com/matheus2x/ie3ogre-nds-recomp
The port's code is under the MIT License (licenses/ie3ogre-recomp.txt).
Third-party components: TERCEIROS.txt. Inazuma Eleven belongs to LEVEL-5
Inc.; this is a fan project with no affiliation to LEVEL-5 or Nintendo.
