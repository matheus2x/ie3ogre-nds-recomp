# CyberLifePlus — PT-BR translation

*Em português: [`README.pt-BR.md`](README.pt-BR.md).*

Translation of Inazuma Eleven 3 - Sekai e no Chousen!! The Ogre into Brazilian Portuguese, as a mod for
ie3ogre-recomp.

## Credits

- **Original translation: the CyberLife group.** All of the translation work up to version 1.0 of this mod is
  theirs: translated text, fonts and images. The translation has had no updates for months, and this mod continues
  from where it stopped.
- **Continuation:** matheus2x, completing what was left untranslated.

If you are from CyberLife and want something in these credits changed (names, links, how the translation is cited),
open an issue in the repository.

## How to use

Put the release's `mod-CyberLifePlus-<version>.zip` in the `mods/` folder, next to the game, and turn the mod on in
the launcher's **Mods** tab. To update, add the new `.zip` through the launcher: it deletes the old version and
leaves the new one on in its place.

The mod only carries **patches** (`.bps`, only the difference from the original game): it contains no game data and
needs the retail dump, the same one the launcher asks for.

## Origin

Version 1.0 was made with `tools/mod.py from-dump --patch` from the dump with CyberLife's translation
(SHA-1 `a70e9a336f6ea8ff78003938dcd024533f034a68`) over the retail one
(SHA-1 `9380f1d7ee75e7c6ef24f1b76c88ecdc85088957`): 512 files and 32 overlays. Applied to the retail game, the mod
gives exactly the files of the translated dump.

## Versions

- **1.16:** the mod is now made from the text in `translations/pt-BR/` (in the repository), with
  `tools/translate.py`. In the game, the text is the same as in 1.15.
- **1.15:** the opening: the lyrics of the intro video's song in Portuguese (before in Japanese, with the kanji
  showing as "?").
- **1.14:** the training messages with the stats, which were in English and disappeared (as abbreviations: CHU,
  FIS, CON, DEF, VEL, RES, COR, because the room only takes 3 letters), "Exp. subiu!" and the match periods (1T,
  2T, 1P, 2P).
- **1.13:** the tournaments (rounds, exit and save windows, elimination, prizes) and the cup names, now "Copa Raimon
  Town", "Copa Fire Tornado"...; the records (menu → System → Records); and the messages that were in English in
  the formation, the equipment shop, the technique teaching and the end of the match.
- **1.12:** the messages of the chests and doors, which were in English or half done: what the chest gives ("Você
  ganhou!"), empty chest, locked door, "Parece que uma porta se abriu.", the loss of grit and friendship points and
  the prize for the complete binder.
- **1.11:** the scout, the binder and the contact map, which were in English: the player's sheet in the binder (how
  to recruit, where and the colour of the machine's coin; "Recrute para ver os dados!"), the scout search (By
  condition, By name, positions and attributes), the result (Hire, Search another, Same/Change condition) and the
  contact map messages (Recruitable, Already in the team, Unlocked!...).
