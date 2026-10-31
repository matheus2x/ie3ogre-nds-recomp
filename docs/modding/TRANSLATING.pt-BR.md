# Traduzir o jogo para outro idioma

*In English: [`TRANSLATING.md`](TRANSLATING.md).*

Uma tradução é um **mod do tipo tradução**: uma pasta `translations/<idioma>/` com arquivos `.json` (os textos) e
`.png` (as letras da fonte e as imagens redesenhadas). Quem traduz edita só esses arquivos; o
`tools/translate.py` confere e gera o mod (os patches `.bps`, o `mod.ini` e o `.zip`), no seu PC. O jogador
recebe o `.zip` e o coloca na pasta `mods/`, como qualquer mod ([`MODS.pt-BR.md`](MODS.pt-BR.md)).

A tradução PT-BR (`translations/pt-BR/`, o mod CyberLifePlus) é feita assim e serve de modelo: ela tem todos os
lugares de texto que já foram traduzidos, e o estado e as regras dela estão em [`TRANSLATION-STATUS.pt-BR.md`](TRANSLATION-STATUS.pt-BR.md).

Há dois jeitos de começar um idioma:

| | Sobre o jogo japonês (padrão) | Duplicando o PT-BR |
|---|---|---|
| Comando | `translate.py new es --mod ...` | `translate.py new es --mod ... --duplicate pt-BR` |
| De onde o mod parte | o jogo original | a base do CyberLife, como o PT-BR (abaixo) |
| Começa com | nenhum texto; as letras latinas do PT-BR por cima das fontes do jogo | uma cópia de tudo do PT-BR (textos, imagens, fontes) para ir trocando |
| O que não for traduzido | fica em japonês | fica em português |
| Limites | as imagens só pelo que você traduzir (`images.json` ou PNG) | os do PT-BR |

## O que é preciso

- Python 3.12 ou mais novo (só a biblioteca padrão).
- O jogo: o `boej.pak` (o launcher o cria ao lado do jogo; no repositório, `build/boej.pak`) ou o `.nds` japonês.
  O `translate.py` acha o `boej.pak` sozinho; para outro caminho, `--image`.
- Um editor de imagens que salve PNG (para as letras novas e as imagens).

## Passo a passo

```sh
# 1. começar: a pasta com o language.json, as letras latinas do PT-BR (fonte) e texts/ vazio
python3 tools/translate.py new es --mod Traduccion-ES --name "Traducción al español" --author "Seu nome"
#    (ou, para partir de uma cópia do PT-BR: ... --duplicate pt-BR)

# 2. as letras do idioma: "charset" no language.json e o desenho delas em font/*.png (abaixo)

# 3. o que falta traduzir, por grupo, com o japonês e o texto do PT-BR ao lado (em build/translate/es/todo/)
python3 tools/translate.py todo es --only 3201,item

# 4. copie os .json do todo para translations/es/texts/, traduza os textos e confira
python3 tools/translate.py check es

# 5. gere o mod, teste no jogo e faça o .zip para distribuir
python3 tools/translate.py build es --install      # em mods/Traduccion-ES (ligue na página Mods do launcher)
python3 tools/translate.py build es --zip          # build/translate/Traduccion-ES-0.1.zip
```

`todo` sem `--only` lista tudo (mais de 21 mil lugares); `--japanese` acrescenta o que ainda está em
japonês em todos os idiomas. Os arquivos que o `todo` escreve ficam em `build/` porque têm o texto japonês do
jogo, que **nunca vai para o repositório** (o `check` recusa um texto com japonês).

## A pasta do idioma

```
translations/es/
  language.json            o centralizador
  texts/*.json             os textos, um arquivo por grupo (3201.json, item.json, overlays.json...)
  font/FONT12.png + .json  as fontes (também FONT12T e FONT8)
  images.json              texto escrito sobre imagens do jogo (opcional)
  images/<arquivo>/<entrada>.png   imagens redesenhadas inteiras (opcional)
```

`language.json`:

