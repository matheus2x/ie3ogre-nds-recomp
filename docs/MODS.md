# Mods

Cada mod é **uma pasta ou um `.zip`** dentro da pasta `mods/`, ao lado do `boej.exe` e do launcher. Para
adicionar um mod, basta colocá-lo lá: o launcher mostra os mods dessa pasta na página **Mods**, onde cada um é
ligado, desligado e ordenado. O jogo lê os arquivos do mod no lugar dos originais, direto da pasta ou de dentro do
`.zip` (sem extrair); o `boej.pak` nunca é alterado.

## O que um mod pode mudar

| Pode | Como |
|---|---|
| Qualquer arquivo do NitroFS (`data_iz/...`): textos, fontes, imagens, modelos, sons, vídeos, tabelas | `files/<caminho no jogo>`, o arquivo inteiro; o tamanho pode mudar |
| Dados das overlays (textos e tabelas que ficam dentro do código, como os menus) | `overlays/ovNNN.bin`, a overlay como fica na RAM (descomprimida) |
| Os mesmos, só com a diferença | `files/<caminho>.bps` ou `overlays/ovNNN.bin.bps`: um patch BPS sobre o arquivo original (ver **Patches** abaixo) |

| Não pode (por enquanto) | Por quê |
|---|---|
| Código do jogo (ARM9, código das overlays) | o código foi recompilado para o PC; mudar os bytes dele não teria efeito. O jogo confere isso e recusa o mod |
| A overlay 129 | é a proteção contra cópia, cifrada na ROM |
| O ARM7, o banner e o cabeçalho | o ARM7 é reimplementado no PC; os outros não são lidos pelo jogo |

## Estrutura

```
boej.exe
ie3-launcher.exe
mods/
  fontes-maiores.zip                     um mod em .zip (mesmo conteúdo da pasta abaixo)
  minha-traducao/                        um mod em pasta
    mod.ini
    files/
      data_iz/logic/CapsuleComment.dat   substitui /data_iz/logic/CapsuleComment.dat
      data_iz/font/FONT12.NFTR.bps       patch sobre o /data_iz/font/FONT12.NFTR original
    overlays/
      ov022.bin                          overlay 22 inteira, descomprimida
      ov023.bin.bps                      patch sobre a overlay 23 descomprimida
```

`mod.ini`:

```ini
[mod]
name = Minha tradução
version = 1.0
author = Seu nome
description = O que o mod faz, numa frase ou duas.
game = BOEJ
```

Só `game` é conferido (se existir, tem que ser `BOEJ`); o resto aparece no launcher. O nome da pasta ou do
`.zip` identifica o mod e não pode ter vírgula. Arquivos ao lado do `mod.ini` (um leia-me, uma imagem) são
ignorados.

**Mod em `.zip`.** O zip tem o mesmo conteúdo da pasta: o `mod.ini` na raiz do zip ou dentro de uma única pasta
(é o que o `tools/mod.py pack` gera e o que o Windows faz ao "Compactar" uma pasta). Compressão "Deflate" ou
nenhuma; zip com senha ou zip64 (acima de 4 GB) não são aceitos. Os arquivos comprimidos são descomprimidos na
memória logo ao abrir o jogo, em segundo plano (a tradução de fãs, 77 MB, leva menos de 1 s); um mod em pasta
é lido direto do disco. Para criar e testar um mod, a pasta é mais prática; para distribuir, o `.zip`.

**Ordem.** Os mods ligados ficam em `boej.ini`, na ordem da lista do launcher:

```ini
[mods]
enabled = minha-traducao, fontes-maiores.zip
```

Quando dois mods trocam o mesmo arquivo (ou a mesma overlay), vale o que vem depois, e o launcher mostra um
aviso no outro. Para testes sem mexer no `boej.ini`, a variável `NTR_MODS=pasta1,mod2.zip` tem prioridade.

**Overlays.** O arquivo é a overlay inteira, como o jogo a coloca na RAM. Só os bytes fora do código podem
mudar: o jogo compara o mod com a overlay original nos trechos de que o código recompilado depende (corpos das
funções, literal pools e tabelas de salto, gravados pelo `ntr-recomp` no `registry.cpp`) e recusa o mod se algum
byte ali mudou, dizendo o endereço. Uma overlay menor que a original é completada com os bytes originais; uma
maior aumenta o espaço da overlay (até 64 KiB a mais).

