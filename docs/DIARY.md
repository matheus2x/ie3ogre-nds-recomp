# Diary

The project's diary — every step, with what was tried, what was learned about the game and how each change was
verified — is written in Portuguese, in [`DIARY.pt-BR.md`](DIARY.pt-BR.md). It is a log, newest entry at the end,
and is not translated.

What it covers, roughly in order:

- extracting and identifying the dump, the ds-decomp function map and the static recompiler (`ntr-recomp`);
- the runtime: memory, CPU and interrupts, threads, the ARM7 and PXI in HLE, 2D and 3D graphics, sound, the
  card, the save and the RTC;
- the launcher, the release model (the game is compiled on the player's PC) and updates;
- mods: the file replacement, BPS patches, `.zip` mods, the launcher's Mods page and the text kept in saves;
- the PT-BR translation (CyberLifePlus): every version, what was found in the game's formats and how each text
  type is written; the translation tools (`tools/text.py`, `image.py`, `font.py`, `translate.py`);
- graphics: the SDL_GPU presentation, FXAA, FSR 1, the wide layout;
- savestates, rewind, achievements, the gallery, Discord Rich Presence.

For the current state of each area, the English documents are the place to start: [`DEVELOPMENT.md`](DEVELOPMENT.md),
[`MODS.md`](MODS.md), [`TRANSLATING.md`](TRANSLATING.md), [`TRANSLATION-STATUS.md`](TRANSLATION-STATUS.md),
[`GRAPHICS.md`](GRAPHICS.md) and [`REMASTER.md`](REMASTER.md).
