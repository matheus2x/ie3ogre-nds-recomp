# ie3ogre-recomp

Port nativo para Windows de **Inazuma Eleven 3: Sekai heno Chousen!! The Ogre**
(Inazuma Eleven 3: Team Ogre Attacks!, Nintendo DS) por **recompilação estática** ARM → C.

O código do jogo não é emulado: o ARM9 e as overlays viram C e são compilados para x86-64. O hardware do DS
e o ARM7 são reimplementados no PC.

> **Estado: pré-alfa.** O jogo roda com vídeo 2D/3D, som, vídeos e controles, e a história já foi jogada
> até depois da primeira partida. Ainda há bugs, e o progresso depende de correções a cada sessão de teste.
> O histórico completo está em [`docs/DIARY.md`](docs/DIARY.md).

## Objetivo

Releases **plug and play** para Windows:

1. Baixe a release e descompacte.
2. Abra o executável e selecione o **seu** dump (ou arraste o `.nds` para a janela).
3. O programa identifica o dump pelo SHA-1 (retail ou uma versão modificada conhecida), extrai e prepara
   os dados localmente e inicia o jogo.

Nenhuma release distribui dados, código ou assets do jogo.

## Você precisa da sua própria cópia do jogo

Este projeto **não inclui a ROM** nem nada derivado dela. Faça o dump do seu cartucho.

| Região | Código | Variante | Dump | SHA-1 |
|---|---|---|---|---|
| Japão | `BOEJ` | retail | `Inazuma Eleven 3 - Sekai e no Chousen!! - The Ogre (Japan).nds` | `9380f1d7ee75e7c6ef24f1b76c88ecdc85088957` |
| Japão | `BOEJ` | tradução de fãs | `5421 - Inazuma Eleven 3 - Sekai heno Chousen The Ogre (J)(BAHAMUT).nds` | `a70e9a336f6ea8ff78003938dcd024533f034a68` |

Os dois dumps têm 512 MiB (536.870.912 bytes) e maker code `HF`. O SHA-1 é do arquivo `.nds` inteiro.
A lista completa fica em [`config/boej/dumps.json`](config/boej/dumps.json).

**Retail e versões modificadas.** O retail é a referência do recompilador. As versões modificadas são suportadas
quando o **código** é idêntico ao do retail e só mudam dados (textos, tabelas, arquivos). É o caso da tradução
acima: ARM9 e ARM7 iguais, e diferenças só em strings/tabelas de 33 overlays e em 512 arquivos. Para comparar
um dump novo com o retail, use `tools/compare_dumps.py`.

**O dump só é usado na preparação.** O `tools/extract.py` gera o `build/boej.pak`, um pacote com as partes da
ROM que o jogo lê (header, binários, tabelas e arquivos, sem o preenchimento vazio; ~448 MiB). Depois disso,
o executável roda só com o pacote, e o `.nds` pode ficar guardado. O pacote e o executável contêm dados e código
do jogo: são de uso pessoal e não devem ser distribuídos.

A versão europeia (*Inazuma Eleven 3: Team Ogre Attacks!*) saiu só para 3DS e não é suportada. Não houve lançamento americano.
As outras versões japonesas de IE3 (Sekai heno Chousen!! **Bomber** / **Spark**) também não são suportadas por enquanto.

Se o seu dump tiver outro SHA-1, abra uma issue informando o código do jogo e o hash.

## Funcionalidades planejadas

