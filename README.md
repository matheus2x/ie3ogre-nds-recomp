# ie3ogre-recomp

Port nativo para Windows de **Inazuma Eleven 3: Sekai heno Chousen!! The Ogre**
(Inazuma Eleven 3: Team Ogre Attacks!, Nintendo DS) por **recompilação estática** ARM → C.

O código do jogo não é emulado: o ARM9 e as overlays viram C e são compilados para x86-64. O hardware do DS
e o ARM7 são reimplementados no PC.

> **Estado: pré-alfa.** O código do jogo já é recompilado inteiro e o boot vai até o carregamento dos
> primeiros gráficos, mas ainda não há vídeo, áudio nem controles. Ainda não dá para jogar.
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
- [ ] HLE do NitroSDK e do ARM7: backup → `.sav`, touch, botões X/Y e contador de VBlank prontos; som só confirma os comandos; RTC pendente
- [x] Janela SDL3 com as duas telas, teclado/controle/mouse e ritmo de 59,83 Hz
- [x] Vídeo 2D: bancos de VRAM, BGs texto/afim/estendidos/bitmap, OBJs, janelas, efeitos, brilho mestre, captura
- [x] Vídeo 3D: geometria do GX, rasterizador por software (texturas, translucidez, toon, névoa, edge marking, sombras)
- [ ] Resolução interna maior; anti-aliasing e wireframe do 3D
- [ ] Áudio: SDAT/SND e streams `sadl`
- [ ] Vídeos Mobiclip (`.mods`): hoje os quadros são pulados (a abertura toca em preto, só com a letra)
- [ ] Wi-Fi/DWC desativado (o Nintendo WFC foi encerrado)
- [x] Dump usado só na preparação: o jogo roda a partir do `boej.pak` (mapeado em memória)
- [ ] Configurações, controles remapeáveis
- [ ] Instalador / release portátil para Windows

## Estrutura do repositório

| Pasta | Conteúdo |
|---|---|
| `recompiler/` | `ntr-recomp` (C++20): manifesto do dsd → C++ |
| `runtime/` | Runtime (C++20): memória, CPU, IRQ, timers, threads, PXI/ARM7 em HLE, VRAM, 2D, 3D, janela SDL3 |
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

Tab (segurado) acelera; F12 salva a tela em `boej-<quadro>.bmp`.

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
- **Janela e ritmo:** `NTR_SCALE` (escala inicial, padrão 2); `NTR_FAST` (sem limite de 60 Hz); `NTR_HEADLESS` (sem janela e sem limite).
- **Testes automáticos:**
  - `NTR_INPUT` roteiriza a entrada por quadro, por exemplo `300-310:TOUCH@128,96;900-905:A+START`;
  - `NTR_SCREENSHOT_FRAMES` (lista separada por vírgulas) com `NTR_SCREENSHOT` (caminho; `%d` vira o número do quadro) salva quadros em BMP;
  - `NTR_EXIT_FRAME` encerra depois de N quadros.
- **Depuração:**
  - `NTR_LOG_IRQ`, `NTR_LOG_THREAD`, `NTR_LOG_CARD`, `NTR_LOG_SND`, `NTR_LOG_TP`, `NTR_LOG_3D` (polígonos e comandos do GX a cada 60 trocas de buffer);
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
