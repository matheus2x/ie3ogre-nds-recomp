# Tradução PT-BR (CyberLifePlus): o que falta

Quanto do texto que o jogador vê o mod CyberLifePlus ainda deixa em japonês, medido com
`python3 tools/text.py survey boej.pak mods/CyberLifePlus` (como continuar a tradução: no fim). A conta é em
caracteres japoneses (kana + kanji) e deixa de fora:

- furigana: as leituras `[漢字/かんじ]` das tabelas e os registros de leitura que vêm depois de cada fala
  (`%1F`, `%2F`…);
- strings de debug (`■`, `%d`, `=`…), sobras de japonês depois do NUL de uma tradução mais curta, espaço que
  nenhum script mostra e "texto" que é só código ou gráfico lido como Shift-JIS por acaso;
- imagens (a partir da v1.6 o texto delas fica no `images.json`), legendas dos vídeos e o ARM9 (ver o fim).

## Números

| | v1.0 | v1.1 | v1.2 | v1.4 | v1.5–v1.9 |
|---|---:|---:|---:|---:|---:|
| Tudo, com repetições (o que o jogador lê; 782 mil caracteres no original) | 29 % | 32 % | 34 % | 35,2 % | **35,4 %** |
| Textos únicos (o trabalho que falta; 429 mil) | 39 % | 44 % | 47 % | 48,3 % | **48,4 %** |
| História, capítulos 1–11, aberturas e capítulo 12 | 91 % | 100 % | 100 % | 100 % | 100 % |
| Mensagens de campo/lojas/máquina de busca (35xx) e `mcht.pkb` | 41 % | 100 % | 100 % | 100 % | 100 % |
| Descrições dos itens e táticas | 0 % | 0 % | 100 % | 100 % | 100 % |
| Títulos da galeria de vídeos | 0 % | 0 % | 0 % | 100 % | 100 % |
| Menus no código (overlays) | 28 % | 28 % | 28 % | 95 % | 95 % |
| Outras tabelas (objetivos das minipartidas, prêmios, condições…) | 96 % | 96 % | 96 % | 96 % | **100 %** |

Desde a v1.2 a conta inclui os nomes dos itens (o `item.dat` é embaralhado e o levantamento antigo não o lia), o
que subiu o total de 776 para 783 mil caracteres; as colunas v1.0 e v1.1 são da conta antiga. Na v1.4 os prints
de depuração dos scripts saíram da conta (782 mil). Na v1.5 saíram as sobras de japonês depois do NUL de uma
tradução mais curta nas tabelas (o fim da string original, que o jogo nunca mostra) e os nomes de técnica que o
CyberLife tirou do lugar no `command.STR`: só isso tira ~1.200 caracteres, e a v1.4 medida assim dá 35,4 % e
48,4 %. Quase tudo o que o levantamento contava em "outras tabelas" era sobra. Da v1.6 à v1.9 o trabalho foi
no texto das imagens, que fica fora da conta: o `survey` da v1.9 dá os mesmos números da v1.5.

A história está toda em português desde a v1.1, mas **ainda falta mais da metade do texto do jogo**. Quase tudo
está em quatro blocos: falas dos jogadores nas partidas, perfis dos jogadores, subeventos e NPCs.

O primeiro levantamento (v1.0: 33 % e 41 %) achava as falas andando pelos registros do `evet`. O `text.py` usa
as referências do script, que é o que o jogo lê; os números acima são todos do método novo.

Além do japonês, ficaram **falas em inglês** (a base da tradução parece ter sido um patch em inglês): umas 1.200
no diálogo (quase todas nos subeventos 3300, como "%s is somewhere in the mall. I say!") e umas 250 nas tabelas
(`sp_binder.STR` 207 de 376, `command.STR`, `ClearCondition.dat`, `OpenCondition.dat`, `games.STR`). A
porcentagem acima conta essas falas como traduzidas.

## Por área

O diálogo fica em `script/evet.pkb`. A chave de cada entrada é o número do evento (`eve31010000.ssd` é a entrada
31010000), e o prefixo diz o tipo.

| Área | Onde | Japonês restante (caracteres) | Traduzido (v1.0 → v1.1) |
|---|---|---:|---:|
| História, cap. 1–11 | `evet` 3201–3211 | 0 | 90 % → 100 % |
| Aberturas de capítulo | `evet` 31xx | 0 | 91 % → 100 % |
| Capítulo 12 / epílogo (Tenma) | `evet` 3212 | 0 | 0 % → 100 % |
| Mensagens de campo, lojas, máquina de busca | `evet` 35xx | 0 | 22 % → 100 % |
| Recrutar/convidar e eventos das partidas | `mcht.pkb` | 0 | 84 % → 100 % |
| Strings dos scripts de evento | `eve.pkb` | 19 (furigana sem uso) | 96 % → 100 % |
| Eventos extras | `evet` 3250/3251 | 3.355 | 36 % |
| Subeventos (telefonemas, recrutamento) | `evet` 3300 | 107.906 | 26 % |
| Scout e rotas de batalha | `evet` 3611/3613/3618 | 22.291 | 16 % |
| NPCs das cidades | `evet` 36xx (resto) | 54.072 | 2 % |
| **Falas dos jogadores nas partidas** | `evet` 3700 | 250.338 | 0 % |
| **Perfis dos jogadores** | `logic/unitbase.STR` | 60.378 | 0 % |
| Descrições de itens | `logic/item.STR` | 22 (2 textos que nenhum item usa) | 0 % → 100 % (v1.2) |
| Nomes de jogadores (sem as leituras em kana) | `logic/unitbase*.dat` | 1.947 (quase tudo "未定") | 84 % |
| Táticas | `logic/tacticscmd.STR` | 0 | 0 % → 100 % (v1.2) |
| Títulos da galeria de vídeos | `logic/movie_view_f.dat` | 0 | 0 % → 100 % (v1.4) |
| Nomes de locais (talvez só internos, ver a fase 5) | `logic/fieldinf.dat` | 984 | 0 % |
| Blog | `script/blogpost.dat`, `blogres.dat` | 3.010 | 13 % |
| Outras tabelas (golpes, torneios, condições…) | `logic/*` | 4 (uma senha) | 96 % → 100 % (v1.5) |
| Menus no código | overlays | 459 (senhas, nomes internos e sobras) | 28 % → 95 % (v1.4) |
| Rótulos de quem fala nas partidas | `mch.pkb` | 0 | 0 % → 100 % (v1.4) |

