# Tradução PT-BR (CyberLifePlus): o que falta

Quanto do texto que o jogador vê o mod CyberLifePlus ainda deixa em japonês, medido com
`python3 tools/text.py survey boej.pak mods/CyberLifePlus` (como continuar a tradução: no fim). A conta é em
caracteres japoneses (kana + kanji) e deixa de fora:

- furigana: as leituras `[漢字/かんじ]` das tabelas e os registros de leitura que vêm depois de cada fala
  (`%1F`, `%2F`…);
- strings de debug (`■`, `%d`, `=`…), sobras de japonês depois do NUL de uma tradução mais curta, espaço que
  nenhum script mostra e "texto" que é só código ou gráfico lido como Shift-JIS por acaso;
- imagens, legendas dos vídeos e o ARM9 (ver o fim).

## Números

| | v1.0 | v1.1 | v1.2 |
|---|---:|---:|---:|
| Tudo, com repetições (o que o jogador lê; 783 mil caracteres no original) | 29 % | 32 % | **34 %** |
| Textos únicos (o trabalho que falta; 429 mil) | 39 % | 44 % | **47 %** |
| História, capítulos 1–11, aberturas e capítulo 12 | 91 % | 100 % | 100 % |
| Mensagens de campo/lojas/máquina de busca (35xx) e `mcht.pkb` | 41 % | 100 % | 100 % |
| Descrições dos itens e táticas | 0 % | 0 % | **100 %** |

Desde a v1.2 a conta inclui os nomes dos itens (o `item.dat` é embaralhado e o levantamento antigo não o lia), o
que subiu o total de 776 para 783 mil caracteres; as colunas v1.0 e v1.1 são da conta antiga.

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
| **Perfis dos jogadores** | `logic/unitbase.STR` | 60.390 | 0 % |
| Descrições de itens | `logic/item.STR` | 22 (2 textos que nenhum item usa) | 0 % → 100 % (v1.2) |
| Nomes de jogadores (sem as leituras em kana) | `logic/unitbase*.dat` | 2.133 (quase tudo "未定") | 83 % |
| Táticas | `logic/tacticscmd.STR` | 0 | 0 % → 100 % (v1.2) |
| Locais e cenas da galeria | `logic/fieldinf.dat`, `movie_view_f.dat` | 1.725 | 0 % |
| Blog | `script/blogpost.dat`, `blogres.dat` | 3.010 | 13 % |
| Outras tabelas (golpes, torneios, condições…) | `logic/*` | 1.238 | 96 % |
| Menus no código | overlays | 6.051 | 28 % |
| Outros scripts (ações da partida, ajuda…) | `act/mch/mrobj/help.pkb` | 736 | 0 % |

Fora da conta do `survey`: as leituras em kana dos nomes (`unitbase*.dat`, ~2.240 nomes ainda em kana; falta
ver onde aparecem) e o ARM9 (15 textos).

Os menus que faltam nos overlays são quase todos de comunicação (DS sem fio, troca, "すれちがい", ligação com o
Inazuma Eleven 2) e alguns avisos (`ov057` "今はこの選手の いどうはできません。", `ov023` quiz). O ARM9 não pode ser
mudado por um mod hoje (`from-dump`/`check` exigem o ARM9 igual ao retail).

Fora da conta: as legendas dos vídeos (`movie/txt`), que o levantamento não lê. A abertura ainda mostra legendas
em japonês ("確かに大事なところでの大失敗は辛い！"), e o aviso de ficção de um jogo novo ("この作品はフィクションです…")
também está em japonês.

## Roadmap

**Fase 1: fechar a história. Feita na v1.1.** 1.281 falas: o que faltava nos capítulos 1–11, o capítulo 12, as
aberturas, as mensagens de campo, lojas e máquina de busca (35xx), o `mcht.pkb` e as strings do `eve.pkb`
(rótulos de quem fala e a máquina de recuperação). Ficaram de fora, de propósito, as strings de debug do
`eve.pkb` e leituras de furigana sem uso (50).

