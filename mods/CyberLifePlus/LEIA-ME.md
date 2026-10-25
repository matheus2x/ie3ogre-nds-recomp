# CyberLifePlus — tradução PT-BR

Tradução de Inazuma Eleven 3 - Sekai e no Chousen!! The Ogre para o português do Brasil, como mod do
ie3ogre-recomp.

## Créditos

- **Tradução original: grupo CyberLife.** Todo o trabalho da tradução até a versão 1.0 deste mod é deles:
  textos, fontes e imagens traduzidas. A tradução está há meses sem atualizações, e este mod continua de onde
  ela parou.
- **Continuação:** matheus2x, completando os pontos que ficaram sem tradução.

Se você é do CyberLife e quer que algo nestes créditos mude (nomes, links, como a tradução é citada), abra uma
issue no repositório.

## Como usar

Coloque o `mod-CyberLifePlus-<versão>.zip` da release na pasta `mods/`, ao lado do jogo, e ligue o mod na aba
**Mods** do launcher. Para atualizar, adicione o `.zip` novo pelo launcher: ele apaga a versão antiga e deixa a
nova ligada no lugar dela.

O mod só traz **patches** (`.bps`, só a diferença em relação ao jogo original): ele não contém dados do jogo e
precisa do dump retail, o mesmo que o launcher pede.

## Origem

A versão 1.0 foi feita com `tools/mod.py from-dump --patch` a partir do dump com a tradução do CyberLife
(SHA-1 `a70e9a336f6ea8ff78003938dcd024533f034a68`) sobre o retail
(SHA-1 `9380f1d7ee75e7c6ef24f1b76c88ecdc85088957`): 512 arquivos e 32 overlays. Aplicado ao retail, o mod
dá exatamente os arquivos do dump traduzido.

## Versões

- **1.6:** os textos das imagens da galeria em português: o botão "Extras" (antes "Gallery") no menu de
  continuar, a lista (Vídeos, Gerar senha, Ligação com celular, Ligação com o filme, Coleção, Surpresa), a
  tela dos vídeos ("Escolha o vídeo que quer ver.", Ver, Sair), a tela de gerar senha e a da ligação com o
  filme (botões, perguntas e Sim/Não). Também o aviso sobre saves alterados que aparece ao carregar o jogo
  ("Alterar os dados salvos com ferramentas de modificação..."), que estava em inglês.
- **1.5:** o que ainda estava em japonês nas tabelas do jogo: os objetivos das minipartidas ("Roube a bola
  do adversário!", agora todos em português, antes metade em inglês), as descrições das quatro técnicas que
  mudam o elemento do jogador (Kaze/Hayashi/Hi/Yama no Gotoshi), os prêmios do mapa dos jogos-treino, os
  times das condições dos jogadores premium e um nome de time do fichário (Kasamino).
  Também os nomes de time e de prêmio dessas tabelas que estavam em inglês ("Neo Japan" → "Novo Japão",
  "Women Selection" → "Seleção Feminina", "Benchers" → "Reservas"), seguindo os nomes do diálogo.
  As descrições das técnicas (lista de técnicas e fichário) e as condições das rotas de batalha em português.
  **Corrige as descrições de itens e táticas da 1.2, que não apareciam no jogo**: a janela só mostrava os
  acentos. Agora elas são gravadas na forma que a janela lê.
- **1.4:** os títulos das cenas da galeria de vídeos (Continuar → Gallery → Game Video) em português: 58
  títulos, com os nomes de personagens e estádios das falas.
  Também os nomes de quem fala em duas partidas que ainda apareciam em japonês (Hibiki, Tae, Ichinose).
  E os menus e mensagens que ficavam no código do jogo: comunicação (DS sem fio, Wi-Fi, trocas, Passing),
  avisos da formação, quiz, scout e itens premium (312 textos).
  Corrige as legendas dos vídeos, que perderam os espaços entre as palavras na 1.3.
- **1.3:** os objetivos da tela de cima (a faixa "Objetivo") em português: 102, uns 80 deles ainda estavam em
  inglês. As letras não ficam mais umas sobre as outras ("aconteceu", "averiguar"): o texto ganha espaçadores
  de 1 px onde o jogo troca de sprite.
- **1.2:** itens e táticas em português: os nomes dos itens (antes em inglês), as descrições de todos os itens
  e as descrições das táticas. Os nomes das técnicas não são traduzidos, mas foram revistos para o romaji
  ("Sabaki no Tettsui" no lugar de "Divine Stamp") e são os mesmos na lista de técnicas e nos cadernos.
- **1.1:** a história fica toda em português: o que faltava dos capítulos 1 a 11, o capítulo 12 (o epílogo com
  o Tenma), as mensagens de campo e das lojas, a máquina de busca de jogadores e os avisos de recrutamento
  (1.268 falas). O que ainda falta está em `docs/TRADUCAO.md`, no repositório.
- **1.0:** a tradução do CyberLife, como estava.

## Como continuar a tradução

O texto traduzido fica em `translations/CyberLifePlus/` no repositório: só o lugar de cada fala e o português,
sem o texto do jogo. O `tools/text.py` lista o que falta e grava a tradução no mod como patches:

```sh
# o que ainda está em japonês, com o espaço de cada fala (o texto do jogo só vai para build/)
python3 tools/text.py extract boej.pak mods/CyberLifePlus build/text --only 3300

# como um nome ou termo já foi traduzido nas falas prontas
python3 tools/text.py lookup build/text 円堂 ゴッドハンド

# escreva as falas em translations/CyberLifePlus/<grupo>.json, {"evet:33000301:4": "Alô?", ...}, e então:
python3 tools/text.py check boej.pak mods/CyberLifePlus translations/CyberLifePlus
python3 tools/text.py apply boej.pak mods/CyberLifePlus translations/CyberLifePlus
python3 tools/text.py survey boej.pak mods/CyberLifePlus     # quanto já está traduzido
```

O `text.py` também cobre os itens (`item.dat`, `item.STR`), as táticas (`tacticscmd.STR`), os nomes das
técnicas (`command.dat`/`command.STR`), os títulos da galeria de vídeos (`movie_view_f.dat`) e os textos dos
overlays (menus no código). O texto desenhado nas imagens fica em `translations/CyberLifePlus/images.json`
(o que apagar e o que escrever, com a fonte do jogo) e vai para o mod com o `tools/image.py` (`export` mostra as
imagens em PNG, só em `build/`; `check` e `apply` como os do `text.py`). Para o resto (outras tabelas, fontes),
use `tools/mod.py expand`, edite
os arquivos inteiros e volte com `tools/mod.py patch`. Um arquivo inteiro vale mais que o patch do mesmo caminho e o substitui; um arquivo
igual ao original é apagado. Suba o `version` no `mod.ini` a cada versão nova. As regras do texto (espaço,
acentos, quebra de linha, nomes) estão em `docs/TRADUCAO.md`.
