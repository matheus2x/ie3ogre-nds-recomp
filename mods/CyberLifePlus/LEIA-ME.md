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

Coloque o `CyberLifePlus.zip` da release na pasta `mods/`, ao lado do jogo, e ligue o mod na aba **Mods** do
launcher. Para atualizar, troque o `.zip` pelo novo (com o mesmo nome, o mod continua ligado).

O mod só traz **patches** (`.bps`, só a diferença em relação ao jogo original): ele não contém dados do jogo e
precisa do dump retail, o mesmo que o launcher pede.

## Origem

A versão 1.0 foi feita com `tools/mod.py from-dump --patch` a partir do dump com a tradução do CyberLife
(SHA-1 `a70e9a336f6ea8ff78003938dcd024533f034a68`) sobre o retail
(SHA-1 `9380f1d7ee75e7c6ef24f1b76c88ecdc85088957`): 512 arquivos e 32 overlays. Aplicado ao retail, o mod
dá exatamente os arquivos do dump traduzido.

## Versões

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

O `text.py` também cobre os itens (`item.dat`, `item.STR`), as táticas (`tacticscmd.STR`) e os nomes das
técnicas (`command.dat`/`command.STR`). Para o resto
(outras tabelas, imagens, fontes), use `tools/mod.py expand`, edite os arquivos inteiros e volte
com `tools/mod.py patch`. Um arquivo inteiro vale mais que o patch do mesmo caminho e o substitui; um arquivo
igual ao original é apagado. Suba o `version` no `mod.ini` a cada versão nova. As regras do texto (espaço,
acentos, quebra de linha, nomes) estão em `docs/TRADUCAO.md`.
