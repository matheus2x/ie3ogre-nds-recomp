<p align="center">
  <img src="docs/assets/ie3-ogre-logo.png" alt="Inazuma Eleven 3: Sekai heno Chousen!! The Ogre" width="600">
</p>

<h1 align="center">Inazuma Eleven 3: The Ogre — Recomp</h1>

<p align="center">
  <a href="README.md">English</a> · <b>Português</b>
</p>

<p align="center">
  <a href="../../releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/matheus2x/ie3ogre-nds-recomp?label=release&style=flat-square&labelColor=30363d&color=57606a"></a>
  <img alt="Windows x64 · Linux (compilando)" src="https://img.shields.io/badge/plataforma-Windows_x64_%C2%B7_Linux_%28compilando%29-57606a?style=flat-square&labelColor=30363d&color=57606a">
  <a href="LICENSE"><img alt="Licença MIT" src="https://img.shields.io/badge/licen%C3%A7a-MIT-57606a?style=flat-square&labelColor=30363d&color=57606a"></a>
</p>

<p align="center">
  Port nativo para PC de <b>Inazuma Eleven 3: Sekai heno Chousen!! The Ogre</b> (Nintendo DS)<br>
  por <b>recompilação estática</b> ARM → C++. Sem emulador: o código do jogo vira um programa de PC.
</p>

<p align="center">
  <a href="#o-que-o-port-acrescenta">O que acrescenta</a> ·
  <a href="#como-jogar">Como jogar</a> ·
  <a href="#controles">Controles</a> ·
  <a href="#mods-e-a-tradução-pt-br">Mods</a> ·
  <a href="#compilando">Compilando</a> ·
  <a href="docs/DESENVOLVIMENTO.md">Desenvolvimento</a> ·
  <a href="#relatando-problemas">Problemas</a>
</p>

> [!WARNING]
> **Pré-alfa.** O jogo roda com vídeo 2D/3D, som, vídeos e controles, e a história já foi jogada até depois da
> primeira partida, mas ainda há bugs. Faça cópias dos seus saves e relate os problemas nas
> [issues](../../issues).

> [!IMPORTANT]
> **O jogo não vem incluído.** As releases não trazem dados nem código do jogo: o launcher prepara tudo no seu PC a
> partir do dump do **seu** cartucho. Projeto de fãs, sem fins lucrativos e sem afiliação com a LEVEL-5 ou a
> Nintendo.

> [!NOTE]
> O launcher e o menu de pausa abrem em inglês: para usar em português, escolha **Português (Brasil)** em
> "Language / Idioma", no canto da primeira aba do launcher. O texto do jogo vem da tradução (abaixo).

> [!NOTE]
> Ferramentas de IA ajudam no desenvolvimento deste projeto. A direção, as decisões técnicas e os testes são do
> mantenedor, que revisa cada mudança antes de publicá-la.

## O que o port acrescenta

Tudo o que o cartucho faz, e mais:

| | No DS | Neste port |
|---|---|---|
| 🎨 **Gráficos** | 3D em 256×192 | 3D de **1× a 10×** com anti-aliasing; tela **16:9** (a de baixo grande e as duas pequenas ao lado) ou telas empilhadas |
| 💾 **Progresso** | o save do jogo | **5 savestates** e **rewind** de até 30 s |
| 🏆 **Conquistas** | — | **36 conquistas locais** por save (capítulos, nível do time, elenco, o capitão de cada time enfrentado), com aviso no jogo, página no menu de pausa e aba no launcher. Nada online: cada save tem um `.achievements.json` ao lado |
| ⏩ **Velocidade** | normal | **avanço rápido** de 2× a 10× e A automático |
| 🎮 **Controles** | botões e caneta | teclado, mouse na tela de toque e **controles**, tudo remapeável |
| ⚙️ **Interface** | — | **menu de pausa** (configurações, savestates, conquistas) e um **launcher** que prepara, atualiza e cuida dos saves e mods, em português ou inglês |
| 🧩 **Conteúdo** | o cartucho | **mods** em pasta ou `.zip`, inclusive a tradução PT-BR |
| 🐞 **Relatar problemas** | — | log, replay exato e histórico de cada sessão (F8 marca o momento) |

O Nintendo WFC (Wi-Fi) foi encerrado e fica desativado.

## Como jogar

**Requisitos:** Windows 10 ou 11 (x64), o dump do seu cartucho (abaixo) e internet na primeira vez, para baixar o
compilador.

1. Baixe o `ie3ogre-recomp-<versão>-windows.zip` da [última release](../../releases/latest) e descompacte numa
   pasta em que dê para gravar (Documentos, por exemplo; evite "Arquivos de Programas").