```json
{
  "type": "translation",
  "language": "es",
  "mod": "Traduccion-ES",
  "name": "Traducción al español",
  "version": "0.1",
  "author": "Seu nome",
  "description": "Uma frase sobre a tradução.",
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

| Campo | O que é |
|---|---|
| `language` | o código do idioma (o nome da pasta); aparece no launcher |
| `mod` | o nome da pasta do mod em `mods/` (sem vírgula) |
| `base` | a camada sobre a qual o mod é gerado: sem `base`, o jogo original; `CyberLife` só no PT-BR e nas cópias dele (abaixo) |
| `name`, `version`, `author`, `description` | vão para o `mod.ini` (o launcher mostra). Suba `version` a cada versão distribuída |
| `charset` | as letras do idioma que não são ASCII, cada uma num byte de `A1` a `DF` |
| `spacer`, `space` | bytes reservados (um espaçador sem largura e o espaço de 4 px das legendas): deixe `DF` e `CF` |
| `plain` | o que escrever no lugar de caracteres que a fonte não tem (reticências, aspas curvas) |
| `texts`, `images`, `pictures`, `fonts` | onde ficam os arquivos (os valores acima são o padrão) |

## Letras e fonte

O jogo escreve em Shift-JIS: ASCII ocupa 1 byte, e os bytes `A1`–`DF` (que no japonês eram katakana de meia
largura) são os únicos lugares livres de 1 byte. Cada letra do idioma que não é ASCII (acentos, `ñ`, `ß`, `¿`)
ganha um desses bytes no `charset` e um desenho nesse lugar das fontes. O PT-BR usa `A2`, `AB`–`AD`, `B1`–`B4`,
`B8`, `BA`, `BB`, `BE`, `C4`–`C6`, `CA`, `CC`, `D0`–`D3`, `D7`, `D9`, `DA`; uma letra que os dois idiomas têm pode
ficar no mesmo byte (o `new` copia o charset do PT-BR). `CF` e `DF` são reservados.

A fonte do jogo não tem letras ASCII. Por isso a fonte de um idioma é uma **camada** (`"layer": true` no JSON):
só as letras latinas (ASCII, os bytes `A1`–`DF`, a pontuação e as letras e números de largura cheia), que o
`build` desenha por cima da fonte do jogo, de onde vêm os kana e os kanji. O `new` começa a camada com as letras
do PT-BR (desenho do CyberLife); redesenhe as que quiser. Cada fonte é uma folha PNG e um JSON:

- `FONT12` (texto comum, 1 cor), `FONT12T` (menus, com contorno: 3 tons de cinza) e `FONT8` (texto pequeno).
- Na folha, 16 letras por linha, na ordem do JSON, cada uma na sua célula da grade vermelha. Escuro é tinta;
  claro ou transparente não. As colunas azuladas estão fora da largura da letra.
- No JSON, uma linha por letra: `code` (o byte), `char` (a letra, só para ler), `left` (colunas vazias à
  esquerda), `width` (largura do desenho) e `advance` (quanto o texto anda depois dela; o jogo põe 1 pixel a
  mais).
- Para uma letra nova: ache a linha do byte dela no JSON, desenhe na célula correspondente da folha (a posição
  na folha é a ordem no JSON) e acerte `left`, `width` e `advance`. `python3 tools/font.py check es` confere
  se toda letra do `charset` tem desenho.

Alfabetos que não cabem em 63 letras (cirílico, grego) precisam de letras de 2 bytes: o formato aceita códigos
novos (uma linha a mais no JSON e uma célula a mais na folha), mas o `charset` ainda não as escreve.

## Textos

Cada arquivo de `texts/` é `{"lugar": "texto"}`, como `{"evet:32010110:4": "Ah, Endo!\\nBom dia!"}`. O lugar diz
onde o texto fica no jogo (arquivo, entrada, posição); copie-o do `todo`, nunca invente. Regras que valem para
qualquer idioma (detalhes em [`TRANSLATION-STATUS.pt-BR.md`](TRANSLATION-STATUS.pt-BR.md), "Regras técnicas do texto"):

- `\n` quebra a linha e `\f` começa outra caixa de diálogo; a caixa mostra **3 linhas** (o jogo quebra sozinho as
  linhas largas demais; a quarta some).
- Cada lugar tem um espaço em bytes (o `todo` mostra): a letra do `charset` ocupa 1 byte; o japonês, 2.
- `%s` (o nome do jogador) tem de continuar no texto.
- `""` pula um lugar de propósito (fica como na base).
- Strings dos scripts (nomes de lugar, objetivos, quem fala): podem passar do espaço do japonês até o tamanho da
  maior string japonesa da mesma instrução (um nome de lugar, 23 bytes; um objetivo, 43); o `build` aumenta o
  registro do script.
- Jogadores (`unitbase.json`): em `unitbase.dat` cada um tem o nome completo (`+0`, 27 bytes), o nome das listas
  (`+0x1C`, 15) e a leitura (`+0x2C`, 31; no jogo, em kana); em `unitbase_npc.dat`, nome, nome curto e leitura
  (16, 16 e 32 bytes). O lugar é `unitbase.dat:<posição do campo>`. O perfil (`unitbase.STR:<posição>`) vai em
  pares, como as descrições dos itens: até 127 bytes, umas 60 letras em 2 linhas.
- Menus, nomes de itens e tabelas têm largura em pixels; o `check` mede com a sua fonte e diz o que passa.

O `check` diz, para cada problema, o lugar, o motivo e o começo do texto. O `build` não grava nada enquanto
houver problema.

## Imagens

Menus desenhados (botões, faixas, avisos) são imagens. Há dois jeitos de traduzi-las:

- **Texto sobre a imagem** (`images.json`): apaga o texto japonês de um retângulo e escreve o seu com a fonte do
  mod. Formato no começo de `tools/image.py`; o `images.json` do PT-BR tem 184 exemplos.
- **Imagem inteira** (`images/`): `python3 tools/translate.py images es --only 'pic3d/option'` exporta as imagens
  em PNG para `build/translate/es/images/`; redesenhe as que quiser e copie para `translations/es/images/`, nas
  mesmas pastas. Use as cores que a imagem já tem (cada imagem tem uma paleta de 16 ou 256 cores; uma cor fora
  dela vira a mais próxima, com aviso) e mantenha o tamanho. O `images.json` da mesma imagem é desenhado por cima.

## Sobre o jogo ou sobre a base do CyberLife

**Sobre o jogo japonês** (o padrão do `new`), o mod parte do jogo original: o que você não traduzir fica em
japonês. O `todo` usa o PT-BR como referência e acha cada lugar dele no jogo (o CyberLife mudou falas e strings
de lugar; o `todo` converte as posições). Medido com o texto inteiro do PT-BR, nomes dos jogadores incluídos:
as 21.426 falas cabem. O que o PT-BR só tem por causa da base fica em japonês até ser traduzido aqui: as imagens
que o CyberLife redesenhou (traduza-as pelo `images.json` ou por PNG) e as tabelas que o `text.py` ainda não lê
(títulos das rotas de batalha, blog).

**Duplicando o PT-BR** (`--duplicate pt-BR`), o mod é gerado sobre a **base CyberLife**
(`translations/bases/CyberLife/`, a tradução do grupo CyberLife como saiu do dump deles), como o PT-BR. Ela traz o
layout do CyberLife (falas que mudaram de lugar, scripts com espaço maior), as fontes latinas, as imagens e os
nomes dos jogadores. Por isso:

- **o que você não traduzir aparece como na base, em português** (ou em inglês, onde o CyberLife deixou assim); o
  `todo` lista os textos que ainda estão iguais aos do PT-BR;
- o espaço de cada lugar é o do layout do CyberLife; o `todo` marca as falas que só existem na base (o `check`
  não as aceita como estão: encurte-as);
- a base é trabalho do CyberLife: dê os créditos no `description` e no leia-me do seu mod.

## Distribuir

`build --zip` faz o `.zip` que o jogador coloca em `mods/` (ou adiciona pelo launcher). Ele só tem patches: nenhum
dado do jogo vai junto. O launcher mostra o mod como "Tradução (es)". Um arquivo ao lado do `mod.ini` (um
`README.md`, por exemplo) pode ir junto: com `--install`, o que já estiver em `mods/<mod>/` além de `files/`,
`overlays/` e `mod.ini` fica.

## O que ainda não dá

- **Vídeos** (`data_iz/movie/*.mods`): só as legendas (`movie.json`). Trocar o vídeo em si exige conhecer o
  formato (no radar).
- **Texto no código do jogo (ARM9)**: 15 textos; um mod não pode mudar o ARM9.
- **Strings dos scripts maiores que a maior japonesa da mesma instrução**: o jogo pode copiá-las para um campo
  de tamanho fixo, então o `check` não deixa.
- **Tabelas sem conversor**: títulos das rotas de batalha (`BattleRouteTitle.dat`), blog.
- **Letras de 2 bytes no `charset`** (alfabetos grandes), acima.
