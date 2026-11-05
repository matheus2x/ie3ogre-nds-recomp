# PT-BR translation (CyberLifePlus): what is left

*Em português: [`TRANSLATION-STATUS.pt-BR.md`](TRANSLATION-STATUS.pt-BR.md).*

How much of the text the player sees the CyberLifePlus mod still leaves in Japanese, measured with
`python3 tools/text.py survey boej.pak mods/CyberLifePlus` (how to continue the translation: at the end). The count
is in Japanese characters (kana + kanji) and leaves out:

- furigana: the `[漢字/かんじ]` readings of the tables and the reading records that come after each line (`%1F`,
  `%2F`…);
- debug strings (`■`, `%d`, `=`…), leftover Japanese after the NUL of a shorter translation, room no script shows
  and "text" that is only code or graphics read as Shift-JIS by chance;
- images (from v1.6 their text is in `images.json`), video subtitles and the ARM9 (see the end).

## Numbers

| | v1.0 | v1.1 | v1.2 | v1.4 | v1.5–v1.10 |
|---|---:|---:|---:|---:|---:|
| Everything, with repetitions (what the player reads; 782 thousand characters in the original) | 29 % | 32 % | 34 % | 35.2 % | **35.4 %** |
| Unique texts (the work left; 429 thousand) | 39 % | 44 % | 47 % | 48.3 % | **48.4 %** |
| Story, chapters 1–11, chapter openings and chapter 12 | 91 % | 100 % | 100 % | 100 % | 100 % |
| Field/shop/search machine messages (35xx) and `mcht.pkb` | 41 % | 100 % | 100 % | 100 % | 100 % |
| Item and tactic descriptions | 0 % | 0 % | 100 % | 100 % | 100 % |
| Video gallery titles | 0 % | 0 % | 0 % | 100 % | 100 % |
| Menus in the code (overlays) | 28 % | 28 % | 28 % | 95 % | 95 % |
| Other tables (mini-match objectives, prizes, conditions…) | 96 % | 96 % | 96 % | 96 % | **100 %** |

Since v1.2 the count includes the item names (`item.dat` is scrambled and the old survey did not read it), which
raised the total from 776 to 783 thousand characters; the v1.0 and v1.1 columns are from the old count. In v1.4 the
scripts' debug prints left the count (782 thousand). In v1.5 the leftover Japanese after the NUL of a shorter
translation in the tables left (the end of the original string, which the game never shows) and so did the
technique names CyberLife moved in `command.STR`: that alone removes ~1,200 characters, and v1.4 measured this way
gives 35.4 % and 48.4 %. Almost everything the survey counted in "other tables" was leftovers. From v1.6 to v1.9
the work was on the text in images, which is outside the count: the v1.9 `survey` gives the same numbers as v1.5.
v1.10 turned into Portuguese menus CyberLife had left in English, which the count already treated as translated.

The story is all in Portuguese since v1.1, but **more than half of the game's text is still missing**. Almost all
of it is in four blocks: player lines in matches, player profiles, sub-events and NPCs.

The first survey (v1.0: 33 % and 41 %) found the lines by walking the `evet` records. `text.py` uses the script's
references, which is what the game reads; the numbers above are all from the new method.