2. Abra o `ie3-launcher.exe` e escolha o dump, ou arraste o `.nds` para a janela.
3. O launcher confere o dump, cria o `boej.pak` (os dados do jogo, ~450 MB), baixa uma vez o compilador
   [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (~190 MB) e compila o `boej.exe`. Leva de 1 a alguns
   minutos, conforme o processador. Depois é só clicar em **Jogar**: o dump não é mais necessário.

As outras abas do launcher:

| Aba | O que faz |
|---|---|
| **Configurações** | vídeo, áudio, controles e o idioma do launcher (também no menu de pausa do jogo, com Esc) |
| **Saves** | escolhe o save que o jogo usa, cria um save novo e importa `.sav` do cartucho ou de emuladores |
| **Mods** | adiciona, liga, ordena e remove mods; cria um mod a partir de um dump modificado |
| **Conquistas** | as conquistas de cada save, com a data em que foram desbloqueadas; também confere um save antigo ou importado |
| **Atualizações** | baixa a versão nova e recompila o jogo sem pedir o dump; saves, configurações e mods ficam como estão |

Tudo fica na pasta do launcher: `saves/` (com as conquistas de cada save), `savestates/`, `mods/`, `logs/` e o
`boej.ini` (configurações). O [`LEIA-ME.txt`](launcher/LEIA-ME.txt) que vai na release explica cada um.

### Dump suportado

| Jogo | Código | SHA-1 do `.nds` |
|---|---|---|
| Inazuma Eleven 3: Sekai heno Chousen!! The Ogre (Japão) | `BOEJ` | `9380f1d7ee75e7c6ef24f1b76c88ecdc85088957` |

Só o dump retail, sem modificações (para uma tradução em `.nds`, veja [Mods](#mods-e-a-tradução-pt-br)). As
outras versões de IE3 (Bomber, Spark e a europeia, de 3DS) não são suportadas. Se o seu dump tiver outro SHA-1,
abra uma issue com o hash.

## Controles

| DS | Teclado | Controle |
|---|---|---|
| Direcional | setas | direcional ou analógico esquerdo |
| A / B / X / Y | X / Z / S / A | botões da direita (pela posição do DS) |
| L / R | Q / W | ombros |
| Start / Select | Enter / Shift direito ou Backspace | Start / Back |
| Tela de toque | mouse na tela de baixo | — |

| Atalho | Tecla |
|---|---|
| Menu de pausa (configurações, savestates, conquistas, reiniciar, sair) | Esc |
| Avanço rápido (segurar / ligar e desligar) | Tab / F2 |
| A automático (segurar / ligar e desligar) | C / F3 |
| Savestate: salvar / carregar / trocar o slot (1 a 5) | F5 / F9 / F6 e F7 |
| Voltar no tempo (rewind, segurar; até 30 s por padrão) | R |
| Marcar um problema no log | F8 |
| Tela cheia / captura de tela | F11 / F12 |

Tudo pode ser remapeado no launcher ou no menu de pausa. Os savestates guardam também o código recompilado em
execução: só abrem no mesmo `boej.exe` e com os mesmos mods (uma atualização recompila o jogo).

## Mods e a tradução PT-BR

Mods trocam arquivos do jogo (textos, fontes, imagens, tabelas) sem alterar o `boej.pak`. Cada mod é uma pasta
ou um `.zip` na pasta `mods/`: coloque o mod lá (ou arraste-o para o launcher) e ligue-o na aba **Mods**. Como
fazer um mod: [`docs/MODS.md`](docs/MODS.md).

Os mods oficiais ficam na pasta [`mods/`](mods/) e saem como `.zip` separados em cada release, só com patches,
sem dados do jogo:

| Mod | O que é |
|---|---|
| [`CyberLifePlus`](mods/CyberLifePlus/LEIA-ME.md) | Tradução PT-BR. Feita originalmente pelo grupo **CyberLife** e continuada aqui: a história está toda em português, e o que falta está em [`docs/TRADUCAO.md`](docs/TRADUCAO.md) |

## Relatando problemas

Se algo der errado, aperte **F8** na hora, feche o jogo e abra uma [issue](../../issues) com:

- o que aconteceu e o que você esperava;
- da pasta `logs` (botão "Abrir a pasta de logs" no launcher): o log da sessão em `logs/sessoes/` e o replay de
  mesmo nome em `logs/replays/` (o `.txt` e o `.sav`, que repetem a sessão exatamente);
- a versão (aba Atualizações) e os mods ligados.

Cada abertura do jogo é uma sessão com nome de data e hora; as 20 mais recentes ficam guardadas, e o
`logs/historico.txt` resume todas.

## Como funciona

O ARM9 e as 133 overlays do jogo são traduzidos para C++ pelo `ntr-recomp` (16.057 funções) e compilados para
x86-64. O que era hardware do DS (vídeo 2D e 3D, som, toque, cartucho) e o ARM7 são reimplementados no PC, junto
com as partes do BIOS e do NitroSDK que o jogo usa.

- [x] Recompilador ARMv5TE/Thumb → C++ (`ntr-recomp`): ARM9, ITCM e 133 overlays
- [x] Runtime: memória, IRQ, timers, DMA, overlays e threads em fibers; BIOS, NitroSDK e ARM7 em HLE
- [x] Vídeo 2D e 3D por software, áudio (16 canais a 32 kHz) e vídeos Mobiclip
- [x] Launcher sem nada do jogo: o jogo é recompilado no PC do jogador
- [x] Mods: troca de arquivos e dos dados das overlays, inteiros ou em patches BPS (sem dados do jogo)
- [ ] Release para Linux (hoje só compilando)
- [ ] Conquistas pelo estado ao vivo do jogo (hoje são conferidas no save)
- [ ] Mods: formatos documentados, texturas em alta resolução, editor de técnicas

## Compilando

Requisitos: Python 3.12+, CMake 3.28+, um compilador C++20 e SDL3. Também é preciso o dump retail.

```sh
python3 tools/extract.py retail.nds        # uma vez: confere o dump e cria build/boej.pak
python3 tools/analyze.py dsd retail.nds    # uma vez: mapa de funções (ds-decomp)
python3 tools/build.py                     # recompila e compila build/cmake-rel/game/boej
./build/cmake-rel/game/boej
python3 -m unittest discover -s tools/tests -t tools   # testes (não precisam da ROM)
```

Build para Windows a partir do Linux/WSL, release, análise com Ghidra e variáveis de ambiente para testes:
[`docs/DESENVOLVIMENTO.md`](docs/DESENVOLVIMENTO.md).

## Documentação

| Documento | Conteúdo |
|---|---|
| [Desenvolvimento](docs/DESENVOLVIMENTO.md) | estrutura do repositório, build, release, análise estática, configurações, logs e variáveis de ambiente |
| [Mods](docs/MODS.md) | formato dos mods, patches BPS, ferramentas e como funciona por dentro |
| [Tradução PT-BR](docs/TRADUCAO.md) | o que falta traduzir, checklist, padrão de nomes e regras técnicas do texto |
| [CyberLifePlus](mods/CyberLifePlus/LEIA-ME.md) | créditos, versões e como continuar a tradução |
| [Diário](docs/DIARY.md) | o histórico do projeto, passo a passo, com as descobertas sobre o jogo |

## Contribuindo

- Nunca envie ROMs, dumps, arquivos extraídos ou trechos de código do jogo (o `.gitignore` bloqueia os casos
  comuns). Mods vão como patches `.bps`, e as traduções em `translations/` só com o texto novo.
- Registre descobertas e mudanças relevantes em `docs/DIARY.md`, e as da tradução também no checklist do
  `docs/TRADUCAO.md`.
- Rode os testes (`python3 -m unittest discover -s tools/tests -t tools`) antes de abrir um pull request.

## Créditos

**Tradução**

- Grupo **CyberLife**, pela tradução PT-BR que é a base do mod [`CyberLifePlus`](mods/CyberLifePlus/LEIA-ME.md).

**Projetos e bibliotecas**

- [ds-decomp](https://github.com/AetiasHax/ds-decomp) (Aetias): mapa de funções e extração do código no PC do
  jogador, incluindo a decifração do DS Protect (crédito a taxicat1).
- [SDL3](https://libsdl.org), [Dear ImGui](https://github.com/ocornut/imgui) e
  [llvm-mingw](https://github.com/mstorsjo/llvm-mingw). Licenças em [`launcher/TERCEIROS.txt`](launcher/TERCEIROS.txt).
- [Ghidra](https://github.com/NationalSecurityAgency/ghidra), na análise estática.

**Referências**

- [FFCCEOT-Recomp](https://github.com/JonathanDC64/FFCCEOT-Recomp): recompilação estática de um jogo de DS; modelo
  de arquitetura deste projeto.
- [gen1recomp](https://github.com/bryanthaboi/gen1recomp): modelo das releases com launcher.
- [khdays-port](https://github.com/Yokimitsuro/khdays-port): port nativo de um jogo de DS a partir de decomp.
- [melonDS](https://github.com/melonDS-emu/melonDS) e [GBATEK](https://problemkaputt.de/gbatek.htm): referências
  de hardware.

## Licença e aviso legal

O código deste projeto (recompilador, runtime, launcher e ferramentas) está sob a [licença MIT](LICENSE). Os
componentes de terceiros mantêm as próprias licenças ([`launcher/TERCEIROS.txt`](launcher/TERCEIROS.txt)). A
tradução em `mods/CyberLifePlus/` e `translations/` parte do trabalho do grupo CyberLife e não está coberta pela
licença MIT.

*Inazuma Eleven* e os seus personagens, logotipos e artes, incluindo o logo em `docs/assets/` e o ícone em
`game/icon.ico`, são da LEVEL-5 Inc. Projeto de fãs, sem afiliação com a LEVEL-5 ou a Nintendo. Para jogar, é
preciso ter uma cópia legítima do jogo. O `boej.pak` e o `boej.exe` gerados contêm dados e código do jogo: são de
uso pessoal e não devem ser distribuídos.