- [x] Extração da ROM: ARM9/ARM7, ITCM/DTCM, 133 overlays, NitroFS
- [x] Identificação de dumps por SHA-1 (retail + versões modificadas) e comparação entre dumps
- [x] Mapa de funções (ds-decomp) e projeto Ghidra com overlays; identificação do NitroSDK em andamento
- [x] Recompilador ARMv5TE/Thumb → C++ (`ntr-recomp`): 16.057 funções, ARM9 + ITCM + 133 overlays
- [x] Runtime básico: memória, IRQ, timers, DMA imediato, overlays, threads em fibers, BIOS em HLE
- [x] HLE do NitroSDK e do ARM7: backup → `.sav`, touch, botões X/Y, contador de VBlank, som e relógio (RTC)
- [x] Janela SDL3 com as duas telas, teclado/controle/mouse e ritmo de 59,83 Hz
- [x] Vídeo 2D: bancos de VRAM, BGs texto/afim/estendidos/bitmap, OBJs, janelas, efeitos, brilho mestre, captura
- [x] Vídeo 3D: geometria do GX, rasterizador por software (texturas, translucidez, toon, névoa, edge marking, sombras)
- [x] Resolução interna maior do 3D (1× a 4×, padrão 2×); anti-aliasing e wireframe do 3D
- [x] Áudio: comandos SND de canal/captura/alarme, 16 canais (PCM8/16, IMA-ADPCM, PSG, ruído) mixados a 32 kHz pelo SDL3 (o jogo mixa música e streams no ARM9 e não usa o sequenciador do SDK)
- [x] Vídeos Mobiclip (`.mods`): o decodificador original (assembly escrito à mão) roda num interpretador ARM embutido
- [x] Wi-Fi/DWC desativado (o Nintendo WFC foi encerrado): o ARM7 não oferece o serviço sem fio, então o jogo recebe "sem fio indisponível" e segue pelo próprio caminho de erro
- [x] Dump usado só na preparação: o jogo roda a partir do `boej.pak` (mapeado em memória)
- [x] Configurações (`boej.ini`): janela, tela cheia, filtro, resolução do 3D, volume e controles remapeáveis (teclado e controle)
- [ ] Instalador / release portátil para Windows

## Estrutura do repositório

| Pasta | Conteúdo |
|---|---|
| `recompiler/` | `ntr-recomp` (C++20): manifesto do dsd → C++ |
| `runtime/` | Runtime (C++20): memória, CPU, interpretador ARM, IRQ, timers, threads, PXI/ARM7 em HLE, VRAM, 2D, 3D, som, janela SDL3 |
| `game/` | Executável `boej` |
| `tools/` | Ferramentas em Python (`ntr/` lê a ROM; `extract.py` extrai; `compare_dumps.py` compara dumps; `analyze.py` roda dsd/Ghidra; `ghidra/` tem os scripts do Ghidra; `tests/` tem os testes) |
| `config/boej/` | `dumps.json` (dumps conhecidos), `layout/<variante>.json` (endereços, overlays, hashes), `dsd/` (funções, seções e relocações do ds-decomp), `names.tsv` (nomes identificados, com evidência), `hle.tsv` (funções substituídas no host) e `code_blobs.tsv` (código que o dsd classifica como dados). Nenhum dado do jogo |
| `docs/` | Documentação e diário de implementação |
| `build/` | Saída local (extração, código gerado). Ignorada pelo git |

Pasta prevista: `third_party/` (melonDS).

## Desenvolvimento

Requisitos: Python 3.12+, CMake 3.28+ e um compilador C++20 (clang, GCC ou MSVC/clang-cl).

```sh
# identifica o dump pelo SHA-1, extrai para build/extract/<variante>/
# e gera config/boej/layout/<variante>.json
python3 tools/extract.py "caminho/para/seu/dump.nds"

# compara um dump com o retail (código x dados)
python3 tools/compare_dumps.py retail.nds outro.nds --json build/diff.json

# testes (usam dados sintéticos, não precisam da ROM)
python3 -m unittest discover -s tools/tests -t tools
```

Dumps fora de `dumps.json` são recusados. Para experimentos, use `--allow-unknown`.

### Análise estática