Besides Japanese, **lines in English** remained (the translation's base seems to have been an English patch):
about 1,200 in the dialogue (almost all in sub-events 3300, like "%s is somewhere in the mall. I say!") and about
250 in the tables (`sp_binder.STR` 207 of 376, `command.STR`, `ClearCondition.dat`, `OpenCondition.dat`,
`games.STR`). The percentage above counts these lines as translated.

## By area

The dialogue is in `script/evet.pkb`. Each entry's key is the event number (`eve31010000.ssd` is entry 31010000),
and the prefix says the kind.

| Area | Where | Japanese left (characters) | Translated (v1.0 → v1.1) |
|---|---|---:|---:|
| Story, ch. 1–11 | `evet` 3201–3211 | 0 | 90 % → 100 % |
| Chapter openings | `evet` 31xx | 0 | 91 % → 100 % |
| Chapter 12 / epilogue (Tenma) | `evet` 3212 | 0 | 0 % → 100 % |
| Field messages, shops, search machine | `evet` 35xx | 0 | 22 % → 100 % |
| Recruit/invite and match events | `mcht.pkb` | 0 | 84 % → 100 % |
| Event script strings | `eve.pkb` | 19 (unused furigana) | 96 % → 100 % |
| Extra events | `evet` 3250/3251 | 3,355 | 36 % |
| Sub-events (phone calls, recruitment) | `evet` 3300 | 107,906 | 26 % |
| Scout and battle routes | `evet` 3611/3613/3618 | 22,291 | 16 % → 100 % (v1.18) |
| Town NPCs | `evet` 36xx (the rest) | 54,072 | 2 % → 98 % (v1.18) |
| **Player lines in matches** | `evet` 3700 | 250,338 | 0 % |
| **Player profiles** | `logic/unitbase.STR` | 60,378 | 0 % |
| Item descriptions | `logic/item.STR` | 22 (2 texts no item uses) | 0 % → 100 % (v1.2) |
| Player names (without the kana readings) | `logic/unitbase*.dat` | 1,947 (almost all "未定") | 84 % |
| Tactics | `logic/tacticscmd.STR` | 0 | 0 % → 100 % (v1.2) |
| Video gallery titles | `logic/movie_view_f.dat` | 0 | 0 % → 100 % (v1.4) |
| Place names (maybe internal only, see phase 6) | `logic/fieldinf.dat` | 984 | 0 % |
| Blog | `script/blogpost.dat`, `blogres.dat` | 3,010 | 13 % → 88 % (v1.18) |
| Other tables (moves, tournaments, conditions…) | `logic/*` | 4 (a password) | 96 % → 100 % (v1.5) |
| Menus in the code | overlays | 456 (passwords, internal names and leftovers) | 28 % → 95 % (v1.4) |
| Labels of who speaks in matches | `mch.pkb` | 0 | 0 % → 100 % (v1.4) |

Outside the `survey` count: the kana readings of the names (`unitbase*.dat`, ~2,240 names still in kana; where they
show is still to be seen) and the ARM9 (15 texts).

In the overlays the passwords and internal names (see the checklist) were left on purpose, and three short labels
that do not fit in the paired form. The ARM9 cannot be changed by a mod today (`from-dump`/`check` require the ARM9
to equal the retail one).

Outside the count: the video subtitles (`movie/txt`), which the survey does not read. The opening song's lyrics
are translated since v1.15, and the 9 subtitles of other scenes that CyberLife left in Japanese since v1.18. The
fiction notice of a new game is a picture (`pic3d/script/atdn_02.pac_` and `mpd_m00.pac_`) that CyberLife already
translated.

## Checklist

What was done and what is planned, by phase. Each translation step updates this list: it checks what finished
(with the mod's version) and adds, in the right phase, what it found along the way. The details of each step are
in `docs/development/DIARY.pt-BR.md`.

### Phase 1: close the story

- [x] Chapters 1–11: what was missing (v1.1)
- [x] Chapter 12 / epilogue with Tenma (v1.1)
- [x] Chapter openings, 31xx (v1.1)
- [x] Field messages, shops and search machine, 35xx (v1.1)
- [x] Recruit/invite and match events, `mcht.pkb` (v1.1)
- [x] Script strings, `eve.pkb`: labels of who speaks and the recovery machine (v1.1). Left out on purpose: debug
  strings and 50 unused furigana readings.

### Phase 2: interface and data shown all the time (about 1,600 unique)

- [x] Items: 820 descriptions and 434 names, which were in English (v1.2)
- [x] Tactics: 43 names and descriptions (v1.2)
- [x] Techniques in romaji, the same in the list (`command.STR`, 360) and in the item notebooks (v1.2). They are not
  translated (rule below); the skills (`Keeper Plus`…) stayed as they were.
- [x] Top screen objectives: 102 texts, about 80 in English, and the overlapping letters (v1.3)
- [x] Video gallery titles, `movie_view_f.dat`: 58 (v1.4)
- [x] Labels of who speaks in the match scripts, `mch.pkb`: 19 (Hibiki, Tae, Ichinose) (v1.4). The rest of what the
  survey counted in `act/mch.pkb` (24 texts of `act.pkb`, "PK GK左ボール左×", "ブロック 成功") are debug prints and
  stay as they are.
- [x] Menus and messages in the overlays: 312 of 321 strings, in 34 overlays (v1.4). Almost all about communication
  (DS wireless, Wi-Fi Connection, trade, Passing, link with Inazuma Eleven 2 and Spark/Bomber), plus the formation
  notices (`ov057`), the `ov023` quiz and the scout and premium item windows.
- [ ] Short overlay labels left untranslated, because they do not fit in pairs and the drawer is unknown: "そうび"
  (`ov053`), "かう"/"うる" (`ov062`), "ファイター" (`ov082`, unknown context). See in the game whether they use the
  furigana parser; if not, they go into `PLAIN_LABELS`. The "カップ" of `ov072` left the list in v1.13: it is the
  "Cup" that comes after the tournament name (see the tournaments below).
- [ ] Half-width player names ("Kanon") disappear in `%s` messages that go through the furigana parser (it drops
  ASCII after the `sprintf`). This also applies to CyberLife's messages. Needs a fix in the port (the parser) or
  full-width names.
- [x] What was left in the other tables (v1.5). Almost everything the survey counted was leftovers after the NUL
  (now out of the count). The rest: the 10 mini-match objectives (`gamerule.dat`, half was in English), the
  descriptions of the 4 element-changing techniques (Kaze/Hayashi/Hi/Yama no Gotoshi, `command.STR`), 60 prizes on
  the practice match map (`PracticeGame0F`–`9F.dat`, with the names the mod already has in `item.dat` and
  `unitbase.dat`), 76 teams in the premium conditions (`premiumcondition_hurri.dat`: Raimon, Imperial, Ogre,
  Berryz) and Kasamino in `BinderData_O.dat`. The `ScoutSpCode.dat` passwords stay in Japanese (the player types
  them, like the overlays').
- [x] From English to Portuguese in the record tables (v1.5): the teams and prizes of the practice match map
  (`PracticeGame*F.dat`, 162), the binder (`BinderData_O.dat`, 71) and the premium conditions
  (`premiumcondition.dat`, 200), by the team name rule (below). The prizes come from the item name by id; three were
  abbreviated to fit ("Perfect Zone P.", "Amazon River W.", "Chave do laborat.").
- [x] "ＬＥＶＥＬ－ＧＯ" on the practice match map, which was "Level 5" (v1.5): the record tables also take names made
  only of full-width letters.
- [x] **Item, technique and tactic descriptions in pairs** (v1.5). Seen in the game: the description window goes
  through the furigana parser, which drops ASCII, and a description in plain text showed only as "éc" (the accent
  and the letter stuck to it). The v1.2 descriptions (items and tactics) never showed in the game. In pairs, a
  description takes twice the bytes: one that does not fit in its place goes to the end of the `.STR` and the
  `.dat` points there (the game reads 128 bytes of the file). Seven item descriptions were shortened.
- [x] From English to Portuguese (v1.5): the 383 technique descriptions (`command.STR`; 343 equal to the first line
  of the notebook, the skills with the item's sentence), the 378 technique binder descriptions (`sp_binder.STR`,
  all written by hand from the Japanese), the route win and opening conditions (`ClearCondition.dat`,
  `OpenCondition.dat`). `games.STR` is not read by the game (no code has its path; the battle objective comes from
  `gamerule.dat`) and stayed as it was.
- [x] Gallery menu (v1.6). It was an image, not text: the "Gallery" button of the continue menu (`MMSlot.SPF_`,
  "オプション" in the original, now "Extras"), the list (`pic3d/option`: Vídeos, Ligação com celular, Gerar senha,
  Ligação com o filme, Coleção, Surpresa), the video screen (`menu_MovieView`: Ver, Sair, "Escolha o vídeo que quer
  ver."), the generate-password one (`password`, `pwup_bg00.pac_`) and the movie link one (`movielink`). Translated
  from the Japanese (CyberLife had swapped meanings: "Web Identifiant" was "Generate password").
- [x] Notice about altered saves on loading (v1.6): images `pic2d/title/atdn_03.pac_` and `atdn_04.pac_`.
- [x] Slot menu messages (v1.7), images of `MMSlot.SPF_`: load failure (`SFDN_W02`), save failure (`W08`), "Trying
  to save again" (`W09`, where CyberLife had put a notice about DS wireless matches that is not the original's) and
  the two corrupted save notices (`ATDN_BG00`/`01`). "Sair" was already in Portuguese. The other windows of that
  file (loading, saving, questions, Yes/No) already were.
- [x] Collection (v1.7), images of `pic3d/collection`, translated from the Japanese: the list (Promotional video,
  Illustrations, Music, Version 1/2, Characters, Maps, Techniques, Secret illustration), the music tabs (Maps,
  Matches, Events; CyberLife had put "places/action/story") and the Play/Stop/Exit buttons. Left out: the 2 bpp
  textures (`BGMDN_I01`–`I03`), which the mod did not change and `image.py` does not read, and the screens inside
  the collection (`cl_bonus`, `cldnfNNN`, 1 and 6 bpp).
- [x] "Connect" menu (v1.7), `pic2d/MTSWiM.SPF_`: Match, Passing Challenge, Trade, Download, Wi-Fi settings,
  Secret, Exit; the Download submenu (Download via Wi-Fi, Download via DS wireless, Download team data, Superlink
  2→3, Ogre Premium Link), the premium buttons (Premium Scout/Item 2→3 and Ogre) and the four notice windows (11
  players, 12 to trade, feature not unlocked). The identical "Exit" of `MTSSrd` too.
- [x] Communication screens (v1.8), translated from the Japanese: the match screen (`MTSWic`: Find a rival, Join,
  Build team, Name and emblem, Change uniform — CyberLife had put "Pitch & computer" —, CPU and pitch, Settings,
  Play now, Network team 1–3, Play with network team 1–3, Team level, Team A/B, the "Joga" stamps, You/Rival,
  Master and the buttons Exit, Stop, OK, View, Cancel, Menu, Yes, No), the download one (`MTSWfDL`: the grey
  buttons, Yes/No, Cancel) and the 16 top screen title banners (`STSWic:WMU_T00`, which the survey had not found:
  Match, Passing Challenge, Trade, Download, Download via Wi-Fi, Connection error, Superlink 2→3, Premium
  Scout/Item...). The images identical in the retail in `MTSWip`, `MPSWip`, `MMSWic`, `MPSWic`, `MTSWis`, `MTSSrd`,
  `MPSPrd` and `MMProfd` got the same edits.
- [x] Download panels (v1.8, `MTSWfDL:WMDN_BG01`/`BG02`, 8 bpp): Download / New players, "Recrute os baixados pelo
  mapa de contatos!", "Quem vem pela Wi-Fi fica do lado de fora!", "Quem vem pelo DS sem fio fica aqui!", Download /
  Items and "Os itens de download chegaram à loja de esportes no fim do bairro de lojas!" (dialogue terms:
  人脈システム → mapa de contatos, 商店街 → bairro de lojas). The big title stays in the 12 px font: there is no
  bigger font in the mod.
- [x] Passing notice on the top screen (v1.8, `STSWic:SRU_W01`).
- [x] Passing top screen (v1.8, `SRSSru`): the profile card (`SRU_W30`/`W40`: Team level, Play time h/min, Team, V
  for victories, Challenges received, Recruited; the 通/人 units removed), New team, the level bar (`SRU_B20`), the
  five banners (`SRU_T00`) and Master. `SRU_I01` ("Ogre" for オーガ) stayed as CyberLife left it.
- [x] Communication windows (v1.8): `MTSWis` (Congratulations on beating the game, with Bomber and with Spark;
  Please wait; "O time mais forte está esperando! Vá até o Monte Magnitude!"; Yes/No; Stop), `MTSWip`/`MPSWip`
  (Cancel, Next) and the `WC_MSG` in the 8 files where it repeats (Connection error / The connection dropped,
  Waiting for the rival, "O rival está traçando jogadas" for 通信相手からのドロータイム).
- [x] Passing Challenge (v1.8, `MTSSrd`, 21 images), from the Japanese: Passing Challenge, Match, Settings, Challenge
  with network data 1–3, Challenge with received data (CyberLife had "Teamshare Mode", "Connect Team", "Cooperative
  Team"), Create profile, Build team, Name and emblem, Change uniform, Play with the story/network 1–3 team, Team
  level, Master, Records (プレイのきろく), Tournament, Play, Exit, End (おわる), OK, Save, Stop, Yes, No, the three
  instruction lines (Type your name...), the "Download team data" banner and the windows (Start Passing?, Play with
  this team?, the notices about 11 players, name and emblem and team data that cannot be used).
- [ ] **Images in English** (survey of the images CyberLife changed; `tools/image.py export` and the texts seen):
  - Superlink and Premium Link logos (`MTSWiM:SLDN_T00`/`T01`): stayed as CyberLife left them ("Superlink",
    "Premium link"), but T00 has a "From C3 Corp" in place of the reading ツー・スリー. Redrawing needs erasing over a
    transparent background and the circle, which `image.py` does not do yet;
  - match (`pic3d/common.pkb`, a pack with hashed names, still outside `image.py`): Half Time, End of Normal Time,
    Extra Time, Full Time, Penalty Shootout, Technic, Prestige, News/Member/Examine/Task, Miss!/Direct/Chain/Block,
    Replay;
  - and the other images changed by CyberLife: comparing the mod's `export` with `--retail`, 597 differed without
    our edits before v1.9 and 567 after (almost all in `pic3d`: shop, trade, tournament, techniques, training,
    scout, uniform, binder, Inabikari, contact map; in `pic2d`: the match screens `MPS*`/`SPS*`, menus `menu/*` and
    the title `MTSIni`), plus the ones it left in Japanese.
- [x] Link tutorial (v1.8, `MTSPi`, 8 pages of 8 bpp and 3 buttons): the Superlink 2→3 pages (`WPD_W00`–`W03`) and
  the Ogre Premium Link ones (`W04`–`W07`), where CyberLife had only changed the button. The "Other/This DS console"
  labels, the explanation boxes, the other game's title screen (Connect, Match, Search, Join, Build team,
  Name/emblem), "Network team (16)" and the list of what is received, and the Next, Start, Exit and Stop buttons.
  Left out: the tiny もどる stamp of the fake title screen (2 px letters) and the game logos.
- [x] Player titles (v1.8, `logic/rpgtitle.STR`, 40), from the Japanese: CyberLife had put faulty English ("Penguin
  Process" for 皇帝ペンギン, "Body Continues" for ガチムチボディ). イレブン → "Onze", as in "Super Onze". Plain text
  (the Passing card shows it so), up to the width of the widest original title.
- [x] Team menus (v1.9), from the Japanese: Friends (`pic3d/menu_minna`: Friends/Order banners, X Data, the sort
  list with the glossary's stats — Name, Nickname, Level, Kick, Body, Control, Guard, Speed, Stamina, Guts —, Swap,
  OK, Menu, End/Exit/Stop, Yes/No, B Friends and the "Shirt number" window), Equipment (`menu_equip`: Equipment,
  Change, OK, End, Exit, X Swap, X Data, Boots/Accessory/Gloves in the windows and tabs, in `FONT8`, and "pág") and
  Formation (`menu_hensei`: Team, Friends, "pág", Build team - Story / Network 1–3 -, Swap, OK, Copy, Open, Yes/No).
  30 images. The edits come from a generator that finds the original text's colours (body and outline) and, in the
  dark frames with the ball drawn behind, erases by ink instead of by column.
- [x] Text menus of the team and the field (v1.10, overlays), from the Japanese: the `ov000` field menu (Menu,
  Friends, Backpack, Formation, Data, System, Save and the submenus: Sort/All/Equip/Techniques,
  Items/Equip/Techniques, Story/Battle/Network 1–3, Blog/Lineup/Binder/Technique binder/Phone/Uniforms/Results,
  Records/Help/Options, Team, Members/Group/Team/Reserve), the top screen card (Team level, Friends, "Tít.", Grit,
  Friendship); the Friends Y menu (`ov044`: Sort, Shirt, Dismiss, Equip, Techniques, Items), the sorting (Name,
  Nickname, Level, Attributes and the seven stats) and the windows ("Dispensar %s?", "Precisamos de %s!", which was
  "power ofEndo is ok.", "Não dá para dispensar mais."); the match commands (`ov057`: Defence, Let's go!, Ace,
  Equip, Items, Techniques, Tactics, Formation); the backpack (`ov059`: Drop, Discard?, the GP/TP messages),
  learning techniques (`ov063`: Learn, Auto, New technique and the messages) and the "Unequip" of `ov056`. 110
  strings. `text.py` started handling the `ov000` lists (one pointer, several strings; see the technical rules).
- [x] Scout, binder and contact map in text (v1.11, overlays), from the Japanese: the player's sheet in the binder
  (`ov108`: how to recruit — Contacts, Hire, Special, Locked, Story, Machine, Phone, Wi-Fi, DS wireless, Premium
  Scout, After mini-battle, Spark/Bomber —, where — N/S Tokyo, Hokkaido, Kyoto, Fuji, Shopping district, Airport, the
  Liocott areas, Liocott centre, the islands, Mt. Magnitude — and the colour of the machine's coin: Red, Blue,
  Yellow; "Recrute para ver os dados!"), the scout search (`ov051`: By condition, By name, Place, Position, Attr.,
  Elem., Smart, Fast, Tough, Skilful, the positions, None), the result (`ov020`, `ov021`, `ov050`: Hire, Search
  another, Same/Change condition, Friendship) and the contact map messages (`ov046`: Recruitable, Already in the
  team, Downloaded by Wi-Fi, Starting point, Unlocked!, Locked by story/scout). 97 strings. ひきぬき → Contratar,
  ガチャ → Máquina, 人脈 → Contatos (the dialogue says "mapa de contatos").
- [x] Chest and door messages (v1.12, `ov131`), from the Japanese: "Você não tem a chave desta porta.", "O baú
  estava vazio.", "Parece que uma porta se abriu.", what the chest gives (item, " (caderno)" after the technique
  name, Formation card, Battle card, Grit +N, Friendship +N, and "Você ganhou!"), the loss of points ("Garra: N
  pontos", "Amizade: N pontos", "Você perdeu!"), "<name> vai sair do time." and "Fichário de jogadores completo!".
  `ov131` builds these messages in pieces in an event script and shows them in the dialogue box: plain text, with
  automatic wrapping, and the furigana readings in separate strings (marked `""`; without the `%1F` in the text
  nobody uses them). "Error", "Passing", "Downloaded Team" and "Cancel" (はいし), of unknown context, stayed as they
  are.
- [x] Tournaments, records and paired messages (v1.13), from the Japanese: the tournament (`ov022`: Round 1/2!,
  Round 1/2 OK!, To the final!, the exit, save and give-up windows, "Que pena! Eliminado!", the Exp./Grit/
  Friendship/Nothing prizes; `ov098`: the current or new bracket), the cup names (`TournamentName01`–`16.dat`, 48:
  "Copa Raimon Town", "Copa Fire Tornado"... The game writes the name and then "カップ"; the "Copa" went to the start
  of the name and the "カップ" of `ov022`/`ov072` became a space. The names follow the technique rule: CyberLife had
  "Tornado Fire", "Splash Rose", "Mahou", "Soul" and "Epsilon" for オメガ), the records (`ov031`: Time, Top scorer,
  Most matches, Mini-battles, Route matches, Battles won, Recruited, Friends now, Binder, Uniforms, etc.), the "Usar
  o time do modo história?" of `ov042`, the equipment shop (`ov062`: Equip now?, Equip another?, Not enough
  points, "%s equipou %s", "Não dá para levar mais."), the missing grit (`ov065`), technique teaching (`ov068`)
  and the end of the match (`ov083`: Save and quit?, Quit without saving?). 60 strings and 48 names.
- [x] Record images (v1.18, `pic3d/menu_ranking`), which CyberLife left in English: the "Registros de jogo" strip
  (プレイのきろく, "Records of play"), the "Artilheiros" and "Mais jogos" rankings (得点/出場回数ランキング, "Best
  Kickers", "Most Participation", as in `ov031`) and the B buttons "Fim" (おわる) and "Sair" (もどる; "Voltar" does
  not fit in 26 px). "Ranking" stays. Seen in the game (menu → Sistema → Recordes → A).
- [x] The ranking's top-screen panel (v1.18, `pic2d/menu/SMStat.SPF_:MSUP_W01`), in CyberLife's English ("Ranking /
  From football battles and matches"): "Ranking" and "Das minibatalhas e dos jogos" (※ミニバトル・しあいから集計).
  Seen in the game. The same file keeps the English badges "MAX Ability", "Ability+Boost" and "Ability+Boost MAX"
  (`MSUP_F13`, のうりょくMAX/＋α).
- [x] Stats in the training messages and the match periods (v1.14). The messages with a stat name go through the
  furigana parser (in pairs), and the names come in by `%s` (or stuck together) from 7-byte slots in a literal
  pool, which cannot move: in pairs 3 letters fit. Abbreviations it is: CHU, FIS (without the capital "Í", which
  the font lacks), CON, DEF, VEL, RES, COR. `ov064` ("%s: %s +%s!" and "%s: %s no máximo!"), the training result of
  `ov096` (the name and " subiu!", or "Exp. subiu" and "!"/" muito!", in the order the code glues them) and the
  "Time: %s +" table of `ov109`. The periods (`ov001`, plain 24 px labels): 1T, 2T, 1P, 2P (º is not in the font).
  30 strings.
- [ ] The other texts CyberLife left in English in the overlays (`extract --only ov --redo`, 46), all of unknown
  drawer or context: the training descriptions of `ov045` (two lines per training, without furigana, half-width in
  CyberLife), the other stats table of `ov109` (goes through `fn_ov109_0212586c`; does not show on the status
  screen, which is an image), the stats of `ov110`, the technique list (`ov070`: "(By Kind)" in half width and
  "（ｎａｍｅ）" in full width in the same place), "Master" on the card (`ov000:0x19374`) and "Error", "Passing",
  "Downloaded Team" and "Cancel" of `ov131`.
- [x] Friends status on the top screen (v1.18, `pic2d/menu/msup_bg02.pac_`): CyberLife had "Corpo" instead of
  "Físico" (ボディ). The eight labels redone over the original: Chute, Físico, Controle, Defesa, Veloc., Resist.,
  Coragem, Experiência (the whole "Velocidade" and "Resistência" touched the values, which can have three digits).
  Seen in the game (menu → Amigos).
- [ ] The uniforms screen's sentence, in CyberLife's English: "Choose a strip." (`pic2d/menu/msup_bg05.pac_`).
- [ ] Binder images (`pic2d/menu/SMBinder1`/`2`?), in CyberLife's English: Player Binder, Players, Role, Recruit
  mode, Where ?, Box, Coin color, Rivals, the groups (FF teams, Various teams, National teams, FFI teams 1/2, Alius
  School), By areas and the area names ("North Tokyo"). The name search button says "By names ROM HANG !": a
  CyberLife warning that the name search hangs. See whether it hangs in the port.
- [x] Opening (`movie/txt/op00f.dat`): the song's lyrics, 22 subtitles (v1.15). Before it showed in Japanese, with
  the kanji as "?" (CyberLife's font does not have them).
- [x] The other video subtitles (`movie/txt`) in Japanese (v1.18): 9, in 8 scenes (`a3m02a`, `a3m03a`, `a3m09a`,
  `a3m24a`, `a3m35a`, `a3m38a`, `a3m84a`, `a3m85a`). CyberLife merged and rewrote sentences in these scenes, so
  each one was translated to fit its neighbours ("Vai começar a" / "cerimônia de abertura" / "do Futebol
  Fronteira Internacional!"). The "Equipe Ogro!" of `a3m84a` became "Time Ogre!", as in the glossary. Not seen in
  the game (the scenes are from the story; the test save's gallery does not unlock them)
- [x] New game fiction notice ("この作品はフィクションです…"): it is the pictures `pic3d/script/atdn_02.pac_` ("Este jogo
  é uma obra fictícia…") and `mpd_m00.pac_` ("Os treinamentos e as técnicas especiais…"), which CyberLife had
  already translated. Seen in the game (2026-11-04, windowless session with a new save); the item was wrong

### Phase 3: a more readable font

Replace the letters' drawing with a cleaner, less pixelated one. It comes before the volume phases because all the
fitting of the text (line breaks at ~226 px, the objectives' spacers, the px widths of the tables and overlays,
abbreviations) depends on the width of each letter. **Rule of the phase: the metrics do not change** (in the fonts'
JSON, `cell`, `baseline` and, in each glyph, `left`, `width` and `advance`); only the drawing changes. That way no
already translated line has to be redone.

- [x] Freeze the metrics (2026-11-04): `font.py freeze translations/pt-BR` writes `font/metrics.lock` (cell,
  baseline, line height and `left`/`width`/`advance` of each of the 1,605 codes of the three fonts), and
  `font.py check`, `translate.py check`/`build` (the "fonts" step) and a test (`tools/tests/test_font.py`, no ROM)
  refuse a font with a different metric. A new letter passes (freeze again). Tested: moving `FONT12`'s "A" 2 px
  further already pushes story lines past 3 rows in the box
- [x] Does the game accept 2 bpp? (2026-11-04) **Not in `FONT12`, `FONT8` and `RUBI8`.** The factory
  `func_02029a24` creates the four fonts: the first three with the base class (vtable 0x0208FFEC) and `FONT12T`
  with the subclass (0x02091290). Every letter goes through the font's vtable +0x10/+0x14/+0x18/+0x1C (4 bpp
  tiled, 4 bpp linear, 8 bpp tiled, 8 bpp linear). In the base class all four read **1 bit per pixel** and paint a
  single colour (`func_02042b4c` etc.): a 2 bpp font there would come out scrambled. In the subclass
  (`func_0206ba24` etc.) value 1 is the text colour and 2 and 3 are the colours of bytes +0x1C/+0x1D of the object
  (outline and shadow chosen by the caller), not greys. So at 1× greys to smooth the edges need a change in the
  port, and even then each screen would need free in-between colours in its 16-colour palette: the real gain is the
  HD font (below)
- [x] **HD font from the port** (2026-11-04, `runtime/src/text_hd.cpp`; format in [`MODS.md`](MODS.md), "HD
  text"). With the 3D above 1× and Atkinson chosen in Settings → Mods → CyberLifePlus → Text font (default: the
  game's pixel font), the letters are drawn again at the high resolution with **Atkinson Hyperlegible** (OFL, in `translations/pt-BR/hd/`, chosen comparing it with Rubik, Fira
  Sans, Noto Sans and Nunito). How it works: hooks on the eight draw functions of the fonts (both classes) and on
  `NNS_G2dCharCanvasDrawChar` tag every pixel of the letter's box (which letter, which pixel of it); the tags follow
  the copies (DMA, `MIi_CpuCopy*`, `memcpy`) to VRAM and go away on clears; the 2D composition (BGs and sprites, in
  the large picture) and the 3D texture reads (the field menus are 4 bpp textures) replace every tagged pixel with
  the vector letter, fitted to the height and center of the original ink. Metrics untouched: the text stays in the
  same place. Seen in the game, at 3×, 4× and 8×: dialogue, objective, top screen card, save card, field menu (3D),
  name screen, "Garra/Amizade". Cost measured at 4×: 6.44 → 6.61 ms per frame
- [x] The font as an option of the mod (2026-11-05): Settings → **Mods** (launcher and pause menu) lists the mods with
  options (`settings.json`) and opens the submenu of each. In the launcher's Mods tab, "Settings" on the mod's row goes to
  the submenu (both only with the mod on). CyberLifePlus has "Text font (HD)": Default (the game's pixel font) or
  Atkinson Hyperlegible; in the pause menu the change applies at once. The choice goes to
  `[mod_settings]` of `boej.ini`, by the mod's name. The "HD text" box of Video is gone
- [ ] See the other paths in HD: `NNS_G2dCharCanvasDrawChar` (the hook is there, but no screen seen used it), the
  `FONT12T` subclass (same; the HD outline is the letter grown by 1 px), the video subtitles, the paired
  descriptions, the binder and the communication screens
- [ ] HD text with the 3D at 1×: the large picture exists only with the 3D above 1× today. A scale of the 2D's own
  (`[video] text_scale`?) would make the text sharp without paying for the 3D at high resolution
- [ ] Smooth edges: each sample of the HD letter is ink or not (at 3× it already looks smooth); mix the colors on
  the edge by the TrueType coverage, where the layer's effect allows it
- [ ] Savestate: the tags are not in the state; after a load, the text already on screen stays native until the game
  draws it again (a new box, another menu)
- [ ] Optional (HD covers the case that matters): redraw `FONT12` on the same grid in 1 bpp, for those who play at 1×
- [ ] New languages (`translate.py new`): suggest the same `hd_fonts` as PT-BR

### Phase 4: optional content (about 4,200 unique, 101 thousand characters)

- [ ] Sub-events 3300: 2,353, including the ~960 lines in English
- [x] Scout and battle routes, 3611/3613/3618 (v1.18): 1,326 places in Japanese (269 unique texts: the NPCs of the
  schools and towns, the Premium Scout tips, the tournaments, the extra and premium routes, the extra routes'
  machine, G-Hunter Hiro, the deer of Nara), 204 that CyberLife left in English (the hiring machine, the invitations
  to the routes and tournaments, Chaos/Prominence/Diamond Dust) and 1 that only the base had. 超次元トーナメント =
  Torneio Especial, エクストラ対戦ルート = rota extra de jogos, プレミアム対戦ルート = rota premium, 対戦トーナメント =
  modo torneio, エキシビション対戦ルート = rota de exibição, イナズマ２ = IE2; in the scout tips, the tables' names
  (Rinka, IQ, IC, Geibo, Daikko, Zagomel, Nyuui). Not seen in the game
- [x] Town NPCs, 36xx (1,343): 3610 and 3615 done (v1.18: Raimon, the town of Inazuma and Japan's schools; 202
  unique texts in 480 places, plus 46 that CyberLife left in English: the hiring machine and the schools'
  invitations to the extra match) and 3617 (v1.18: the towns' children, elderly, shopkeepers, doctors and police;
  433 unique in 654 places, plus 7 of CyberLife's in English in the shops), 3616 (v1.18: the NPCs of Liocott and
  of Japan's towns, the cats, dogs and rabbits, Pyonta and his owner; 624 unique in 1,030 places, plus 1 in English)
  and 3620 (17 messages). `extract` finds no more Japanese lines in 36xx; the survey still counts 996 characters
  that are not lines `text.py` reads In the Premium
  Scout tips, the names of the player list (Lika, Shimizu, Tsugunaga, Sugaya, Natsuyaki, Susumu, Saiji); 2→3
  written "2-3" (Scout Premium 2-3, Itens Premium 2-3)
- [x] Extra events 3250/3251 (v1.18): Fideo's past in Italy (with Rushe) and Rococó's (with Levin Murdoch and his
  master), 170 lines in Japanese and 3 of CyberLife's that `check` did not accept (one with a broken "Í"). Names as
  in the story's lines (Rushe, Blasi, Levin Murdoch, Rococó, Satoru); the narration, the announcer and the
  notebook and 11-player messages reuse the translation of the same sentences in the story. Not seen in the game
- [x] Blog (v1.18, `blogpost.dat`/`blogres.dat`): 30 titles, 30 posts and 30 comments, besides CyberLife's 15
  (exported to `blogpost.json`/`blogres.json`). `text.py` got the converter (record tables in `script/`, the `BLOG`
  form); see the technical rules.
- [ ] The blog's 10 automatic posts (recruited by the scout or by hiring, who joined or left the team, player and
  item downloads: title and text, 20 places) are still in Japanese. The texts have `%s` with the player's name, and
  in the blog's form `%s` would become full-width; how the name goes into that parser is still unknown (look in the
  game after recruiting someone with the scout).
- [ ] Blog pictures: CyberLife's English header ("Get all the latest Inazuma Japan news here!", "Check back often
  for new entries!", "Football Blog") and the Japanese labels (トピックス, まえへ, とじる, つぎへ).

### Phase 5: volume (about 6,500 unique, 117 thousand characters)

- [ ] Player lines in matches, 3700 (4,154; 250 thousand characters with repetitions). It can be split by team:
  each entry is one player's.
- [ ] Player profiles, `unitbase.STR` (2,309), also by team

### Phase 6: check in the game

- [x] Video gallery: title no. 1 and the widest one seen in the game (v1.4)
- [x] Opening (v1.15): the 22 subtitles seen in the game, centred, with accents; the widest (238 px) whole
- [x] Technique description seen in the game (v1.5): in pairs, without a pixel between letters, with an accent
  (menu → Bag → Techniques, session `bug4` at frame 44000)
- [ ] Item and tactic descriptions in the game (same window and parser as the techniques; the test session's bag
  is empty) and the item names (in plain text, as CyberLife left them)
- [ ] Technique binder (`ov112`, `sp_binder`) and battle route conditions (`ov104`): the code uses the furigana
  parser (hence pairs), but the screens were not seen
- [ ] The other 217 objectives (only simulated; eight drawn in the game)
- [ ] The other 57 gallery titles (locked in the test save)
- [ ] Slot menu messages (v1.7): only in the preview. Breaking both copies of a slot in a copy of the save (bytes
  0x200 and 0x8200) only makes the slot show as empty, without a notice; what the game checks to show
  `ATDN_BG00`/`01` is still unknown. The load/save failure ones depend on a media error (force it in the save HLE?).
- [ ] Collection (v1.7): only in the preview. In the test save it shows as "???" and does not open.
- [x] "Connect" menu (v1.7): seen in the game the main menu, the Download submenu and the "Esta função ainda não
  está liberada" window (in Superlink 2→3). Not seen: the premium buttons, Secret and the other three windows.
- [x] Communication screens (v1.8): seen in the game the match screen, Find a rival (the three "Play with network
  team"), Build team (Network team 1–3), the Download submenu and the top banners "Match" and "Download via Wi-Fi".
  Not seen: Team level, Team A/B, the stamps, You/Rival/Master and the Yes/No windows (they need another DS), and the
  copies in the other files.
- [x] Team menus (v1.9): seen in the game the Friends screen (Friends banner, X Data, Swap, Menu, End, OK, Exit),
  the sorting (Order/Name banner, B Friends). Not seen: Equipment, the Formation screens (the test save only opens
  the submenu), Copy/Open and the Yes/No and "Shirt number" windows. Kageto's profile shows as "?から?まれた??の???"
  (kanji outside the font): it is the untranslated `unitbase.STR` (phase 5).
- [x] Text menus (v1.10): seen in the game the field menu and the Formation submenu, the top screen card ("Título"
  does not fit in the tab: "Tít."; Grit and Friendship below), the Y menu and the Friends sorting with the attribute
  list, and the "Precisamos de Endo!" window (in pairs). "Dismiss" is greyed out in the test save. Not seen: the
  match commands, the backpack, learning techniques and the other windows.
- [ ] Link tutorial (v1.8): only in the preview. In the test save Superlink 2→3 and Ogre Premium Link are locked
  ("Esta função ainda não está liberada").
- [ ] Player titles (v1.8): seen on the Passing card ("Onze rival"). The one reading the table is `ov000`, always
  loaded; whether they show on another screen (status menu?) and whether plain text works there is still to see.
- [x] Passing profile card (v1.8): seen in the game (Passing Challenge → Settings). The windows of `MTSWis`,
  `MTSWip`/`MPSWip` and `WC_MSG` not (they need Spark/Bomber or another DS).
- [x] Passing Challenge (v1.8): seen in the game the menu, the four challenges, the Settings screen and the Yes/No
  window of "Download team data". Not seen: the notice windows, Records/Tournament and the instructions of
  `SRD_O60`.
- [x] Passing notice (v1.8): seen in the game (Connect → Passing Challenge → A). The download panels not: they show
  neither in the Download submenu nor in "Download team data"; they must come after a download.
- [x] Binder (v1.11): seen in the game Iinoya's sheet (Contacts, N Tokyo, "Recrute para ver os dados!"). The values
  go in plain text in a font smaller than the 12 px one, centred in a ~88 px field. Not seen: the other methods and
  places (the test save only has Iinoya), the scout search and result and the contact map (still locked in that
  save: the "????" of the Data submenu).
- [x] Records (v1.13): seen in the game (menu → System → Records). In the original the labels have two lines
  ("ミニバトル" / "　対戦回数"), but in plain text the game stops at the `\n` (tested, also with the full-width space
  in front): they stayed on one line, as CyberLife did. Not seen: the tournament, the cups, the shop and technique
  teaching.
- [ ] Stats in training and match periods (v1.14): only in `check`. See in the game a training (the `ov096` result,
  the `ov064` message) and a match (the `ov001` periods: if they do not show, the drawer is the furigana parser and
  they go in pairs).
- [ ] Chest and door messages (v1.12): only in `check`. The chests are in each map's objects (`treasurebox.dat`
  only says the item and the amount, in 16-byte records); one near the test save is still to be found. Check that
  the plain text shows whole and that the leftover readings do not show.
- [ ] Overlay messages: the first seen in the game was the "Precisamos de Endo!" of `ov044` (v1.10), in pairs like
  the gallery's. The test save's formation did not allow triggering the `ov057` notices.
- [ ] Labels of who speaks in matches 34010007 and 34010022 (`mch.pkb`)
- [x] Street battle objectives (`gamerule.dat`, v1.5): seen in the game, in pairs and with an accent ("Objective"
  on the battle's top screen; session `bug3`, frame 35300)
- [ ] Do the other readers of `command.STR` (`ov045`, `ov063`, `ov080`) show the descriptions like `ov109`?
- [ ] Whether the names on the practice match map and in the premium conditions show to the player. Each comes
  next to the id of the team, item or player, and the game may take the shown name from the main tables. `ov089`
  picks between `premiumcondition.dat` and `_hurri.dat` by the byte 0x020A0640+0x89.
- [ ] Where the kana readings of the names show (search, sorting?), and only then decide whether to translate them
- [ ] Whether the `fieldinf.dat` names show to the player. They are 149 places in records of 0x180 bytes, with the
  name at +0x90 (at most 20 bytes; `func_0206f834` reads the record). They look like internal labels: several are
  cut mid-word ("…グラウン"), there are names of trainings and mini-battles, and not even the English patch translated
  them.
- [ ] Images: the mod touches 92 of the 250 files of `pic2d` and 296 of the 567 of `pic3d`, and none of the 15 of
  `obj2d`. The rest need visual inspection to see which have text. `image.py` does not read the 1, 2 and 8 bpp
  textures yet (about 210 frames, almost all of the `gmapNN` maps) nor the image packs with hashed names
  (`common.pkb`, `mbv_c.pkb`, `tcv_c.pkb`…).
- [x] Gallery in the game (v1.6, Windows test save): Extras, the list, the video screen, the generate-password one
  (question, password, OK) and the save notice, seen. Not seen: the movie link (`movielink`) and the phone link,
  which show as "???" in that save.

### Outside the mod (needs a change in the port)

- [ ] ARM9 (15 texts): a mod cannot change it today (`from-dump`/`check` require the retail ARM9)
- [x] Place, label and objective kept in the save in another version (a save from before the mod, or made with the
  mod and loaded without it, with "????"): the port replaces them with the texts of the scripts in use when drawing
  the top strip (see `MODS.md`, "Text kept in the save"). The load screen's card only changes after saving again
- [x] Objectives of opcodes 0x2019, 0x201A, 0x201C and 0x2024 (v1.18): `text.py` only recognized those of 0x201D, so
  these did not go through `objective_fit` ("Converse com o pessoal!" showed as "Converse cam", with letters
  overlapping). There are now 225 objectives, not 102. Of the 123 new ones, about 80 were in CyberLife's English
  and the rest had spaces put by hand in the middle of words ("garo to"): translated from the Japanese, in the
  style of the others, with the same text where the Japanese is the same. Seen in the game "Velhos amigos!
  Converse com eles!" (32011600, 0x201C) and "Agora é a batalha final!" (32112400, 0x201A)

### Other languages and tools

- [x] The translation in the language format (2026-10-31, [`TRANSLATING.md`](TRANSLATING.md)):
  `translations/pt-BR/language.json`, the mod made by `tools/translate.py` over CyberLife's v1.0 base, the fonts as
  PNG and CyberLife's text exported to the JSON (10,078 lines, 437 of them script strings CyberLife grew; 15,697 in
  total). The `build` gives the same text as the mod in all 79,610 places. Only PT-BR (and its copies) uses the
  CyberLife base; a new language starts from the original game, with PT-BR's Latin letters as a layer over the
  game's fonts
- [x] For languages on the original game (2026-10-31): player names (`unitbase.dat`, `unitbase_npc.dat`; 5,729 of
  CyberLife's exported) and profiles (`unitbase.STR`, in pairs) in `text.py`; script strings can grow up to the
  longest Japanese one of the same instruction (`ssd_rewrite`). The whole PT-BR text fits on the game
- [ ] 112 `unitbase` places of CyberLife only in the base: 44 full-width profiles (they do not pass `check` as they
  are) and 68 names 1–7 px wider than the field's widest Japanese one
- [x] The 130 story (3201–3211) and match (`mcht`) lines that only the base had (v1.18): 80 had 4 or 5 rows in a
  box after the game's word wrap, others were 1–2 bytes over because of the spaces CyberLife left at the end, and
  some used bytes only CyberLife's font draws (ligatures like "al" and "ar", Italian letters like "ì") or had junk at
  the end. 100 were fixed without changing the text (no trailing spaces, rows redone by the game's wrap and a new box
  with `\f` past 3 rows); 30 by hand, and where CyberLife wrote something else (Tsunami's line in `mcht`, "Nós
  voltamos logo!" for 行ってくるな　フユッペ) the translation follows the Japanese.
- [x] The 52 movie subtitles that only the base had (v1.18): they passed `check`, but `subtitled` would write them
  with other bytes (0xCF after an accent, where CyberLife put the plain space). With the plain-space rule, 48 come
  out identical to the base; the other 4 differ only in a lone "…", a full-width space and an accent at the end.
- [ ] The other 148 only in the base: 36 overlay labels (`ov045`, `ov070`, `ov109`, `ov110`) and 112 places of
  `unitbase` (see the item below). `translate.py todo pt-BR` lists them as "only in the base"
- [ ] CyberLife's permission for their text and fonts to be in the repository as plain text and PNG (up to v1.15,
  only in patches)
- [ ] Release: check that `mods/CyberLifePlus` is the result of `translate.py build pt-BR` (today a test only checks
  `mod.ini` against `language.json`)

### Credits and package

- [ ] CyberLife's credits with the members' names, links and the translation's version

## Translation conventions

The dialogue uses the names of the Brazilian dub, not those of the tables (which came from an English patch).
Before translating a name, see how it shows in the lines already translated.

| Japanese | Dialogue (PT-BR) | Note |
|---|---|---|
| イナズマジャパン | Super Onze | the tables say "Inazuma Japan" |
| イナズマイレブン (those of 40 years ago) | Lendários Super Onze | |
| 円堂 守 / マモルくん | Endo / Satoru | Endo's given name is Satoru in this translation |
| 鬼道, 不動, 久遠, 響木, 飛鷹 | Kido, Fudou, Kudou, Hibiki, Tobitaka | |
| 音無 / 木野 / 冬花 (フユッペ) | Haruna / Aki / Fuyuka | by the given name |
| 目金 / メガネ | Quatro Olhos | |
| ミスターＫ | Senhor K | |
| 監督 X | treinador X | |
| 必殺技 / 秘伝書 | técnica / caderno de técnicas | |
| ゴッドハンド | Mão Fantasma | technique names in the dialogue follow the dub |
| オルフェウス, ナイツオブクィーン, ザ・キングダム, ジ・エンパイア, リトルギガント | Orfeu, Cavaleiros da Rainha, O Reino, Império, Pequenos Gigantes | |
| ビッグウェイブス / ネオジャパン / チームＫ | Grandes Ondas / Novo Japão / Time K | |
| ライオコット島 / コトアール | Liocott / Cotarl | |
| 河川敷 / 商店街 / 合宿所 / 宿舎 | rio / bairro de lojas / alojamento / albergue | |
| 稲妻町 / 裏山 | cidade de Inazuma / Colina Oculta | |
| イナズマキャラバン / イナズマジェット | Caravana Relâmpago / Avião Relâmpago | |

Decided in v1.1 (there was no translation before): ねっけつポイント = pontos de garra; ねっけつ特訓 = treino de garra;
イナズマシンボル = Símbolo Inazuma; ゆうじょうポイント = pontos de amizade; ペンギーゴ = Penguigo; いやしのハーブ = Erva
Curativa; ピンバッチ = broche; 松風 天馬 = Tenma Matsukaze; 空野 葵 = Aoi Sorano; 如月 まこ = Mako Kisaragi; 稲妻ＫＦＣ =
Inazuma KFC.

**Technique (and tactic) names: not translated, but kept in readable romaji**, like "Majin the Hand": a Japanese
word becomes romaji ("Sabaki no Tettsui", "Tsuchidaruma", "Ushiro no Shoumen"), a katakana word from English stays
in English ("God Hand", "Pegasus Shot", "Phantom Shoot"), and ごう becomes "gou" without a hyphen ("Koutei Penguin
2gou", "Inazuma 1gou"). The same name in the technique list (`command.STR`) and in the notebook (`item.dat`). The
dialogue lines (which follow the dub, "Mão Fantasma") stay as they are.

**Team names in the tables** (v1.5): katakana from English stays in English ("Gemini Storm", "The Woods", "Dark
Emperors", "Unicorn"); a Japanese proper name stays in romaji, as in the dialogue ("Senbayama", "Manyuuji", "Zetsu
Yoroshiku"); those the dub translated follow the table above (Novo Japão, Grandes Ondas, O Reino, Império, Orfeu,
Time K, Time Garshield, Anjos Caídos, Leões do Deserto, Dragões de Fogo, Mensageiros do Céu, Exército Infernal Z,
Alien A/B); 帝国 = Imperial, 真・帝国学園 = Novo Imperial, 尾刈斗 = Sobrenatural, 野生 = Selvagem, ネオ雷門 = Nova Raimon,
オーガ = Ogre (as in the items). Descriptions become Portuguese: 代表 = Seleção (da Ásia, da Europa, Mundial…), 選抜 =
Combinado (Comb. when it does not fit), 女子選抜 = Seleção Feminina, ベンチーズ = Reservas, キャッピキャピ = Patricinhas,
○メガネ○ = Quatro Olhos, 雷門ＯＢ = Ex-alunos Raimon, システム予約 = Reservado. In the map labels (93 px) and the binder
(90 px), abbreviate what does not fit ("Cav. da Rainha", "Peq. Gigantes", "Mensag. do Céu").

Decided in v1.2 (items): ウェア/ユニフォーム = uniforme; スパイク/シューズ = chuteira; グローブ = luvas; ミサンガ = pulseira;
ペンダント = pingente; 秘伝書 = caderno de técnica (de chute, defesa, goleiro, ataque). Stats: キック = Chute, ガード =
Defesa, ボディ = Físico, コントロール = Controle, ガッツ = Coragem, スピード = Velocidade, スタミナ = Resistência. Elements: 火 =
Fogo, 風 = Vento, 林 = Floresta, 山 = Montanha. Teams in the names: 帝国 = Imperial, 尾刈斗 = Sobrenatural, 野生 = Selvagem
(the others by name, as in the dialogue). The game's menus still say "Kick", "Guard" etc. (from the English patch)
and should follow this list when translated.

## Technical text rules

- **Where a line is:** the event script (`eve.pkb`) points at each line with a `"@offset,length"` string. The
  length is that of the line plus the furigana records after it. A translated line may take all that room (the
  record's header stays intact and the text goes up to the NUL), but cannot go past it. In 41 entries the original
  translation moved the lines and updated these references (sometimes without changing the entry's size): what the
  mod's script points at always counts, not the position in the Japanese original.
- **Accents:** one byte each, in place of the half-width kana: à B1, á B2, â B3, ã B4, ç B8, é BA, ê BB, í BE, ó C4,
  ô C5, õ C6, ú CA; À D0, Á D1, Â D2, Ã D3, Ç D7, É D9, Ê DA, Ó AB, Ô AC, Õ AD, Ú A2. `º` and `ª` are not in the font.
- **Dialogue box:** `\n` breaks the line and `\f` starts another box. The game also wraps lines wider than ~226 px
  by itself (the 12 px font advances the glyph width + 1), but only shows **3 lines per box**: a fourth one
  disappears.
- **Tables (`logic`):** a `.STR` keeps the texts in 32-byte steps; the `.dat` points at each one by index (× 32),
  and the game reads at most 128 bytes. The translation stays in the original's place and fits up to the next text.
  **Item, technique and tactic descriptions go in pairs** (the window uses the furigana parser; seen in the game):
  the place is the offset in the original, and one that does not fit goes to the end of the `.STR`
  (`put_description`), limited to 127 bytes. The names stay in plain text. Whether the game wraps these lines by
  itself is still unknown; `check` assumes it does not: each line (the translation's `\n` becomes the real break)
  must fit in the width of the table's own widest line, and at most its number of lines (`item.STR`: 2 of 200 px;
  `item.dat` names: 1 of 133 px). `[` is not allowed (it is the tables' furigana).
- **`item.dat`:** 1024 records of 0x2C bytes, scrambled (XOR 0xAD, 2-bit rotation and byte swaps; `unscramble` in
  `tools/ntr/text.py`, taken from `func_020748c8`). The name is at 0x00–0x1F and the index of the description in
  `item.STR` at 0x2A.
- **`command.dat`:** 512 unscrambled records of 0x24 bytes; the index of the name in `command.STR` is at 0x18 (the
  description's at 0x1A). CyberLife reorganized `command.STR` (the file grew) and wrote the names in full width;
  `text.py` does the same (write plain ASCII: it converts) and, if the name does not fit in its place, writes it at
  the end of the file and changes the index. Maximum width: 124 px, that of CyberLife's widest name.
- **Objectives** (the top screen's "Objetivo" strip and the save card): record 3 of commands 0x2019, 0x201A, 0x201C,
  0x201D and 0x2024 of the scripts (`eve.pkb`). The game draws the text into 4 sprites of 64 px placed every 60 px, meant for 5 Japanese letters of
  12 px each. A letter only goes to the next sprite when it does not fit in the 64, so with narrow letters the last
  4 px of a sprite fall over the start of the next (it was the "t" over the "e" in "aconteceu"). The 1 px gap between
  letters comes before each letter, except the first of a sprite. `text.py` recognizes these objectives, simulates
  the drawing (`objective_layout`) and, when writing, puts 1 px spacers next to the spaces so that no letter sits on
  another and the gap between words stays at least 4 px (`objective_fit`). The spacer is code 0xDF of `FONT12` (no
  text uses it), which `apply` leaves with no width. 0xCF is CyberLife's space (4 px), used in its subtitles,
  scripts and tables as "letter, 0xCF, letter": do not touch it. Write the objective normally in the `.json`; if
  `check` refuses it, change words (the spacers also take bytes). In the original game each sprite shows the text
  1 px below the previous one; the port fixes that (a `Font_DrawString` hook, see the diary of 2026-10-24).
- **Video gallery (`movie_view_f.dat`):** 58 records of 0x74 bytes; the title is at +0x1B (88 useful bytes) and the
  number ("ＮＯ．１") at +0x0C. The game reads the title's furigana with a signed `char` (`func_ov016_020f24e8`):
  every byte from 0x80 up is treated as the first of two and copied with the next, and the rest of ASCII is thrown
  away. That is why `text.py` writes each one-byte character (letter, space, accent) paired with the 0xDF spacer,
  which draws nothing: the title takes twice the bytes (about 44 letters). The box does not put the pixel between
  letters, and the title goes from x = 16 up to 229 px wide.
- **Video subtitles (`movie/txt/*.dat`, place `op00f.dat:record`):** records `[u32 start frame][u32 end][u32
  size][text, NUL, zeros up to a multiple of 4]`, up to a start frame of 0xFFFFFFFF. The game reads the whole file
  (`func_ov016_020f2868`), so a subtitle may grow: `apply` rebuilds the file (`subtitle_file`). One line, centred
  under the video, through the same furigana parser as the gallery, but with 1 px after each letter, also after the
  0xDF spacer; the centring does not count that pixel, and with the spacer the lines came out shifted to the right
  and cut (seen in the game). That is why the subtitles are written as CyberLife did (`subtitled`): full-width
  letters (in the mod's `FONT12` they are proportional), the space as 0xCF paired with the next half-width letter,
  and an accent paired with the next letter (a space after an accent stays the plain space, `B2 20`, as CyberLife
  did; seen in the opening in "dá certo", v1.18). Up to 248 px; quotes (`"`) have no full-width form in the font.
- **Blog (`script/blogpost.dat`, `blogres.dat`):** fixed-size records with an 8-byte header. `blogpost.dat` has 45
  posts of 0x24E bytes: title at +0x08 (66 bytes) and text at +0x4A (516 bytes); `blogres.dat`, 64 comments of
  0x10A bytes, with the text at +0x08 (258 bytes; the empty ones are left out). The game goes through the furigana
  parser, and CyberLife wrote it as in the subtitles; `text.py` writes it so (`BLOG`), with `\n` as the real
  newline. A space after an accent stays a plain space (`B2 20`, as CyberLife): with 0xCF there, the post showed
  but the comment under it vanished (seen in the game). Size measured on screen (Dados →
  Blog → touch the post): title in one row of 200 px (the list of posts), text in 5 rows of 186 px (x = 63 to 251),
  comment in 2 rows of 190 px (x = 2 to 193).
- **Overlays (menus and messages in the code):** the recompiled code keeps the address of each string, so the
  translation stays in the same place and fits up to the original's NUL and 4-byte alignment (the following zeroed
  words may be data). `text.py` only takes strings the overlay has a pointer to and that are outside the code
  (`tools/mod.py code_ranges`). The messages go to the `ov016` windows, which go through the same furigana parser as
  the gallery: they are written in pairs (`PAIRED`). An accent carries the next letter in the pair, and `%s` stays
  whole for `sprintf`. That is why CyberLife wrote its messages in full width. Rules: the lines and width (with 1 px
  between letters) of the same overlay's original messages. Button labels with half-width neighbours
  (`PLAIN_LABELS` in `text.py`) go as plain text, with 12 px per letter of the original; those of one menu or
  column (`MENUS`) fit in the width of its widest original label. On label screens (`LABEL_OVERLAYS`: scout and
  binder) everything goes as plain text, except the `LABEL_MESSAGES` messages. The `ov131` messages
  (`DIALOG_OVERLAYS`) follow the dialogue box rules, with the break written as byte 0x0A, as in the original (in the
  scripts it is the 2-byte `\n`). A plain-text label has a single line: drawing stops at the `\n`. The `ov000`
  strings (`MENU_OVERLAYS`) are all menu ones, in plain text, and there a pointer may give a whole list: the code
  passes the first string and the count, and the game finds the others after each NUL (CyberLife moved them). Each
  string of the list has its place (the offset in the original), and `apply` rewrites the list in sequence, without
  extra NULs, in its whole room (up to the next pointer); one without a translation goes as the mod has it. Place:
  `ovNNN:0xoffset` (`overlays.json`). Passwords compared with what the player types stay in Japanese (`""`):
  "ゆうきがあれば" (`ov055`), "なつみぶか"/"ぜうすくろ" (`ov016`), "てんま" (`ov028`), "トンコツ" (`ov131`).
- **Technique descriptions (`command.STR:offset` in the original):** the index is at 0x1A of the `command.dat`
  record. `ov109` reads item (`item.dat` +0x2A), technique and tactic (`tacticscmd.dat`, records of 0x14, +0x0E)
  into the same buffer: in pairs, 2 lines in the width of the item and technique descriptions (200 px).
- **Technique binder (`sp_binder.dat:record`):** `sp_binder.dat` has a 2-byte count and 378 records of 12 bytes,
  with the item id at +2 and the byte offset of the description in `sp_binder.STR` at +10 (the strings are packed,
  without 32-byte steps). `ov112` copies the description into 0x100 bytes and draws it with the furigana parser: in
  pairs, up to 255 bytes, 2 lines of 189 px. `apply` rebuilds the whole `sp_binder.STR`, one string after the other,
  and writes the new offsets (`put_binder`).
- **Fixed record tables** (`RECORDS` in `text.py`; place `file:offset of the field`): the string stays in its
  field. `PracticeGame0F`–`9F.dat`: 34 records of 0x4A bytes, with two names (22 bytes) at +0x0C and +0x26, each
  after the id (+0x0A, +0x24); the width is that of both fields across the ten tables. `premiumcondition.dat`: 200
  records of 0x3C bytes, team at +0x04 (24); `premiumcondition_hurri.dat`: 0x40 bytes, team at +0x04 (24).
  `BinderData_O.dat`: 0x1C bytes, name at +0x04 (18). `gamerule.dat`: 64 records of 0x120 bytes, objective at +0x20
  (64 bytes, in pairs; it is the "Objective" of the street battles). `ClearCondition.dat`/`OpenCondition.dat`: 256
  records of 0x51 bytes, text at +0x01 (80 bytes, in pairs: about 39 letters). `TournamentName01`–`16.dat`: 3
  records of 0x14 bytes, name at +0x00 (19 bytes); `ov022` and `ov072` draw the name and then "カップ", and the width
  adds the 36 px of that suffix (`RECORD_AFTER`). The translation puts "Copa" at the start of the name, and the
  suffix became a space.
- **Images (`images.json`, `tools/image.py`):** the drawn menus (buttons, banners, notices) are images. pic3d keeps
  the textures in a `.SPL` (index) + `.SPD` (data) pair, each a compressed PAC (LZ10) with the 4 bpp texture, the
  palette and the frames (the pieces the menu shows: a normal, selected, greyed-out button…). pic2d keeps them in a
  `.SPF_` (index and data in one file, LZ10) or in a lone `.pac_`, with palette, 8×8 tile map and tiles; the map
  does not say the width, which `export` guesses by the seams (`"width"` fixes it). The edits start from the retail
  (Japanese) image and only replace the edited entries in the mod's file: `erase` removes the text (ink colours →
  the nearest non-ink pixel in the row, or each row with the colour of a clean column, or `under`: the pixel of
  another image with the same background), and `text` writes with the mod's `FONT12`, centred, with a shadow (right
  and below) or an outline, like the original labels. `"frames"` repeats the edit on a texture's frames. Small B
  buttons (48 px) use "Sair": "Voltar" does not fit with the shadow.
- `%s` (name) must stay in the line. The furigana markers (`%1F`…) go away in the translation.
- **Players:** `unitbase.dat` has 2,582 records of 0x68 bytes: full name at +0x00 (28 bytes), the name in the lists
  at +0x1C (16; in the game, the nickname in kana: "えんどう", "マックス"), the reading at +0x2C (32) and, at +0x66, the
  index (× 32) of the profile in `unitbase.STR`. CyberLife put the surname at +0x1C and the kana nickname at +0x2C.
  `unitbase_npc.dat`: 512 records of 0x50, name at +0x00, short name at +0x10 (16 each) and reading at +0x20 (32).
  Each field is measured against its own originals (`FIELD_RULES`). The profile is drawn by the furigana parser
  (seen in the game: in pairs it shows whole) and goes like the descriptions: one that does not fit goes to the end
  of the `.STR` and `unitbase.dat` points there. The Japanese ones have up to 110 bytes with the furigana.
- **Strings that grow in the scripts:** `apply` rewrites the script (`ssd_rewrite`): the record grows (4-aligned, up
  to 252 bytes), the following ones move and the header gets the new sizes (word 2 = file, word 5 = data); the code
  does not change, because the instruction finds its strings by the record's id. A string's limit is the longest
  Japanese one of the same instruction (`script_rooms`): place (0x4037) 23 bytes, objective (0x2019–0x201D, 0x2024) 38 to 43. As
  CyberLife did in the 393 scripts it grew.
- **Script strings (`eve.pkb`, `mch.pkb`):** each belongs to an instruction, and the command says what it is.
  0x3019 shows who speaks ("Senhor K", "Hibiki"); 0x2019, 0x201A, 0x201C, 0x201D and 0x2024 give the objective; 0x3070 only writes to the debug log
  ("■試合終了", "PK GK左ボール左×") and `text.py` leaves those out, also in the survey.
- `evet.pkb` and `mcht.pkb` are not compressed; `eve.pkb` and `mch.pkb` are LZ10 per entry, and changing an entry
  means recompressing it and redoing the `.pkh` offsets (16-aligned; field 0x1C of the `.pkh` is the largest stored
  size).

## How to continue the translation

The PT-BR translation is a translation mod, like any other language ([`TRANSLATING.md`](TRANSLATING.md)): everything
is in `translations/pt-BR/` and `mods/CyberLifePlus/` is **made** from it by `tools/translate.py`, over the CyberLife
base (`translations/bases/CyberLife/`, their v1.0). Do not edit `mods/CyberLifePlus/` by hand: what is not in
`translations/pt-BR/` disappears on the next `build`. There you find:

- `language.json`: the language, the mod's name, version, authors and description (they go into `mod.ini`), the
  accented letters and their bytes;
- `texts/<group>.json`: the lines, one file per group (`3300.json`, `mcht.json`…), like
  `{"evet:33000301:4": "Alô?\\nÉ do Super Onze?"}`: the place of the line (file, entry, offset of the text) and the
  Portuguese. In the tables the place is `item.STR:offset`, `item.dat:record`, `tacticscmd.STR:offset`,
  `rpgtitle.STR:offset`, `command.dat:record` (technique name), `command.STR:offset` (description),
  `sp_binder.dat:record`, `movie_view_f.dat:record`, in the subtitles `op00f.dat:record` or, in the record tables,
  `gamerule.dat:offset`; in the overlays, `ov057:0xa4dc` (`overlays.json`). Since 2026-10-31 the files also have
  CyberLife's text (exported with `text.py export`: 10,078 lines), not only what was translated here;
- `font/`: CyberLife's fonts (`FONT12`, `FONT12T`, `FONT8`) as PNG + JSON;
- `images.json` (text written over images) and, if any, `images/` (whole redrawn images).

**None of the Japanese text goes into the repository**: `text.py` refuses translations with Japanese, only writes
lists with the game's text inside `build/` (ignored by git), and a test (`tools/tests/test_text.py`) checks the
`translations/` folder. `""` marks a line skipped on purpose.

```sh
# 1. what is left in a group, with each line's room and the translated lines around it (in build/text/)
python3 tools/text.py extract boej.pak mods/CyberLifePlus build/text --only 3300 \
    --translations translations/pt-BR
#    (or: translate.py todo pt-BR --japanese --only 3300, which also lists the 148 lines only the base has)

# 2. how a name or term already shows in the finished lines (names and techniques: follow the dialogue, not the tables)
python3 tools/text.py lookup build/text 綱海 ゴッドハンド

# 3. write the Portuguese in translations/pt-BR/texts/3300.json and check (fonts, lines and images)
python3 tools/translate.py check pt-BR

# 4. raise "version" in translations/pt-BR/language.json, make the mod in mods/CyberLifePlus and measure again
python3 tools/translate.py build pt-BR --install
python3 tools/text.py survey boej.pak mods/CyberLifePlus
```

`check` refuses a line that does not fit its room, that goes past 3 lines in the box (with the game's automatic
wrapping), that loses a `%s` or that uses a character outside the font. `build` starts from the base, writes the
fonts, lines and images as patches, makes `mod.ini` from `language.json` and checks the mod (`mod.py check`);
`--install` replaces `files/`, `overlays/` and `mod.ini` of `mods/CyberLifePlus/` and leaves the READMEs. `text.py`
covers the dialogue (`evet`, `mcht`) and the script strings (`eve`, `mch`), the items (`item.dat`, `item.STR`), the
tactics (`tacticscmd.STR`), the player titles (`rpgtitle.STR`), the techniques (`command.dat`) and the video
gallery (`movie_view_f.dat`), the video subtitles (`movie/txt`), the record tables (`RECORDS`), the overlay strings
and the players (`unitbase*`). A table it does not cover (`BattleRouteTitle.dat`…) needs a new converter in
`text.py` before being translated: a hand edit in the mod is lost on the next `build`.

The text on images is in `translations/pt-BR/images.json` (format at the top of `tools/image.py`); to find the text
and the coordinates:

```sh
# the images as PNG (the game's colour indices); only in build/
python3 tools/image.py export boej.pak mods/CyberLifePlus build/images/retail --retail --only 'pic3d/option'
# after writing the edits: what does not fit, and a preview of the edited images
python3 tools/image.py check boej.pak mods/CyberLifePlus translations/pt-BR --png build/images/check
```

## Limits of the survey

It is an estimate: the dialogue and the script strings are read by their format, but the tables, the overlays and
the other packs are read by looking for NUL-terminated Shift-JIS strings, and kanji-only texts (names) may be left
out when they sit in the middle of code. A mod string that starts in the middle of an original string (after the
NUL of a shorter translation) is a leftover and does not count; in `command.STR` only the strings `command.dat`
points at count.