Fora da conta do `survey`: as leituras em kana dos nomes (`unitbase*.dat`, ~2.240 nomes ainda em kana; falta
ver onde aparecem) e o ARM9 (15 textos).

Nos overlays ficaram de propósito as senhas e nomes internos (ver o checklist) e três rótulos curtos que não
cabem na forma em pares. O ARM9 não pode ser mudado por um mod hoje (`from-dump`/`check` exigem o ARM9 igual ao
retail).

Fora da conta: as legendas dos vídeos (`movie/txt`), que o levantamento não lê. A abertura ainda mostra legendas
em japonês ("確かに大事なところでの大失敗は辛い！"), e o aviso de ficção de um jogo novo ("この作品はフィクションです…")
também está em japonês.

## Checklist

O que já foi feito e o que está previsto, por fase. Cada etapa da tradução atualiza esta lista: marca o que
terminou (com a versão do mod) e acrescenta, na fase certa, o que descobriu no caminho. Os detalhes de cada
etapa ficam no `docs/DIARY.md`.

### Fase 1: fechar a história

- [x] Capítulos 1–11: o que faltava (v1.1)
- [x] Capítulo 12 / epílogo com o Tenma (v1.1)
- [x] Aberturas de capítulo, 31xx (v1.1)
- [x] Mensagens de campo, lojas e máquina de busca, 35xx (v1.1)
- [x] Recrutar/convidar e eventos das partidas, `mcht.pkb` (v1.1)
- [x] Strings dos scripts, `eve.pkb`: rótulos de quem fala e a máquina de recuperação (v1.1). De fora, de
  propósito: strings de debug e 50 leituras de furigana sem uso.

### Fase 2: interface e dados que aparecem o tempo todo (cerca de 1.600 únicos)

- [x] Itens: 820 descrições e 434 nomes, que estavam em inglês (v1.2)
- [x] Táticas: 43 nomes e descrições (v1.2)
- [x] Técnicas em romaji, iguais na lista (`command.STR`, 360) e nos cadernos dos itens (v1.2). Não se
  traduzem (regra abaixo); as habilidades (`Keeper Plus`…) ficaram como estavam.
- [x] Objetivos da tela de cima: 102 textos, uns 80 em inglês, e as letras sobrepostas (v1.3)
- [x] Títulos da galeria de vídeos, `movie_view_f.dat`: 58 (v1.4)
- [x] Rótulos de quem fala nos scripts das partidas, `mch.pkb`: 19 (Hibiki, Tae, Ichinose) (v1.4). O resto
  do que o levantamento contava em `act/mch.pkb` (24 textos do `act.pkb`, "PK GK左ボール左×", "ブロック 成功")
  são prints de depuração e ficam como estão.
- [x] Menus e mensagens nos overlays: 312 de 321 strings, em 34 overlays (v1.4). Quase todos de comunicação
  (DS sem fio, Wi-Fi Connection, troca, Passing, ligação com o Inazuma Eleven 2 e o Spark/Bomber), além dos
  avisos da formação (`ov057`), do quiz do `ov023` e das janelas de scout e itens premium.
- [ ] Rótulos curtos dos overlays sem tradução, porque em pares não cabem e não se sabe o desenhista: "そうび"
  (`ov053`), "かう"/"うる" (`ov062`), "カップ" (`ov072`), "ファイター" (`ov082`, contexto desconhecido). Ver no jogo
  se usam o leitor de furigana; se não usarem, entram em `PLAIN_LABELS`.
- [ ] Nomes de jogador em meia largura ("Kanon") somem nas mensagens com `%s` que passam pelo leitor de furigana
  (ele descarta o ASCII depois do `sprintf`). Vale também para as mensagens do CyberLife. Precisa de correção
  no port (o leitor) ou nomes em largura cheia.
- [x] O que sobrou nas outras tabelas (v1.5). Quase tudo o que o levantamento contava eram sobras depois do NUL
  (agora fora da conta). O resto: os 10 objetivos das minipartidas (`gamerule.dat`, metade estava em inglês),
  as descrições das 4 técnicas de mudar de elemento (Kaze/Hayashi/Hi/Yama no Gotoshi, `command.STR`), 60
  prêmios do mapa dos jogos-treino (`PracticeGame0F`–`9F.dat`, com os nomes que o mod já tem no `item.dat` e no
  `unitbase.dat`), 76 times das condições premium (`premiumcondition_hurri.dat`: Raimon, Imperial, Ogre,
  Berryz) e Kasamino no `BinderData_O.dat`. Ficam em japonês as senhas do `ScoutSpCode.dat` (o jogador as
  digita, como as dos overlays).
- [x] Do inglês para o português nas tabelas de registros (v1.5): os times e prêmios do mapa dos jogos-treino
  (`PracticeGame*F.dat`, 162), o fichário (`BinderData_O.dat`, 71) e as condições premium
  (`premiumcondition.dat`, 200), pela regra dos nomes de time (abaixo). Os prêmios vêm do nome do item pelo id;
  três foram abreviados para caber ("Perfect Zone P.", "Amazon River W.", "Chave do laborat.").
- [x] "ＬＥＶＥＬ－ＧＯ" no mapa dos jogos-treino, que estava "Level 5" (v1.5): as tabelas de registros pegam também
  os nomes só com letras de largura cheia.
- [x] **Descrições de itens, técnicas e táticas em pares** (v1.5). Visto no jogo: a janela de descrição passa
  pelo leitor de furigana, que descarta o ASCII, e a descrição em texto comum aparecia só como "éc" (o acento e
  a letra colada nele). As descrições da v1.2 (itens e táticas) nunca apareceram no jogo. Em pares, uma
  descrição ocupa o dobro de bytes: a que não cabe no lugar vai para o fim do `.STR` e o `.dat` passa a
  apontar para lá (o jogo lê 128 bytes do arquivo). Sete descrições de itens foram encurtadas.
- [x] Do inglês para o português (v1.5): as 383 descrições das técnicas (`command.STR`; 343 iguais à primeira
  linha do caderno, as habilidades com a frase do item), as 378 descrições do fichário de técnicas
  (`sp_binder.STR`, todas escritas à mão a partir do japonês), as condições de vitória e de abertura das rotas
  (`ClearCondition.dat`, `OpenCondition.dat`). O `games.STR` não é lido pelo jogo (nenhum código tem o caminho
  dele; o objetivo da batalha vem do `gamerule.dat`) e ficou como estava.
