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

## Como continuar a tradução

```sh
# 1. o mod com os arquivos inteiros (traduzidos), para editar e testar
python3 tools/mod.py expand mods/CyberLifePlus boej.pak build/CyberLifePlus

# 2. edite os arquivos em build/CyberLifePlus/files/ (para um arquivo que a tradução ainda não muda,
#    pegue o original com tools/mod.py unpack e coloque-o lá); teste copiando a pasta para mods/ do jogo

# 3. de volta para patches, no repositório
cp -r build/CyberLifePlus/files build/CyberLifePlus/overlays mods/CyberLifePlus/
python3 tools/mod.py patch mods/CyberLifePlus boej.pak
python3 tools/mod.py check mods/CyberLifePlus boej.pak
```

No passo 3, um arquivo inteiro vale mais que o patch do mesmo caminho (é a edição nova) e o substitui; um
arquivo igual ao original é apagado. Suba o `version` no `mod.ini` a cada versão nova.
