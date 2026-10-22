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

A versão europeia (*Inazuma Eleven 3: Team Ogre Attacks!*) saiu só para 3DS e não é suportada. Não houve lançamento americano.
As outras versões japonesas de IE3 (Sekai heno Chousen!! **Bomber** / **Spark**) também não são suportadas por enquanto.

Se o seu dump tiver outro SHA-1, abra uma issue informando o código do jogo e o hash.

## Funcionalidades planejadas

- [x] Extração da ROM: ARM9/ARM7, ITCM/DTCM, 133 overlays, NitroFS
- [x] Identificação de dumps por SHA-1 (retail + versões modificadas) e comparação entre dumps
- [x] Mapa de funções (ds-decomp) e projeto Ghidra com overlays; identificação do NitroSDK em andamento
- [x] Recompilador ARMv5TE/Thumb → C++ (`ntr-recomp`): 16.057 funções, ARM9 + ITCM + 133 overlays
- [x] Runtime básico: memória, IRQ, timers, DMA imediato, overlays, threads em fibers, BIOS em HLE
- [ ] HLE do NitroSDK e do ARM7: backup → `.sav` e touch prontos; som só confirma os comandos; RTC pendente
- [ ] Vídeo: 2D e 3D com resolução interna maior; renderer de referência fiel ao DS
- [ ] Áudio: SDAT/SND e streams `sadl`
- [ ] Vídeos Mobiclip (`.mods`)
- [ ] Wi-Fi/DWC desativado (o Nintendo WFC foi encerrado)
- [ ] Seleção do dump no primeiro uso, configurações, controles remapeáveis
- [ ] Instalador / release portátil para Windows

## Estrutura do repositório

| Pasta | Conteúdo |
|---|---|
| `recompiler/` | `ntr-recomp` (C++20): manifesto do dsd → C++ |
| `runtime/` | Runtime (C++20): memória, CPU, IRQ, timers, threads, PXI/ARM7 em HLE |
| `game/` | Executável `boej` |
| `tools/` | Ferramentas em Python (`ntr/` lê a ROM; `extract.py` extrai; `compare_dumps.py` compara dumps; `analyze.py` roda dsd/Ghidra; `ghidra/` tem os scripts do Ghidra; `tests/` tem os testes) |
| `config/boej/` | `dumps.json` (dumps conhecidos), `layout/<variante>.json` (endereços, overlays, hashes), `dsd/` (funções, seções e relocações do ds-decomp), `names.tsv` (nomes identificados, com evidência) e `hle.tsv` (funções substituídas no host). Nenhum dado do jogo |
| `docs/` | Documentação e diário de implementação |
| `build/` | Saída local (extração, código gerado). Ignorada pelo git |

Pasta prevista: `third_party/` (melonDS, SDL3).

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

```sh
python3 tools/extract.py retail.nds          # uma vez
python3 tools/analyze.py dsd retail.nds      # uma vez (mapa de funções)
python3 tools/build.py                       # manifesto -> ntr-recomp -> build/cmake-rel/game/boej
./build/cmake-rel/game/boej retail.nds
```

#### Windows (compilação cruzada a partir do WSL/Linux)

Requer MinGW-w64 (`pacman -S mingw-w64-gcc` no Arch). O `ntr-recomp` continua sendo compilado e executado
no Linux; só o jogo é compilado para Windows. O resultado é um único `boej.exe` estático, que depende apenas
das DLLs do sistema.

```sh
python3 tools/build.py --windows                       # build/cmake-win/game/boej.exe
python3 tools/build.py --windows --dist /mnt/c/Games/ie3  # e copia para C:\Games\ie3
```

Coloque a ROM num disco do Windows: lida por `\\wsl$`, ela leva bem mais de 20 s só para carregar.

`tools/build.py --trace` gera uma build com rastreamento de funções em `build/cmake`. Ao receber Ctrl+C,
ela imprime as últimas 256 funções chamadas.

Variáveis de ambiente para depuração: `NTR_LOG_IRQ`, `NTR_LOG_THREAD`, `NTR_LOG_CARD`, `NTR_LOG_SND`.
Para o save, `NTR_SAVE` (padrão: `saves/boej.sav`).

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