- [x] Menu da galeria (v1.6). Era imagem, não texto: o botão "Gallery" do menu de continuar (`MMSlot.SPF_`, no
  original "オプション", agora "Extras"), a lista (`pic3d/option`: Vídeos, Ligação com celular, Gerar senha,
  Ligação com o filme, Coleção, Surpresa), a tela dos vídeos (`menu_MovieView`: Ver, Sair, "Escolha o vídeo
  que quer ver."), a de gerar senha (`password`, `pwup_bg00.pac_`) e a da ligação com o filme (`movielink`).
  Traduzidas do japonês (o CyberLife tinha trocado sentidos: "Web Identifiant" era "Gerar senha").
- [x] Aviso sobre saves alterados ao carregar (v1.6): imagens `pic2d/title/atdn_03.pac_` e `atdn_04.pac_`.
- [x] Mensagens do menu de slots (v1.7), imagens do `MMSlot.SPF_`: falha ao carregar (`SFDN_W02`), falha ao
  salvar (`W08`), "Tentando salvar de novo" (`W09`, onde o CyberLife tinha posto um aviso sobre partidas por DS
  sem fio que não é o do original) e os dois avisos de save corrompido (`ATDN_BG00`/`01`). O "Sair" já estava
  em português. As outras janelas desse arquivo (carregando, salvando, perguntas, Sim/Não) já estavam.
- [x] Coleção (v1.7), imagens do `pic3d/collection`, traduzidas do japonês: a lista (Vídeo promocional,
  Ilustrações, Músicas, Versão 1/2, Personagens, Mapas, Técnicas, Ilustração secreta), as abas das músicas
  (Mapas, Partidas, Eventos; o CyberLife tinha posto "places/action/story") e os botões Tocar/Parar/Sair.
  Ficam de fora as texturas de 2 bpp (`BGMDN_I01`–`I03`), que o mod não mudou e o `image.py` não lê, e as
  telas de dentro da coleção (`cl_bonus`, `cldnfNNN`, de 1 e 6 bpp).
- [x] Menu "Conectar" (v1.7), `pic2d/MTSWiM.SPF_`: Partida, Desafio Passing, Troca, Download, Configurar
  Wi-Fi, Segredo, Sair; o submenu Download (Baixar via Wi-Fi, Baixar via DS sem fio, Baixar dados de time,
  Superlink 2→3, Link Premium Ogre), os botões premium (Scout/Item Premium 2→3 e Ogre) e as quatro janelas de
  aviso (11 jogadores, 12 para trocar, função não liberada). O "Sair" igual do `MTSSrd` também.
- [x] Telas de comunicação (v1.8), traduzidas do japonês: a tela de partida (`MTSWic`: Procurar rival,
  Entrar, Montar time, Nome e emblema, Trocar uniforme — o CyberLife tinha posto "Pitch & computer" —, CPU e
  campo, Configurar, Jogar agora, Time de rede 1–3, Jogar com o time de rede 1–3, Nível do time, Time A/B,
  os selos "Joga", Você/Rival, Mestre e os botões Sair, Parar, OK, Ver, Cancelar, Menu, Sim, Não), a de
  download (`MTSWfDL`: os botões cinza, Sim/Não, Cancelar) e as 16 faixas de título da tela de cima
  (`STSWic:WMU_T00`, que o levantamento não tinha achado: Partida, Desafio Passing, Troca, Download, Baixar
  via Wi-Fi, Erro de conexão, Superlink 2→3, Scout/Item Premium...). As imagens iguais no retail em
  `MTSWip`, `MPSWip`, `MMSWic`, `MPSWic`, `MTSWis`, `MTSSrd`, `MPSPrd` e `MMProfd` receberam as mesmas edições.
- [x] Painéis de download (v1.8, `MTSWfDL:WMDN_BG01`/`BG02`, de 8 bpp): Download / Novos jogadores, "Recrute
  os baixados pelo mapa de contatos!", "Quem vem pela Wi-Fi fica do lado de fora!", "Quem vem pelo DS sem fio
  fica aqui!", Download / Itens e "Os itens de download chegaram à loja de esportes no fim do bairro de
  lojas!" (termos do diálogo: 人脈システム → mapa de contatos, 商店街 → bairro de lojas). O título grande fica
  na fonte de 12 px: não há fonte maior no mod.
- [x] Aviso do Passing na tela de cima (v1.8, `STSWic:SRU_W01`).
- [x] Tela de cima do Passing (v1.8, `SRSSru`): o cartão de perfil (`SRU_W30`/`W40`: Nível do time, Tempo de
  jogo h/min, Time, V de vitórias, Desafios recebidos, Recrutados; as unidades 通/人 tiradas), Novo time, a
  barra do nível (`SRU_B20`), as cinco faixas (`SRU_T00`) e Mestre. O `SRU_I01` ("Ogre" por オーガ) ficou como o
  CyberLife deixou.
- [x] Janelas de comunicação (v1.8): `MTSWis` (Parabéns por zerar o jogo, com Bomber e com Spark; Aguarde um
  pouco; "O time mais forte está esperando! Vá até o Monte Magnitude!"; Sim/Não; Parar), `MTSWip`/`MPSWip`
  (Cancelar, Avançar) e o `WC_MSG` nos 8 arquivos onde se repete (Erro de conexão / A conexão caiu, Aguardando o
  rival, "O rival está traçando jogadas" por 通信相手からのドロータイム).
- [x] Desafio Passing (v1.8, `MTSSrd`, 21 imagens), do japonês: Desafio Passing, Partida, Configurar, Desafio
  com os dados de rede 1–3, Desafio com dados recebidos (o CyberLife tinha "Teamshare Mode", "Connect Team",
  "Cooperative Team"), Criar perfil, Montar time, Nome e emblema, Trocar uniforme, Jogar com o time da
  história/de rede 1–3, Nível do time, Mestre, Registros (プレイのきろく), Torneio, Jogar, Sair, Fim (おわる),
  OK, Salvar, Parar, Sim, Não, as três linhas de instrução (Digite o seu nome...), a faixa "Baixar dados de
  time" e as janelas (Iniciar o Passing?, Jogar com este time?, os avisos de 11 jogadores, de nome e emblema
  e de dados de time que não podem ser usados).