**Patches.** Um arquivo `<caminho>.bps` no lugar do arquivo inteiro é um patch [BPS](https://www.romhacking.net/documents/746/)
(o formato do beat e do Floating IPS) aplicado sobre o arquivo original do jogo; numa overlay, sobre ela
descomprimida. O mod só leva o que mudou (textos traduzidos, pixels editados), e não os dados do jogo: é o formato
para distribuir um mod. O patch confere o CRC-32 do original, do resultado e de si mesmo; um patch feito para outro
arquivo, ou danificado, dá erro e deixa o mod de fora. O tamanho do arquivo pode mudar.

Num mesmo mod, um caminho tem o arquivo inteiro ou o patch, não os dois. O patch é sempre sobre o original do
jogo, nunca sobre o arquivo de outro mod (o último mod da lista que troca o arquivo vale, como sempre).

Os patches são aplicados ao abrir o jogo, em segundo plano, como os arquivos comprimidos de um `.zip`. A tradução
inteira (512 arquivos, 9 MB de patches para 76 MB de arquivos) custa uns 0,1 s a mais no começo.

**Mod com erro** fica de fora inteiro (o jogo abre sem ele). Arquivos com caminho que o jogo não tem são
ignorados, com um aviso. Os erros e avisos aparecem na página Mods e no log da sessão em `logs/sessoes/` (linhas `[mods]`).

**Savestates** guardam quais mods estavam em uso: um slot salvo com outros mods (ou com outra versão de um
arquivo do mod) não abre, porque o jogo já tinha lido os arquivos antigos.

## Ferramentas

### Launcher (página Mods)

- A lista mostra o que está na pasta `mods/` e se atualiza sozinha quando algo é colocado ou tirado de lá.
- **Adicionar mod (.zip):** copia o `.zip` para a pasta `mods/` (o mesmo que copiar à mão). Também dá para
  arrastar um `.zip` ou a pasta de um mod para a janela. Um mod com o mesmo nome é substituído.
- **Criar de um dump modificado:** escolha um `.nds` de uma tradução ou hack. O launcher compara com o jogo
  original (`boej.pak`) e cria um mod com os arquivos e overlays que mudaram. Só dá certo se o código do dump
  for o do retail (o caso das traduções que só mudam textos, fontes e imagens).
- **Remover** (na linha do mod ou nos detalhes, com confirmação) apaga a pasta ou o `.zip` de dentro de `mods/`.
- Adicionar e remover mexem só na cópia que está em `mods/`: o `.zip` ou a pasta de onde o mod foi adicionado
  nunca é alterado nem apagado. A página e a confirmação de remover dizem isso.
- Ligar/desligar e mudar a ordem com as setas.

Pela linha de comando: `ie3-launcher --install-mod mod.zip` e `ie3-launcher --mod-from-dump hack.nds`.
O próprio jogo faz a checagem com `boej --check-mods` (uma linha por mod e por problema).

### `tools/mod.py` (Python 3.12+)

```sh
# pasta vazia com o mod.ini
python3 tools/mod.py new mods/minha-traducao --name "Minha tradução" --author "Eu"

# arquivos originais para editar (o retail pode ser o boej.pak ou o .nds)
python3 tools/mod.py unpack boej.pak build/original --only 'data_iz/logic/*'
python3 tools/mod.py unpack boej.pak build/original --overlays

# mod a partir de um dump modificado (o mesmo que o botão do launcher); --patch: só patches .bps
python3 tools/mod.py from-dump boej.pak hack.nds mods/hack --name "Hack" --patch

# arquivos inteiros do mod viram patches (um arquivo igual ao original sai, com o patch dele)
python3 tools/mod.py patch mods/hack boej.pak

# cópia do mod com os patches aplicados (arquivos inteiros), para editar
python3 tools/mod.py expand mods/hack boej.pak build/hack

# procura problemas (caminhos que não existem, overlays com código mudado); pasta ou .zip
python3 tools/mod.py check mods/minha-traducao boej.pak

# .zip para distribuir (é só colocar na pasta mods/)
python3 tools/mod.py pack mods/minha-traducao
```

O `check` do Python usa os tamanhos de função do ds-decomp (`config/boej/dsd`); a checagem que vale é a do
jogo, que usa exatamente o que o recompilador usou.

### Descobrir qual arquivo mudar

O log da sessão (`logs/sessoes/<data e hora>.log`) registra cada arquivo que o jogo abre, com o número do quadro e a hora:

```
  12345 14:03:12| [fs] open /data_iz/logic/CapsuleComment.dat
```

Jogue até a cena que interessa, aperte **F8** (marca o momento no log) e veja os últimos arquivos abertos.

## Fluxo de trabalho

1. `tools/mod.py unpack` para ter os originais; edite com as ferramentas do formato.
2. Coloque só os arquivos alterados em `mods/<seu-mod>/files/` (mesmo caminho do jogo).
3. Ligue o mod no launcher e abra o jogo. Não precisa recompilar nada: o mod é lido ao abrir o jogo.
4. Confira a página Mods (ou o log da sessão) se algo não aparecer.
5. `tools/mod.py patch` para trocar os arquivos inteiros por patches e `tools/mod.py pack` (ou "Compactar" no
   Windows) para gerar o `.zip` que os jogadores colocam na pasta `mods/`.

**Distribuição.** Um mod com arquivos inteiros leva dados do jogo (o arquivo original modificado); com patches,
só o que mudou. Distribua mods em patches. O "Criar de um dump modificado" do launcher gera arquivos inteiros,
para uso no próprio PC.

## Mods oficiais

A pasta [`mods/`](../mods/) do repositório tem os mods mantidos pelo projeto, um por pasta, só com patches. Cada um
sai na release como `mod-<nome>-<version>.zip` (`tools/release.py`). O nome do arquivo muda a cada versão, mas o
launcher reconhece o mesmo mod pelo `name` do `mod.ini`: adicionado pelo launcher, o novo apaga a cópia antiga da
pasta `mods` e fica no lugar dela na lista (ligado, se ela estava); colocado à mão, uma versão mais nova fica no
lugar da antiga, que continua na pasta, desligada. A versão é o `version` do `mod.ini`: aparece no nome do
`.zip`, na seção "Mods oficiais" das notas (com "atualizado nesta versão" quando o mod mudou) e, no launcher, na
lista e nos detalhes da página Mods. A release é recusada se um mod oficial tiver um arquivo inteiro do jogo ou se
tiver mudado desde a release anterior sem um `version` novo.

| Mod | O que é |
|---|---|
| [`CyberLifePlus`](../mods/CyberLifePlus/LEIA-ME.md) | Tradução PT-BR, feita originalmente pelo grupo CyberLife e continuada aqui. O `LEIA-ME.md` dele tem os créditos e como continuar a tradução |

## Tipos de mod planejados

O que já dá para fazer com a troca de arquivos, e o que ainda falta no port para cada tipo:

| Tipo | Hoje | Falta |
|---|---|---|
| **Tradução** | sim: textos (`data_iz/logic`, `data_iz/movie/txt`), fontes (`data_iz/font`), imagens com texto e strings das overlays. Uma tradução já pronta em `.nds` vira mod pelo botão "Criar de um dump modificado" (ou `tools/mod.py from-dump --patch`) e roda igual ao dump traduzido (testado byte a byte com uma tradução de fãs, em arquivos inteiros e em patches) | documentar os formatos de texto e fonte e ter conversores para texto editável em `tools/` |
| **Troca de sprites e imagens** | sim, trocando o arquivo (`data_iz/pic2d`, `obj2d`, `spr`, `face2d`, `pic3d`) no formato do jogo | documentar os formatos e ter conversores para PNG |
| **Texturas em alta resolução (upscaling)** | não: um arquivo do jogo só pode ter o tamanho que cabe na VRAM do DS | pacotes de texturas por hash (`textures/` no mod), aplicados pelo renderizador 3D na resolução interna e pela composição 2D (plano em [`REMASTER.md`](REMASTER.md)) |
| **Gerenciador de técnicas (estilo Mini Hack)** | em parte: as tabelas de jogadores e técnicas são arquivos e podem ser trocadas inteiras | mapear o formato das tabelas e fazer um editor que gere a pasta do mod |

Próximos passos da infraestrutura, em ordem:

1. ~~Patches por arquivo~~ (feito: `.bps`, ver **Patches** em Estrutura).
2. **Formatos documentados** e conversores em `tools/` (texto, fonte, imagem, tabelas).
3. **Pacotes de texturas** para o upscaling.
4. **Editor de técnicas** gerando um mod.
5. Mods que mudam comportamento (ganchos em C++ chamados pelo código recompilado), a longo prazo.

## Como funciona por dentro

`runtime/src/mods.cpp`: ao abrir, o jogo lê os mods ligados, resolve cada caminho pela FNT do NitroFS e coloca
os arquivos do mod em endereços da ROM depois do fim da imagem. Uma cópia da FAT (e, para overlays, da tabela de
overlays, com a overlay marcada como não comprimida) aponta o jogo para eles (`rom_override` em `rom.cpp`). O
jogo lê os arquivos do mod pelos mesmos caminhos de sempre, sem saber que vieram de fora. Pastas e zips passam
pelo `ModPackage` (`runtime/src/package.cpp`, com um leitor de zip e um inflate próprios, conferidos pelo CRC-32).
Os patches BPS são aplicados pelo `bps_apply` (no mesmo arquivo) sobre os bytes originais do `boej.pak`; o
`tools/ntr/bps.py` gera e aplica os mesmos patches em Python.
Os arquivos de uma pasta e os guardados sem compressão num zip ficam mapeados em memória; os comprimidos são
descomprimidos por uma thread em segundo plano, ou na hora, se o jogo pedir um antes dela.