- **1.10:** the text menus that were in English: the field menu (Backpack, Formation with Story, Battle and Network
  1–3, Data with Binder, Phone, Uniforms and Results, System with Records, Help and Options), the top screen card
  (Team level, Friends, Grit, Friendship), the Friends Y menu (Sort, Shirt, Dismiss, Equip, Techniques, Items) and
  sorting by Name, Nickname, Level and Attributes, the match commands (Defence, Let's go!, Ace, Tactics, Formation),
  the backpack and learning techniques, with their windows ("Precisamos de Endo!", which was "power ofEndo is
  ok.").
- **1.9:** the team menus, which were in English: Friends (the list, "Order" with the stats — Kick, Body, Control,
  Guard, Speed, Stamina, Guts —, Data, Swap, Menu, "Shirt number"), Equipment (Boots, Accessory, Gloves, Change)
  and Formation (Build team - Story / Network 1–3 -, Copy, Open), with the buttons End, Exit, Stop, OK, Yes and No.
  Translated from the Japanese; CyberLife had put "Unsort" on the button that goes back to the team's order and
  "Club" on the Friends list.
- **1.8:** the communication screens that were still in English: the match screen (Find a rival, Join, Build team,
  Name and emblem, Change uniform, CPU and pitch, Settings, Play now, the network teams and the "Team level"), the
  download screen (Match, Trade, Download, Wi-Fi settings), the small buttons (Exit, Stop, OK, View, Cancel, Yes,
  No) and the 16 title banners of the top screen (Match, Download via Wi-Fi, Connection error, Superlink 2→3...).
  Translated from the Japanese: CyberLife had put "Pitch & computer" on the change-uniform button and "COM battle"
  on the one that picks the CPU's team and the pitch.
  Also the download panels (New players, "Recrute os baixados pelo mapa de contatos!", Items and the sports shop's
  explanation) and the Passing notice on the top screen.
  And the Passing Challenge menu (Passing Challenge, Match, Settings, the challenges with network data and with
  received data, Create profile, Records, Tournament), its windows (Start Passing?, Play with this team?, the
  11-player and name-and-emblem notices) and the "Download team data" one.
  Also the Passing top screen profile card (Team level, Play time, Team, Challenges received, Recruited), its title
  banners and the communication windows: Congratulations on beating the game (link with Spark/Bomber), Please
  wait, the strongest team on Mount Magnitude, Connection error, Waiting for the rival, Cancel and Next.
  And the player's 40 titles (Onze rival, Pinguim-imperador, Muralha de ferro...), which were in faulty English.
  And the eight pages of the Superlink 2→3 and Ogre Premium Link tutorial, whose illustrations had text in Japanese
  (the console labels, the explanations, the other game's title screen and the buttons).
- **1.7:** the slot menu messages that were in English: load failure and save failure, the one warning that the
  game will try to save again (which in CyberLife talked about DS wireless matches, something else) and the two
  corrupted save notices ("Os dados salvos do slot estão corrompidos..."). Also the collection: the list
  (Promotional video, Illustrations, Music, Characters, Maps, Techniques, Secret illustration), the music tabs (Maps,
  Matches, Events) and the Play, Stop and Exit buttons.
  And the "Connect" menu: Match, Passing Challenge, Trade, Download, Wi-Fi settings, the Download submenu (Download
  via Wi-Fi, Download via DS wireless, Download team data, Superlink 2→3, Ogre Premium Link), the Premium Scout and
  Premium Item buttons and the notice windows ("Sem 11 jogadores no time...").
- **1.6:** the text of the gallery images in Portuguese: the "Extras" button (before "Gallery") in the continue
  menu, the list (Videos, Generate password, Phone link, Movie link, Collection, Surprise), the video screen
  ("Escolha o vídeo que quer ver.", View, Exit), the generate-password screen and the movie link one (buttons,
  questions and Yes/No). Also the notice about altered saves that shows when loading the game ("Alterar os dados
  salvos com ferramentas de modificação..."), which was in English.
- **1.5:** what was still in Japanese in the game's tables: the mini-match objectives ("Roube a bola do
  adversário!", now all in Portuguese, before half in English), the descriptions of the four techniques that change
  the player's element (Kaze/Hayashi/Hi/Yama no Gotoshi), the prizes on the practice match map, the teams in the
  premium player conditions and a team name in the binder (Kasamino).
  Also the team and prize names in these tables that were in English ("Neo Japan" → "Novo Japão", "Women Selection"
  → "Seleção Feminina", "Benchers" → "Reservas"), following the names in the dialogue.
  The technique descriptions (technique list and binder) and the battle route conditions in Portuguese.
  **Fixes the item and tactic descriptions of 1.2, which did not show in the game**: the window only showed the
  accents. Now they are written in the form the window reads.
- **1.4:** the titles of the video gallery scenes (Continue → Gallery → Game Video) in Portuguese: 58 titles, with
  the character and stadium names from the dialogue.
  Also the names of who speaks in two matches that still showed in Japanese (Hibiki, Tae, Ichinose).
  And the menus and messages kept in the game's code: communication (DS wireless, Wi-Fi, trades, Passing),
  formation notices, quiz, scout and premium items (312 texts).
  Fixes the video subtitles, which lost the spaces between words in 1.3.
- **1.3:** the top screen objectives (the "Objetivo" strip) in Portuguese: 102, about 80 of them were still in
  English. The letters no longer sit on top of each other ("aconteceu", "averiguar"): the text gets 1 px spacers
  where the game switches sprites.
- **1.2:** items and tactics in Portuguese: the item names (before in English), the descriptions of every item and
  the tactic descriptions. The technique names are not translated, but were revised to romaji ("Sabaki no
  Tettsui" instead of "Divine Stamp") and are the same in the technique list and in the notebooks.
- **1.1:** the whole story in Portuguese: what was missing from chapters 1 to 11, chapter 12 (the epilogue with
  Tenma), the field and shop messages, the player search machine and the recruitment notices (1,268 lines). What
  is still missing is in `docs/TRANSLATION-STATUS.md`, in the repository.
- **1.0:** CyberLife's translation, as it was.

## How to continue the translation

This mod is **made** from `translations/pt-BR/` in the repository (do not edit its files by hand): `language.json`
(name, version, authors, the accented letters), the text in `texts/<group>.json` (only the place of each line and
the Portuguese, without the game's text), the fonts in `font/` (PNG) and the text on images in `images.json`.
`tools/translate.py` checks everything and makes the mod over the CyberLife base (`translations/bases/CyberLife/`):

```sh
# what is still in Japanese, with each line's room (the game's text only goes to build/)
python3 tools/text.py extract boej.pak mods/CyberLifePlus build/text --only 3300

# how a name or term was already translated in the finished lines
python3 tools/text.py lookup build/text 円堂 ゴッドハンド

# write the lines in translations/pt-BR/texts/<group>.json, {"evet:33000301:4": "Alô?", ...}, then:
python3 tools/translate.py check pt-BR
python3 tools/translate.py build pt-BR --install           # raise "version" in language.json first
python3 tools/text.py survey boej.pak mods/CyberLifePlus     # how much is already translated
```

The text rules (room, accents, line breaks, names) are in `docs/TRANSLATION-STATUS.md`. To translate the game into
another language starting from this one, see `docs/TRANSLATING.md`.