Requer [ds-decomp](https://github.com/AetiasHax/ds-decomp) v0.12.1 e Ghidra 12.1.4 com JDK 21. Os caminhos vêm de
`DSD`, `GHIDRA_INSTALL_DIR` e `JAVA_HOME`; sem essas variáveis, os padrões ficam em `~/tools`.

```sh
python3 tools/analyze.py dsd retail.nds     # mapa de funções -> config/boej/dsd (só aceita o dump retail)
python3 tools/analyze.py names              # aplica config/boej/names.tsv aos símbolos
python3 tools/analyze.py ghidra             # projeto headless -> build/ghidra/boej.gpr
python3 tools/analyze.py decompile NitroMain ov132:0x0213acc0
python3 tools/analyze.py export ram itcm    # pseudo-C de blocos inteiros -> build/ghidra/export/
```

Conteúdo de `build/extract/<variante>/`:

| Caminho | Conteúdo |
|---|---|
| `arm9.bin` | ARM9 descomprimido (BLZ), base 0x02000000 |
| `arm7.bin` | ARM7, base 0x02380000 |
| `autoload/01ff8000.bin`, `autoload/027e0000.bin` | ITCM e DTCM |
| `overlays/ovNNN.bin` | Overlays descomprimidas |
| `files/` | NitroFS (`data_iz/…`, `dwc/…`) |

### Recompilar e rodar

Requer SDL3 (`pacman -S sdl3` no Arch).

```sh
python3 tools/extract.py retail.nds          # uma vez: extração + build/boej.pak
python3 tools/analyze.py dsd retail.nds      # uma vez (mapa de funções)
python3 tools/build.py                       # manifesto -> ntr-recomp -> build/cmake-rel/game/boej (+ boej.pak ao lado)
./build/cmake-rel/game/boej                  # sem argumentos: usa o boej.pak da pasta do executável
```

Também dá para passar o caminho de um `boej.pak` ou de um `.nds` como argumento.

| DS | Teclado | Controle |
|---|---|---|
| Direcional | setas | direcional |
| A / B / X / Y | X / Z / S / A | botões da direita (posição do DS) |
| L / R | Q / W | ombros |
| Start / Select | Enter / Shift direito ou Backspace | Start / Back |
| Tela de toque | botão esquerdo do mouse na tela de baixo | — |

F12 salva a tela em `boej-<quadro>.bmp` e F11 alterna a tela cheia.

Atalhos para testar mais rápido (o título da janela mostra quais estão ligados):

| Atalho | Teclado | Controle |
|---|---|---|
| Acelerar (sem limite de velocidade) | Tab segurado, ou F2 liga/desliga | gatilho direito |
| A automático (aperta e solta A sem parar, para passar mensagens) | C segurado, ou F3 liga/desliga | gatilho esquerdo |

Os dois juntos passam diálogos e cenas muito rápido. O A automático também confirma opções de menu, então
convém desligá-lo antes de escolhas.

#### Configurações

Na primeira vez, o jogo cria o `boej.ini` ao lado do executável, com os valores padrão e comentários.
Ele é lido ao abrir o jogo; apague-o para voltar ao padrão.

| Seção | Opções |
|---|---|
| `[video]` | `window_scale` (tamanho inicial da janela), `resolution_3d` (1 a 4), `fullscreen`, `filter` (`nearest` ou `linear`), `integer_scale` (ampliar só em múltiplos inteiros) |
| `[audio]` | `volume` (0 a 100) |
| `[keyboard]` | uma linha por botão do DS (`A`, `B`, ..., `UP`, `DOWN`) e por atalho (`fast_forward`, `fast_forward_toggle`, `turbo`, `turbo_toggle`, `screenshot`, `debug_dump`, `fullscreen`), com nomes de tecla do SDL separados por vírgula, por exemplo `SELECT = Right Shift, Backspace`. Vazio desliga. |
| `[gamepad]` | o mesmo, com botões pela posição (`south`, `east`, `west`, `north`), `start`, `back`, `leftshoulder`, `dpup` etc., e eixos com direção (`lefty-` = analógico para cima, `righttrigger`). Por padrão, o analógico esquerdo também move o direcional. |

Valores inválidos aparecem no `boej.log` com o número da linha, e o padrão é usado no lugar.
Sem janela (`NTR_HEADLESS`), o arquivo só é lido se `NTR_CONFIG` apontar para ele, para os testes não dependerem
das configurações de quem joga.

#### Relatando problemas

O jogo grava, ao lado do executável:

| Arquivo | Conteúdo |
|---|---|
| `boej.log` | tudo o que aparece no terminal, com o número do quadro em cada linha, mais os arquivos que o jogo abre. Falhas e travamentos (nenhum quadro por 5 s) entram no log com o estado da máquina, e cada segundo com quadros acima de 16,7 ms (queda de FPS, som engasgando) gera uma linha `[perf]`. |
| `boej-replay.txt` + `boej-replay.sav` | a sessão inteira (botões e toque por quadro, a hora inicial do relógio e o save de onde ela partiu). Com eles a sessão é repetida exatamente igual: `NTR_REPLAY=boej-replay.txt NTR_SAVE=<cópia do boej-replay.sav>`. |

A sessão anterior fica em `boej.old.log` e `boej-replay.old.*`, então um problema não se perde ao abrir o jogo de
novo. Se algo der errado, aperte **F8** no momento (grava o estado no log e uma captura de tela), feche o jogo e
envie esses arquivos.

#### Windows (compilação cruzada a partir do WSL/Linux)

Requer MinGW-w64 (`pacman -S mingw-w64-gcc` no Arch). O `ntr-recomp` continua sendo compilado e executado
no Linux; só o jogo é compilado para Windows. O CMake baixa o pacote oficial `SDL3-devel-*-mingw`. O resultado
é o `boej.exe` (libstdc++ estática) mais o `SDL3.dll`, que precisa ficar na mesma pasta. O `--dist` copia os dois
e o `boej.pak`; o pacote só é recopiado quando muda.

```sh
python3 tools/build.py --windows                       # build/cmake-win/game/boej.exe
python3 tools/build.py --windows --dist /mnt/c/Games/ie3  # e copia para C:\Games\ie3
```

`tools/build.py --trace` gera uma build com rastreamento de funções em `build/cmake`. Ao receber Ctrl+C,
ela imprime as últimas 256 funções chamadas.

Variáveis de ambiente:

- **Save:** `NTR_SAVE` (padrão: `saves/boej.sav`).
- **Configurações:** `NTR_CONFIG` (outro arquivo no lugar do `boej.ini`). As variáveis abaixo têm prioridade sobre ele.
- **Janela e ritmo:** `NTR_SCALE` (escala inicial; padrão do `boej.ini`, 2); `NTR_FAST` (sem limite de 60 Hz); `NTR_HEADLESS` (sem janela e sem limite).
- **Resolução do 3D:** `NTR_3D_SCALE=1..4` (padrão: `resolution_3d` do `boej.ini` com janela, 2; 1 sem janela). Só o 3D ganha resolução, e as camadas 2D são esticadas. O jogo e a captura de tela do DS continuam vendo a imagem nativa. As capturas (F12, F8, `NTR_SCREENSHOT_FRAMES`) saem no tamanho mostrado na janela. O 3D usa até 8 núcleos (`NTR_3D_THREADS=1` desliga), e a imagem é a mesma com qualquer número.
- **Áudio:** `NTR_MUTE` (sem dispositivo de áudio); `NTR_WAV=arquivo.wav` grava a saída de som (funciona também sem janela).
- **Relógio:** o RTC usa a hora local do PC; `NTR_RTC="2010-07-01 12:00:00"` parte de um momento fixo e avança com o tempo emulado (testes reproduzíveis).
- **Testes automáticos:**
  - `NTR_INPUT` roteiriza a entrada por quadro, por exemplo `300-310:TOUCH@128,96;900-905:A+START`;
  - `NTR_SCREENSHOT_FRAMES` (lista separada por vírgulas) com `NTR_SCREENSHOT` (caminho; `%d` vira o número do quadro) salva quadros em BMP;
  - `NTR_EXIT_FRAME` encerra depois de N quadros.
- **Depuração:**
  - `NTR_LOG_IRQ`, `NTR_LOG_THREAD`, `NTR_LOG_CARD`, `NTR_DUMP_SND` (cada comando SND), `NTR_LOG_TP`, `NTR_LOG_3D` (polígonos e comandos do GX a cada 60 trocas de buffer);
  - `NTR_PEEK=endereço:tamanho` imprime memória do jogo a cada quadro;
  - `NTR_PROFILE=início-fim` (só na build `--trace`) conta as funções chamadas nesses quadros.

## Documentação

- [`docs/DIARY.md`](docs/DIARY.md): diário com descobertas, decisões e bugs, em ordem cronológica

## Referências

- [FFCCEOT-Recomp](https://github.com/JonathanDC64/FFCCEOT-Recomp): recompilação estática de um jogo de DS; é o modelo de arquitetura deste projeto
- [khdays-port](https://github.com/Yokimitsuro/khdays-port): port nativo de um jogo de DS a partir de decomp
- [melonDS](https://github.com/melonDS-emu/melonDS): referência de hardware
- [GBATEK](https://problemkaputt.de/gbatek.htm): documentação do hardware do DS

## Contribuindo

- Nunca envie ROMs, dumps, arquivos extraídos ou trechos de código do jogo. O `.gitignore` já bloqueia os casos comuns.
- Registre descobertas e mudanças relevantes em `docs/DIARY.md`.

## Legal

Projeto de fãs, sem afiliação com LEVEL-5 ou Nintendo. *Inazuma Eleven* é marca da LEVEL-5 Inc.
Para usar, é preciso ter uma cópia legítima do jogo.
