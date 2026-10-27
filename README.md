# ie3ogre-recomp

Port nativo para PC de **Inazuma Eleven 3: Sekai heno Chousen!! The Ogre** (Nintendo DS) por
**recompilação estática** ARM → C++.

O código do jogo não é emulado: o ARM9 e as overlays viram C++ e são compilados para x86-64. O hardware do DS e o
ARM7 são reimplementados no PC.

> **Estado: pré-alfa.** O jogo roda com vídeo 2D/3D, som, vídeos e controles, e a história já foi jogada até
> depois da primeira partida. Ainda há bugs. O histórico está em [`docs/DIARY.md`](docs/DIARY.md).

## Como jogar

As releases não trazem nenhum dado nem código do jogo: o launcher prepara tudo no seu PC a partir do dump do
**seu** cartucho.

1. Baixe o `ie3ogre-recomp-<versão>-windows.zip` da [última release](../../releases/latest) e descompacte numa
   pasta em que dê para gravar.
2. Abra o `ie3-launcher.exe` e escolha o dump (ou arraste o `.nds` para a janela).
3. O launcher confere o dump, cria o `boej.pak` (os dados do jogo), baixa uma vez o compilador
   [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (~190 MB) e compila o `boej.exe`. Leva de 1 a alguns
   minutos, conforme o processador. Depois é só clicar em **Jogar**: o dump não é mais necessário.

Outras abas do launcher: **Configurações** (vídeo, áudio e controles), **Mods** e **Atualizações** (baixa a versão
nova e recompila sem pedir o dump; saves, configurações e mods ficam como estão).

### Dump suportado

| Jogo | Código | SHA-1 do `.nds` |
|---|---|---|
| Inazuma Eleven 3: Sekai heno Chousen!! The Ogre (Japão) | `BOEJ` | `9380f1d7ee75e7c6ef24f1b76c88ecdc85088957` |

Só o dump retail, sem modificações. As outras versões de IE3 (Bomber, Spark e a europeia, de 3DS) não são
suportadas. Se o seu dump tiver outro SHA-1, abra uma issue com o hash.

### Controles

| DS | Teclado | Controle |
|---|---|---|
| Direcional | setas | direcional ou analógico esquerdo |
| A / B / X / Y | X / Z / S / A | botões da direita (pela posição do DS) |
| L / R | Q / W | ombros |
| Start / Select | Enter / Shift direito ou Backspace | Start / Back |
| Tela de toque | mouse na tela de baixo | — |

| Atalho | Tecla |
|---|---|
| Menu de pausa (configurações, savestates, reiniciar, sair) | Esc |
| Avanço rápido (segurar / ligar e desligar) | Tab / F2 |
| A automático (segurar / ligar e desligar) | C / F3 |
| Savestate: salvar / carregar / trocar o slot (1 a 5) | F5 / F9 / F6 e F7 |
| Tela cheia / captura de tela | F11 / F12 |

Tudo pode ser remapeado no launcher ou no menu de pausa. As configurações ficam no `boej.ini`, na pasta do jogo.

### Mods

Mods trocam arquivos do jogo (textos, fontes, imagens, tabelas) sem alterar o `boej.pak`. Cada mod é uma pasta
ou um `.zip` dentro da pasta `mods/`, ao lado do `boej.exe`: coloque o mod lá e ligue-o na aba **Mods** do
launcher, onde também dá para ordenar e remover. Como fazer um mod: [`docs/MODS.md`](docs/MODS.md).

Mods oficiais (pasta [`mods/`](mods/) do repositório) saem como `.zip` separados em cada release. Eles só trazem
patches, sem dados do jogo:

| Mod | O que é |
|---|---|
| [`CyberLifePlus`](mods/CyberLifePlus/LEIA-ME.md) | Tradução PT-BR. Feita originalmente pelo grupo **CyberLife** e continuada aqui, completando o que faltou |

### Relatando problemas

Se algo der errado, aperte **F8** na hora, feche o jogo e envie, da pasta `logs` do jogo (botão "Abrir a pasta
de logs" no launcher), o log da sessão em `logs/sessoes/` e o replay de mesmo nome em `logs/replays/` (`.txt` e
`.sav`: a sessão gravada, que permite repetir o problema exatamente). Cada abertura do jogo é uma sessão com nome
de data e hora; as 20 mais recentes ficam guardadas, e o `logs/historico.txt` resume todas.

## Funcionalidades

- [x] Recompilador ARMv5TE/Thumb → C++ (`ntr-recomp`): 16.057 funções, ARM9 + ITCM + 133 overlays
- [x] Runtime: memória, IRQ, timers, DMA, overlays, threads em fibers; BIOS, NitroSDK e ARM7 em HLE
- [x] Vídeo 2D e 3D por software, com resolução interna do 3D de 1× a 10×, anti-aliasing e wireframe
- [x] Áudio (16 canais a 32 kHz) e vídeos Mobiclip
- [x] Save, savestates, avanço rápido, tela larga 16:9 e controles remapeáveis
- [x] Launcher sem nada do jogo: prepara, configura, atualiza e gerencia mods
- [x] Mods: troca de arquivos e dos dados das overlays, inteiros ou como patches (BPS) sem dados do jogo
- [ ] Mods: formatos documentados, texturas em alta resolução, editor de técnicas
- [x] Wi-Fi desativado (o Nintendo WFC foi encerrado)

## Desenvolvimento

Requisitos: Python 3.12+, CMake 3.28+, um compilador C++20 e SDL3.

```sh
python3 tools/extract.py retail.nds        # uma vez: confere o dump e cria build/boej.pak
python3 tools/analyze.py dsd retail.nds    # uma vez: mapa de funções (ds-decomp)
python3 tools/build.py                     # recompila e compila build/cmake-rel/game/boej
./build/cmake-rel/game/boej
python3 -m unittest discover -s tools/tests -t tools   # testes (não precisam da ROM)
```

Estrutura do repositório, build para Windows, release, análise com Ghidra e variáveis de ambiente para testes:
[`docs/DESENVOLVIMENTO.md`](docs/DESENVOLVIMENTO.md).

## Referências

- [FFCCEOT-Recomp](https://github.com/JonathanDC64/FFCCEOT-Recomp): recompilação estática de um jogo de DS; modelo de arquitetura deste projeto
- [gen1recomp](https://github.com/bryanthaboi/gen1recomp): modelo das releases com launcher
- [khdays-port](https://github.com/Yokimitsuro/khdays-port): port nativo de um jogo de DS a partir de decomp
- [melonDS](https://github.com/melonDS-emu/melonDS) e [GBATEK](https://problemkaputt.de/gbatek.htm): referências de hardware

## Contribuindo

- Nunca envie ROMs, dumps, arquivos extraídos ou trechos de código do jogo (o `.gitignore` bloqueia os casos comuns).
- Registre descobertas e mudanças relevantes em `docs/DIARY.md`.

## Legal

Projeto de fãs, sem afiliação com LEVEL-5 ou Nintendo. *Inazuma Eleven* é marca da LEVEL-5 Inc. Para jogar, é
preciso ter uma cópia legítima do jogo. O `boej.pak` e o `boej.exe` gerados contêm dados e código do jogo: são de
uso pessoal e não devem ser distribuídos.