- [ ] **Imagens em inglês** (levantamento nas imagens que o CyberLife mudou; `tools/image.py export` e os
  textos vistos):
  - logotipos do Superlink e do Premium Link (`MTSWiM:SLDN_T00`/`T01`): ficaram como o CyberLife deixou
    ("Superlink", "Premium link"), mas o T00 tem um "From C3 Corp" no lugar da leitura ツー・スリー. Redesenhar
    exige apagar sobre fundo transparente e o círculo, o que o `image.py` ainda não faz;
  - partida (`pic3d/common.pkb`, que é um pacote com nomes em hash, ainda fora do `image.py`): Half Time, End
    of Normal Time, Extra Time, Full Time, Penalty Shootout, Technic, Prestige, News/Member/Examine/Task,
    Miss!/Direct/Chain/Block, Replay;
  - e as outras imagens mudadas pelo CyberLife: comparando o `export` do mod com o `--retail`, 597 diferiam
    sem edição nossa antes da v1.9 e 567 depois (quase todas em `pic3d`: loja, troca, torneio, técnicas,
    treino, scout, uniforme, fichário, Inabikari, mapa de contatos; em `pic2d`: telas da partida `MPS*`/`SPS*`,
    menus `menu/*` e o título `MTSIni`), além das que ele deixou em japonês.
- [x] Tutorial das ligações (v1.8, `MTSPi`, 8 páginas de 8 bpp e 3 botões): as páginas do Superlink 2→3
  (`WPD_W00`–`W03`) e do Link Premium Ogre (`W04`–`W07`), em que o CyberLife só tinha trocado o botão. Os
  rótulos "Outro/Este console DS", as caixas de explicação, a tela de título do outro jogo (Conectar, Partida,
  Procurar, Entrar, Montar time, Nome/emblema), "Time de rede (16)" e a lista do que se recebe, e os botões
  Avançar, Iniciar, Sair e Parar. Ficaram de fora o selo もどる minúsculo da tela de título falsa (2 px de letra)
  e os logotipos dos jogos.
- [x] Títulos do jogador (v1.8, `logic/rpgtitle.STR`, 40), do japonês: o CyberLife tinha posto um inglês com
  erros ("Penguin Process" por 皇帝ペンギン, "Body Continues" por ガチムチボディ). イレブン → "Onze", como em "Super
  Onze". Texto comum (o cartão do Passing o mostra assim), até a largura do título original mais largo.
- [x] Menus do time (v1.9), do japonês: Amigos (`pic3d/menu_minna`: faixas Amigos/Ordem, X Dados, a lista de
  ordenação com os status do glossário — Nome, Apelido, Nível, Chute, Físico, Controle, Defesa, Velocidade,
  Resistência, Coragem —, Trocar, OK, Menu, Fim/Sair/Parar, Sim/Não, B Amigos e a janela "Número da camisa"),
  Equipamento (`menu_equip`: Equipamento, Mudar, OK, Fim, Sair, X Trocar, X Dados, Chuteira/Acessório/Luvas
  nas janelas e nas abas, de `FONT8`, e "pág") e Formação (`menu_hensei`: Time, Amigos, "pág", Montar time -
  História / Rede 1–3 -, Trocar, OK, Copiar, Abrir, Sim/Não). 30 imagens. As edições saem de um gerador que
  acha as cores do texto original (corpo e contorno) e, nos quadros escuros com a bola desenhada atrás,
  apaga por tinta em vez de por coluna.
- [ ] Menus do jogo em texto que ainda estão em inglês (vistos no jogo, v1.9): o submenu da Formação ("Story",
  "Battle", "Time 1–3"), o menu Y dos Amigos ("Sort by", "Bib number", "Dismiss", "Equip", "Special moves",
  "Items") e a lista de ordenação dele ("Name", "Nickname", "Level", "Ability"). Achar de onde vêm (overlay ou
  tabela).
- [ ] Janela "power ofEndo is ok." ao apertar A num jogador já escalado na tela dos Amigos (em inglês, e sem o
  espaço antes do nome).
- [ ] Legendas dos vídeos (`movie/txt`): levantar e traduzir ("確かに大事なところでの大失敗は辛い！")
- [ ] Aviso de ficção de um jogo novo ("この作品はフィクションです…")

### Fase 3: conteúdo opcional (cerca de 4.200 únicos, 101 mil caracteres)

- [ ] Subeventos 3300: 2.353, incluindo as ~960 falas em inglês
- [ ] Scout e rotas de batalha, 3611/3613/3618 (299)
- [ ] NPCs das cidades, 36xx (1.343)
- [ ] Eventos extras 3250/3251 (157)
- [ ] Blog, `blogpost.dat`/`blogres.dat` (96)

### Fase 4: volume (cerca de 6.500 únicos, 117 mil caracteres)

- [ ] Falas dos jogadores nas partidas, 3700 (4.154; 250 mil caracteres com as repetições). Dá para dividir
  por time: cada entrada é de um jogador.
- [ ] Perfis dos jogadores, `unitbase.STR` (2.309), também por time

### Fase 5: conferir no jogo

- [x] Galeria de vídeos: título nº 1 e o mais largo vistos no jogo (v1.4)
- [x] Descrição de técnica vista no jogo (v1.5): em pares, sem pixel entre as letras, com acento (menu →
  Bag → Técnicas, sessão `bug4` no quadro 44000)
- [ ] Descrições de itens e táticas no jogo (mesma janela e mesmo leitor das técnicas; a bolsa da sessão de teste
  está vazia) e os nomes dos itens (em texto comum, como o CyberLife os deixou)
- [ ] Fichário de técnicas (`ov112`, `sp_binder`) e condições das rotas de batalha (`ov104`): o código usa o
  leitor de furigana (por isso em pares), mas as telas não foram vistas
- [ ] Os outros 101 objetivos (só simulados; seis desenhados no jogo)
- [ ] Os outros 57 títulos da galeria (bloqueados no save de teste)
- [ ] Mensagens do menu de slots (v1.7): só na prévia. Estragar as duas cópias de um slot numa cópia do save
  (bytes 0x200 e 0x8200) só faz o slot aparecer vazio, sem aviso; falta saber o que o jogo confere para
  mostrar o `ATDN_BG00`/`01`. As de falha ao carregar/salvar dependem de erro na mídia (forçar no HLE do save?).