**Fase 2: interface e dados que aparecem o tempo todo (cerca de 1.600 únicos).**
- **Feito na v1.2:** itens (820 descrições, 434 nomes que estavam em inglês) e táticas (43 nomes e descrições).
  **Técnicas não se traduzem** (regra abaixo): os nomes da lista de técnicas (`command.STR`, 360) e dos
  cadernos e táticas nos itens foram revistos para o romaji e ficaram iguais nos dois lugares. As habilidades
  (`Keeper Plus`…) ficaram como estavam.
- Falta: locais e cenas (194), rótulos de ação (27), menus dos overlays (264) e o que sobrou nas tabelas (48).
- Passar para o português o que ficou em inglês: `sp_binder.STR` (207), `command.STR`, condições, `games.STR`.
- Legendas dos vídeos e aviso de ficção (levantar primeiro).

**Fase 3: conteúdo opcional (cerca de 4.200 únicos, 101 mil caracteres).**
- Subeventos 3300 (2.353, incluindo as ~960 falas em inglês), scout/rotas (299), NPCs (1.343), eventos extras
  3250/3251 (157) e blog (96).

**Fase 4: volume (cerca de 6.500 únicos, 117 mil caracteres).**
- Falas nas partidas (4.154; muito repetidas: 250 mil caracteres com as repetições) e perfis dos jogadores
  (2.309). Dá para dividir por time, já que cada entrada 3700 e cada perfil é de um jogador.

**Fase 5: conferir no jogo.**
- Onde as leituras em kana dos nomes aparecem (busca, ordenação?), e só então decidir se traduzir.
- Imagens: o mod mexe em 92 dos 250 arquivos de `pic2d` e em 296 dos 567 de `pic3d`; nenhum de `obj2d` (15). Os
  que sobraram precisam de inspeção visual para ver quais têm texto.

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
  (× 32), e o jogo lê no máximo 128 bytes. A tradução fica no lugar do original e cabe até o texto seguinte. Ainda
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
  pelo menos 4 px (`objective_fit`). O espaçador é o código 0xCF da `FONT12`, que o `apply` deixa sem largura.
  Escreva o objetivo normalmente no `.json`; se o `check` recusar, troque palavras (os espaçadores também
  ocupam bytes). Cada sprite ainda desce 1 px em relação ao anterior, como no jogo original.
- `%s` (nome) precisa continuar na fala. Os marcadores de furigana (`%1F`…) saem na tradução.
- `evet.pkb` e `mcht.pkb` não são comprimidos; o `eve.pkb` é LZ10 por entrada, e ao mudar uma entrada é preciso
  recomprimir e refazer os deslocamentos do `.pkh` (alinhados em 16; o campo 0x1C do `.pkh` é o maior tamanho
  guardado).

## Como continuar a tradução

As traduções ficam em `translations/CyberLifePlus/<grupo>.json`, um arquivo por grupo de eventos
(`3300.json`, `mcht.json`…), como `{"evet:33000301:4": "Alô?\\nÉ do Super Onze?"}`: o lugar da fala (arquivo,
entrada, deslocamento do texto) e o português. Nas tabelas o lugar é `item.STR:deslocamento`, `item.dat:registro`
`tacticscmd.STR:deslocamento` ou `command.dat:registro` (`item.json`, `tacticscmd.json`, `command.json`); `extract --redo` lista também o que o mod já
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
(`eve`), os itens (`item.dat`, `item.STR`) e as táticas (`tacticscmd.STR`); as outras tabelas, os menus e as
imagens ainda são editados com `tools/mod.py expand`/`patch`.

## Limites do levantamento

É uma estimativa: o diálogo e as strings dos scripts são lidos pelo formato, mas as tabelas, os overlays e os
outros pacotes são lidos procurando strings Shift-JIS terminadas em NUL, e textos com só kanji (nomes) podem
ficar de fora quando estão no meio de código.