- [ ] Coleção (v1.7): só na prévia. No save do teste ela aparece como "???" e não abre.
- [x] Menu "Conectar" (v1.7): visto no jogo o menu principal, o submenu Download e a janela "Esta função
  ainda não está liberada" (em Superlink 2→3). Não vistos: os botões premium, Segredo e as outras três janelas.
- [x] Telas de comunicação (v1.8): vistas no jogo a tela de partida, Procurar rival (os três "Jogar com o
  time de rede"), Montar time (Time de rede 1–3), o submenu Download e as faixas de cima "Partida" e "Baixar
  via Wi-Fi". Não vistos: Nível do time, Time A/B, os selos, Você/Rival/Mestre e as janelas Sim/Não
  (precisam de outro DS), e as cópias nos outros arquivos.
- [x] Menus do time (v1.9): vistos no jogo a tela dos Amigos (faixa Amigos, X Dados, Trocar, Menu, Fim, OK,
  Sair), a ordenação (faixa Ordem/Nome, B Amigos). Não vistos: Equipamento, as telas da Formação (o save do
  teste abre só o submenu), Copiar/Abrir e as janelas Sim/Não e "Número da camisa". O perfil do Kageto aparece
  como "?から?まれた??の???" (kanji fora da fonte): é o `unitbase.STR` sem tradução (fase 4).
- [ ] Tutorial das ligações (v1.8): só na prévia. No save do teste o Superlink 2→3 e o Link Premium Ogre
  estão bloqueados ("Esta função ainda não está liberada").
- [ ] Títulos do jogador (v1.8): vistos no cartão do Passing ("Onze rival"). Quem lê a tabela é o `ov000`,
  carregado sempre; falta ver se aparecem em outra tela (menu de status?) e se lá o texto comum funciona.
- [x] Cartão de perfil do Passing (v1.8): visto no jogo (Desafio Passing → Configurar). As janelas do `MTSWis`,
  `MTSWip`/`MPSWip` e `WC_MSG` não (precisam do Spark/Bomber ou de outro DS).
- [x] Desafio Passing (v1.8): vistos no jogo o menu, os quatro desafios, a tela Configurar e a janela Sim/Não
  de "Baixar dados de time". Não vistos: as janelas de aviso, Registros/Torneio e as instruções do `SRD_O60`.
- [x] Aviso do Passing (v1.8): visto no jogo (Conectar → Desafio Passing → A). Os painéis de download não:
  não aparecem nem no submenu Download nem em "Baixar dados de time"; devem vir depois de um download.
- [ ] Mensagens dos overlays: nenhuma vista no jogo (a forma em pares foi conferida na galeria, que usa o
  mesmo leitor). A formação do save de teste não deixou provocar os avisos do `ov057`.
- [ ] Rótulos de quem fala nas partidas 34010007 e 34010022 (`mch.pkb`)
- [x] Objetivos das batalhas de rua (`gamerule.dat`, v1.5): vistos no jogo, em pares e com acento ("Objective"
  na tela de cima da batalha; sessão `bug3`, quadro 35300)
- [ ] Os outros leitores do `command.STR` (`ov045`, `ov063`, `ov080`) mostram as descrições igual ao `ov109`?
- [ ] Se os nomes do mapa dos jogos-treino e das condições premium aparecem para o jogador. Cada um vem ao lado
  do id do time, item ou jogador, e o jogo pode tirar o nome mostrado das tabelas principais. O `ov089`
  escolhe entre `premiumcondition.dat` e `_hurri.dat` pelo byte 0x020A0640+0x89.
- [ ] Onde as leituras em kana dos nomes aparecem (busca, ordenação?), e só então decidir se traduzir
- [ ] Se os nomes do `fieldinf.dat` aparecem para o jogador. São 149 locais em registros de 0x180 bytes, com
  o nome em +0x90 (no máximo 20 bytes; `func_0206f834` lê o registro). Parecem rótulos internos: vários estão
  cortados no meio da palavra ("…グラウン"), há nomes de treinos e minibatalhas, e nem o patch em inglês os
  traduziu.
- [ ] Imagens: o mod mexe em 92 dos 250 arquivos de `pic2d` e em 296 dos 567 de `pic3d`, e em nenhum dos 15
  de `obj2d`. Os que sobraram precisam de inspeção visual para ver quais têm texto. O `image.py` ainda não lê
  as texturas de 1, 2 e 8 bpp (uns 210 quadros, quase todos dos mapas `gmapNN`) nem os pacotes de imagens com
  nome em hash (`common.pkb`, `mbv_c.pkb`, `tcv_c.pkb`…).
- [x] Galeria no jogo (v1.6, save do teste do Windows): Extras, a lista, a tela dos vídeos, a de gerar senha
  (pergunta, senha, OK) e o aviso de saves, vistos. Não vistos: a ligação com o filme (`movielink`) e a com
  celular, que aparecem como "???" nesse save.

### Fora do mod (precisa de mudança no port)

- [ ] ARM9 (15 textos): um mod não pode mudá-lo hoje (`from-dump`/`check` exigem o ARM9 do retail)
- [ ] Saves de antes do mod mostram o lugar e o objetivo gravados em japonês (card do save e faixa de cima) até
  salvar de novo

### Créditos e pacote

- [ ] Créditos do CyberLife com os nomes dos membros, links e a versão da tradução

## Padrão da tradução

O diálogo usa os nomes da dublagem brasileira, e não os das tabelas (que vieram de um patch em inglês). Antes de
traduzir um nome, veja como ele aparece nas falas já traduzidas.

| Japonês | Diálogo | Observação |
|---|---|---|
| イナズマジャパン | Super Onze | as tabelas dizem "Inazuma Japan" |
| イナズマイレブン (os de 40 anos atrás) | Lendários Super Onze | |
| 円堂 守 / マモルくん | Endo / Satoru | o nome dado do Endo é Satoru nesta tradução |
| 鬼道, 不動, 久遠, 響木, 飛鷹 | Kido, Fudou, Kudou, Hibiki, Tobitaka | |
| 音無 / 木野 / 冬花 (フユッペ) | Haruna / Aki / Fuyuka | pelo nome dado |
| 目金 / メガネ | Quatro Olhos | |
| ミスターＫ | Senhor K | |
| 監督 X | treinador X | |
| 必殺技 / 秘伝書 | técnica / caderno de técnicas | |
| ゴッドハンド | Mão Fantasma | nomes de técnica do diálogo seguem a dublagem |
| オルフェウス, ナイツオブクィーン, ザ・キングダム, ジ・エンパイア, リトルギガント | Orfeu, Cavaleiros da Rainha, O Reino, Império, Pequenos Gigantes | |
| ビッグウェイブス / ネオジャパン / チームＫ | Grandes Ondas / Novo Japão / Time K | |
| ライオコット島 / コトアール | Liocott / Cotarl | |
| 河川敷 / 商店街 / 合宿所 / 宿舎 | rio / bairro de lojas / alojamento / albergue | |
| 稲妻町 / 裏山 | cidade de Inazuma / Colina Oculta | |
| イナズマキャラバン / イナズマジェット | Caravana Relâmpago / Avião Relâmpago | |

Decididos na v1.1 (não havia tradução antes): ねっけつポイント = pontos de garra; ねっけつ特訓 = treino de garra;
イナズマシンボル = Símbolo Inazuma; ゆうじょうポイント = pontos de amizade; ペンギーゴ = Penguigo; いやしのハーブ = Erva
Curativa; ピンバッチ = broche; 松風 天馬 = Tenma Matsukaze; 空野 葵 = Aoi Sorano; 如月 まこ = Mako Kisaragi;
稲妻ＫＦＣ = Inazuma KFC.

**Nomes de técnicas (e táticas): não se traduzem, mas ficam em romaji legível**, como "Majin the Hand": palavra
em japonês vira romaji ("Sabaki no Tettsui", "Tsuchidaruma", "Ushiro no Shoumen"), palavra em katakana que vem do
inglês fica em inglês ("God Hand", "Pegasus Shot", "Phantom Shoot"), e ごう vira "gou" sem hífen ("Koutei Penguin
2gou", "Inazuma 1gou"). O mesmo nome na lista de técnicas (`command.STR`) e no caderno (`item.dat`). As falas do
diálogo (que seguem a dublagem, "Mão Fantasma") ficam como estão.

**Nomes de time nas tabelas** (v1.5): katakana que vem do inglês fica em inglês ("Gemini Storm", "The Woods",
"Dark Emperors", "Unicorn"); nome próprio japonês fica em romaji, como no diálogo ("Senbayama", "Manyuuji",
"Zetsu Yoroshiku"); os que a dublagem traduziu seguem a tabela acima (Novo Japão, Grandes Ondas, O Reino, Império,
Orfeu, Time K, Time Garshield, Anjos Caídos, Leões do Deserto, Dragões de Fogo, Mensageiros do Céu, Exército
Infernal Z, Alien A/B); 帝国 = Imperial, 真・帝国学園 = Novo Imperial, 尾刈斗 = Sobrenatural, 野生 = Selvagem,
ネオ雷門 = Nova Raimon, オーガ = Ogre (como nos itens). Descrições viram português: 代表 = Seleção (da Ásia, da
Europa, Mundial…), 選抜 = Combinado (Comb. quando não cabe), 女子選抜 = Seleção Feminina, ベンチーズ = Reservas,
キャッピキャピ = Patricinhas, ○メガネ○ = Quatro Olhos, 雷門ＯＢ = Ex-alunos Raimon, システム予約 = Reservado. Nos
rótulos do mapa (93 px) e do fichário (90 px), abreviar o que não couber ("Cav. da Rainha", "Peq. Gigantes",
"Mensag. do Céu").

Decididos na v1.2 (itens): ウェア/ユニフォーム = uniforme; スパイク/シューズ = chuteira; グローブ = luvas; ミサンガ =
pulseira; ペンダント = pingente; 秘伝書 = caderno de técnica (de chute, defesa, goleiro, ataque). Status: キック =
Chute, ガード = Defesa, ボディ = Físico, コントロール = Controle, ガッツ = Coragem, スピード = Velocidade, スタミナ =
Resistência. Elementos: 火 = Fogo, 風 = Vento, 林 = Floresta, 山 = Montanha. Times nos nomes: 帝国 = Imperial, 尾刈斗
= Sobrenatural, 野生 = Selvagem (os outros pelo nome, como no diálogo). Os menus do jogo ainda dizem "Kick", "Guard"
etc. (vieram do patch em inglês) e devem seguir esta lista quando forem traduzidos.

## Regras técnicas do texto

- **Onde a fala fica:** o script de evento (`eve.pkb`) aponta cada fala com uma string `"@deslocamento,tamanho"`.
  O tamanho é o da fala mais os registros de furigana que vêm depois dela. Uma fala traduzida pode ocupar esse
  espaço todo (o cabeçalho do registro fica intacto e o texto vai até o NUL), mas não pode passar dele. Em 41
  entradas a tradução original mudou as falas de lugar e atualizou essas referências (às vezes sem mudar o
  tamanho da entrada): vale sempre o que o script do mod aponta, e não a posição no original japonês.
- **Acentos:** um byte cada, no lugar do kana de meia largura: à B1, á B2, â B3, ã B4, ç B8, é BA, ê BB, í BE,
  ó C4, ô C5, õ C6, ú CA; À D0, Á D1, Â D2, Ã D3, Ç D7, É D9, Ê DA, Ó AB, Ô AC, Õ AD, Ú A2. `º` e `ª` não existem
  na fonte.
- **Caixa de diálogo:** `\n` quebra a linha e `\f` começa outra caixa. O jogo também quebra sozinho as linhas que
  passam de ~226 px (a fonte de 12 px avança a largura do glifo + 1), mas só mostra **3 linhas por caixa**: a
  quarta some.
- **Tabelas (`logic`):** um `.STR` guarda os textos em passos de 32 bytes; o `.dat` aponta cada um pelo índice
  (× 32), e o jogo lê no máximo 128 bytes. A tradução fica no lugar do original e cabe até o texto seguinte.
  **As descrições de itens, técnicas e táticas vão em pares** (a janela usa o leitor de furigana; visto no jogo):
  o lugar é o deslocamento no original, e a que não cabe vai para o fim do `.STR` (`put_description`), com
  limite de 127 bytes. Os nomes ficam em texto comum. Ainda
  não se sabe se o jogo quebra essas linhas sozinho; o `check` supõe que não: cada linha (o `\n` da tradução
  vira a quebra de verdade) tem de caber na largura da linha mais larga da própria tabela, e no máximo o
  número de linhas dela (`item.STR`: 2 de 200 px;
  nomes do `item.dat`: 1 de 133 px). `[` não pode (é o furigana das tabelas).
- **`item.dat`:** 1024 registros de 0x2C bytes embaralhados (XOR 0xAD, rotação de 2 bits e trocas de bytes;
  `unscramble` em `tools/ntr/text.py`, tirado de `func_020748c8`). O nome fica em 0x00–0x1F e o índice da
  descrição no `item.STR` em 0x2A.
- **`command.dat`:** 512 registros de 0x24 bytes sem embaralhar; o índice do nome no `command.STR` fica em 0x18
  (o da descrição em 0x1A). O CyberLife reorganizou o `command.STR` (o arquivo cresceu) e escreveu os nomes em
  largura cheia; o `text.py` faz o mesmo (escreva ASCII comum: ele converte) e, se o nome não cabe no lugar,
  grava no fim do arquivo e muda o índice. Largura máxima: 124 px, a do nome mais largo do CyberLife.
- **Objetivos** (a faixa "Objetivo" da tela de cima e o card do save): são as strings do comando 0x201D dos
  scripts (`eve.pkb`). O jogo desenha o texto em 4 sprites de 64 px colocados a cada 60 px, pensados para 5
  letras japonesas de 12 px cada. Uma letra só passa para o sprite seguinte quando não cabe nos 64, então
  com letras estreitas os últimos 4 px de um sprite caem em cima do começo do próximo (era o "t" sobre o "e" em
  "aconteceu"). O espaço de 1 px entre letras vem antes de cada letra, menos da primeira de um sprite.
  O `text.py` reconhece esses objetivos, simula o desenho (`objective_layout`) e, ao gravar, põe espaçadores
  de 1 px junto dos espaços para que nenhuma letra fique por cima de outra e o vão entre palavras continue com
  pelo menos 4 px (`objective_fit`). O espaçador é o código 0xDF da `FONT12` (nenhum texto o usa), que o
  `apply` deixa sem largura. O 0xCF é o espaço do CyberLife (4 px), usado nas legendas, scripts e tabelas dele
  como "letra, 0xCF, letra": não mexer.
  Escreva o objetivo normalmente no `.json`; se o `check` recusar, troque palavras (os espaçadores também
  ocupam bytes). No jogo original cada sprite mostra o texto 1 px abaixo do anterior; o port corrige isso (hook
  do `Font_DrawString`, ver o diário de 2026-10-24).
- **Galeria de vídeos (`movie_view_f.dat`):** 58 registros de 0x74 bytes; o título fica em +0x1B (88 bytes
  úteis) e o número ("ＮＯ．１") em +0x0C. O jogo lê o furigana do título com um `char` com sinal
  (`func_ov016_020f24e8`): todo byte de 0x80 para cima é tratado como o primeiro de dois e copiado com o seguinte,
  e o resto do ASCII é jogado fora. Por isso o `text.py` grava cada caractere de 1 byte (letra, espaço, acento)
  em par com o espaçador 0xDF, que não desenha nada: o título ocupa o dobro de bytes (cerca de 44 letras). A
  caixa não põe o pixel entre as letras, e o título vai de x = 16 até 229 px de largura.
- **Overlays (menus e mensagens no código):** o código recompilado guarda o endereço de cada string, então a
  tradução fica no mesmo lugar e cabe até o NUL e o alinhamento de 4 bytes da original (as palavras zeradas
  seguintes podem ser dados). O `text.py` só pega as strings para as quais a overlay tem um ponteiro e que estão
  fora do código (`tools/mod.py code_ranges`). As mensagens vão para as janelas do `ov016`, que passam pelo mesmo
  leitor de furigana da galeria: são gravadas em pares (`PAIRED`). Um acento carrega a letra seguinte no par, e
  `%s` fica inteiro para o `sprintf`. Por isso o CyberLife escreveu as mensagens dele em largura cheia. Regras:
  as linhas e a largura (com 1 px entre letras) das mensagens originais do mesmo overlay. Os rótulos de botão
  com vizinhos em meia largura (`PLAIN_LABELS` no `text.py`) vão como texto comum, com 12 px por letra do
  original. Lugar: `ovNNN:0xdeslocamento` (`overlays.json`). Senhas comparadas com o que o jogador digita
  ficam em japonês (`""`): "ゆうきがあれば" (`ov055`), "なつみぶか"/"ぜうすくろ" (`ov016`), "てんま" (`ov028`),
  "トンコツ" (`ov131`).
- **Descrições das técnicas (`command.STR:deslocamento` no original):** o índice fica em 0x1A do registro do
  `command.dat`. O `ov109` lê item (`item.dat` +0x2A), técnica e tática (`tacticscmd.dat`, registros de 0x14,
  +0x0E) no mesmo buffer: em pares, 2 linhas na largura das descrições de itens e técnicas (200 px).
- **Fichário de técnicas (`sp_binder.dat:registro`):** o `sp_binder.dat` tem 2 bytes de contagem e 378 registros
  de 12 bytes, com o id do item em +2 e o deslocamento em bytes da descrição no `sp_binder.STR` em +10 (as
  strings ficam coladas, sem passos de 32). O `ov112` copia a descrição para 0x100 bytes e a desenha com o leitor
  de furigana: em pares, até 255 bytes, 2 linhas de 189 px. O `apply` refaz o `sp_binder.STR` inteiro, uma string
  depois da outra, e grava os deslocamentos novos (`put_binder`).
- **Tabelas de registros fixos** (`RECORDS` no `text.py`; lugar `arquivo:deslocamento do campo`): a string fica
  no campo dela. `PracticeGame0F`–`9F.dat`: 34 registros de 0x4A bytes, com dois nomes (22 bytes) em +0x0C e
  +0x26, cada um depois do id (+0x0A, +0x24); a largura é a dos dois campos nas dez tabelas.
  `premiumcondition.dat`: 200 registros de 0x3C bytes, time em +0x04 (24); `premiumcondition_hurri.dat`: 0x40
  bytes, time em +0x04 (24). `BinderData_O.dat`: 0x1C bytes, nome em +0x04
  (18). `gamerule.dat`: 64 registros de 0x120 bytes, objetivo em +0x20 (64 bytes, em pares; é o "Objective" das
  batalhas de rua). `ClearCondition.dat`/`OpenCondition.dat`: 256 registros de 0x51 bytes, texto em +0x01
  (80 bytes, em pares: cerca de 39 letras).
- **Imagens (`images.json`, `tools/image.py`):** os menus desenhados (botões, faixas, avisos) são imagens. pic3d
  guarda as texturas num par `.SPL` (índice) + `.SPD` (dados), cada uma um PAC comprimido (LZ10) com a textura
  de 4 bpp, a paleta e os quadros (os pedaços que o menu mostra: um botão normal, selecionado, apagado…). pic2d
  guarda num `.SPF_` (índice e dados num arquivo só, LZ10) ou num `.pac_` sozinho, com paleta, mapa de tiles 8×8
  e tiles; o mapa não diz a largura, que o `export` adivinha pelas emendas (`"width"` fixa). As edições partem
  da imagem retail (japonesa) e trocam só as entradas editadas no arquivo do mod: `erase` apaga o texto
  (cores de tinta → o pixel não-tinta mais perto na linha, ou cada linha com a cor de uma coluna limpa, ou
  `under`: o pixel de outra imagem com o mesmo fundo), e `text` escreve com a `FONT12` do mod, centralizado, com
  sombra (direita e abaixo) ou contorno, como os rótulos originais. `"frames"` repete a edição nos quadros de
  uma textura. Botões B pequenos (48 px) usam "Sair": "Voltar" não cabe com a sombra.
- `%s` (nome) precisa continuar na fala. Os marcadores de furigana (`%1F`…) saem na tradução.
- **Strings dos scripts (`eve.pkb`, `mch.pkb`):** cada uma pertence a uma instrução, e o comando diz o que ela é.
  0x3019 mostra quem fala ("Senhor K", "Hibiki"); 0x201D é o objetivo; 0x3070 só escreve no log de depuração
  ("■試合終了", "PK GK左ボール左×") e o `text.py` deixa essas de fora, também no levantamento.
- `evet.pkb` e `mcht.pkb` não são comprimidos; o `eve.pkb` e o `mch.pkb` são LZ10 por entrada, e ao mudar uma
  entrada é preciso recomprimir e refazer os deslocamentos do `.pkh` (alinhados em 16; o campo 0x1C do `.pkh` é
  o maior tamanho guardado).

## Como continuar a tradução

As traduções ficam em `translations/CyberLifePlus/<grupo>.json`, um arquivo por grupo de eventos
(`3300.json`, `mcht.json`…), como `{"evet:33000301:4": "Alô?\\nÉ do Super Onze?"}`: o lugar da fala (arquivo,
entrada, deslocamento do texto) e o português. Nas tabelas o lugar é `item.STR:deslocamento`, `item.dat:registro`
`tacticscmd.STR:deslocamento`, `rpgtitle.STR:deslocamento`, `command.dat:registro` (nome da técnica), `command.STR:deslocamento` (descrição),
`sp_binder.dat:registro`, `movie_view_f.dat:registro` ou, nas tabelas de registros, `gamerule.dat:deslocamento`
(`item.json`, `tacticscmd.json`, `rpgtitle.json`, `command.json`, `sp_binder.json`, `movie_view_f.json`, `gamerule.json`,
`PracticeGame.json`, `ClearCondition.json`…); nos overlays, `ov057:0xa4dc` (`overlays.json`, e
`extract --only ov` lista o que falta); `extract --redo` lista também o que o mod já
tem em outra língua (os nomes em inglês). **Nada do texto japonês vai para o repositório**: o `text.py`
recusa traduções com japonês, só escreve listas com o texto do jogo dentro de `build/` (ignorada pelo git), e um
teste (`tools/tests/test_text.py`) confere a pasta `translations/`. `""` marca uma linha pulada de propósito.

```sh
# 1. o que falta num grupo, com o espaço de cada fala e as falas traduzidas em volta (em build/text/)
python3 tools/text.py extract boej.pak mods/CyberLifePlus build/text --only 3300 \
    --translations translations/CyberLifePlus

# 2. como um nome ou termo já aparece nas falas prontas (nomes e técnicas: siga o diálogo, não as tabelas)
python3 tools/text.py lookup build/text 綱海 ゴッドハンド

# 3. escreva o português em translations/CyberLifePlus/3300.json, confira e grave no mod (como patches)
python3 tools/text.py check boej.pak mods/CyberLifePlus translations/CyberLifePlus
python3 tools/text.py apply boej.pak mods/CyberLifePlus translations/CyberLifePlus

# 4. suba o version do mods/CyberLifePlus/mod.ini, confira o mod e meça de novo
python3 tools/mod.py check mods/CyberLifePlus boej.pak
python3 tools/text.py survey boej.pak mods/CyberLifePlus
```

O `check` recusa a fala que não cabe no espaço, que passa de 3 linhas na caixa (com a quebra automática do
jogo), que perde um `%s` ou que usa um caractere fora da fonte. O `apply` escreve as falas sobre o que o mod já
tem e regrava só os patches que mudaram. O `text.py` cobre o diálogo (`evet`, `mcht`) e as strings dos scripts
(`eve`, `mch`), os itens (`item.dat`, `item.STR`), as táticas (`tacticscmd.STR`), os títulos do jogador (`rpgtitle.STR`), as técnicas (`command.dat`) e a
galeria de vídeos (`movie_view_f.dat`), as tabelas de registros (`RECORDS`) e as strings dos overlays; as outras
tabelas ainda são editadas com `tools/mod.py expand`/`patch`.

O texto das imagens fica em `translations/CyberLifePlus/images.json` e vai para o mod com o `tools/image.py`:

```sh
# as imagens em PNG (índices de cor do jogo), para achar o texto e as coordenadas; só em build/
python3 tools/image.py export boej.pak mods/CyberLifePlus build/images/retail --retail --only 'pic3d/option'
# depois de escrever as edições: o que não cabe, e uma prévia das imagens editadas
python3 tools/image.py check boej.pak mods/CyberLifePlus translations/CyberLifePlus --png build/images/check
python3 tools/image.py apply boej.pak mods/CyberLifePlus translations/CyberLifePlus
```

## Limites do levantamento

É uma estimativa: o diálogo e as strings dos scripts são lidos pelo formato, mas as tabelas, os overlays e os
outros pacotes são lidos procurando strings Shift-JIS terminadas em NUL, e textos com só kanji (nomes) podem
ficar de fora quando estão no meio de código. Uma string do mod que começa no meio de uma string original (depois
do NUL de uma tradução mais curta) é sobra e não conta; no `command.STR` só contam as strings que o
`command.dat` aponta.
