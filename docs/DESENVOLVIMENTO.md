# Desenvolvimento

Detalhes para quem compila, testa ou analisa o jogo. O resumo está no
[README](../README.pt-BR.md) ([em inglês](../README.md)); o formato dos mods, em [`MODS.md`](MODS.md); a tradução
PT-BR, em [`TRADUCAO.md`](TRADUCAO.md).

Requisitos: Python 3.12+, CMake 3.28+, um compilador C++20 (clang, GCC ou MSVC/clang-cl) e SDL3.

```sh
# identifica o dump pelo SHA-1, extrai para build/extract/<variante>/
# e gera config/boej/layout/<variante>.json
python3 tools/extract.py "caminho/para/seu/dump.nds"

# compara um dump modificado com o retail (código x dados)
python3 tools/compare_dumps.py retail.nds outro.nds --json build/diff.json

# testes (usam dados sintéticos, não precisam da ROM)
python3 -m unittest discover -s tools/tests -t tools
```

Dumps fora de `dumps.json` são recusados. Para experimentos, use `--allow-unknown`.

## Estrutura do repositório

| Pasta | Conteúdo |
|---|---|
| `recompiler/` | `ntr-recomp` (C++20): manifesto do dsd → C++ |
| `runtime/` | Runtime (C++20): memória, CPU, interpretador ARM, IRQ, timers, threads, PXI/ARM7 em HLE, VRAM, 2D, 3D, som, janela SDL3 |
| `game/` | Executável `boej` e o ícone do Windows (`icon.ico`/`icon.rc`, usado também pelo launcher) |
| `launcher/` | `ie3-launcher` (SDL3 + Dear ImGui): prepara, configura, atualiza e abre o jogo; `README.txt`, `LEIA-ME.txt` e `TERCEIROS.txt` da release |
| `runtime/ui/` | `ntr-ui`: telas em Dear ImGui usadas pelo jogo (menu de pausa) e pelo launcher (painel de configurações) |
| `translations/` | Traduções do texto para os mods oficiais: o lugar de cada fala e o português, sem texto do jogo (`tools/text.py apply`) |
| `tools/` | Ferramentas em Python (`ntr/` lê a ROM; `extract.py` extrai; `compare_dumps.py` compara dumps; `analyze.py` roda dsd/Ghidra; `mod.py` cria, confere e transforma mods em patches; `ntr/bps.py` gera e aplica patches BPS; `text.py` mede, lista e aplica a tradução do texto (formatos em `ntr/text.py`, LZ10 em `ntr/lz10.py`); `build.py` compila para desenvolvimento; `release.py` monta a release; `ghidra/` tem os scripts do Ghidra; `tests/` tem os testes) |
| `config/boej/` | `dumps.json` (dumps conhecidos), `layout/<variante>.json` (endereços, overlays, hashes), `dsd/` (funções, seções e relocações do ds-decomp), `names.tsv` (nomes identificados, com evidência), `hle.tsv` (funções substituídas no host) e `code_blobs.tsv` (código que o dsd classifica como dados). Nenhum dado do jogo |
| `mods/` | Mods oficiais, um por pasta, só com patches `.bps` (nenhum dado do jogo); cada um sai como `.zip` na release |
| `docs/` | Diário (`DIARY.md`), mods (`MODS.md`), tradução (`TRADUCAO.md`), este documento e `assets/` (o logo do README) |
| `.github/workflows/` | CI da release (`release.yml`) |
| `build/` | Saída local (extração, código gerado). Ignorada pelo git |

## Análise estática

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

## Recompilar e rodar

O SDL3 vem do sistema (no Arch, o pacote `sdl3`; no Debian/Ubuntu, `libsdl3-dev` quando disponível).

```sh
python3 tools/extract.py retail.nds          # uma vez: extração + build/boej.pak
python3 tools/analyze.py dsd retail.nds      # uma vez (mapa de funções)
python3 tools/build.py                       # manifesto -> ntr-recomp -> build/cmake-rel/game/boej (+ boej.pak ao lado)
./build/cmake-rel/game/boej                  # sem argumentos: usa o boej.pak da pasta do executável
```

Também dá para passar o caminho de um `boej.pak` ou de um `.nds` como argumento.

**Savestates:** 5 slots, cada um num arquivo em `savestates/` na pasta do jogo (`boej-slot1.state` a
`boej-slot5.state`, ~6 MB cada). **F5** salva no slot escolhido, **F9** carrega, **F6**/**F7** escolhem o slot; o
menu de pausa tem a página **Savestates**, com a data de cada slot. Carregar não mexe no save do próprio jogo
(`saves/boej.sav`). O savestate guarda também as pilhas do código recompilado, então só abre no mesmo `boej.exe`
que o criou, e com os mesmos mods: depois de uma atualização (que recompila o jogo) ou de trocar os mods, os slots
antigos aparecem como "de outra versão do jogo ou com outros mods".

## Configurações

Na primeira vez, o jogo cria o `boej.ini` ao lado do executável, com os valores padrão e comentários.
Ele é lido ao abrir o jogo; apague-o para voltar ao padrão.

| Seção | Opções |
|---|---|
| `[video]` | `wide_layout` (padrão `true`: janela 16:9 pela altura do monitor, as duas telas à esquerda e a de baixo ampliada à direita; `false` = telas empilhadas e centralizadas), `wide_side` (`left` ou `right`: lado das duas telas pequenas na tela larga), `window_scale` (tamanho inicial da janela, só sem `wide_layout`), `resolution_3d` (1 a 10), `fullscreen`, `filter` (`nearest` ou `linear`), `integer_scale` (ampliar só em múltiplos inteiros, só sem `wide_layout`) |
| `[audio]` | `volume` (0 a 100) |
| `[game]` | `fast_forward_speed`: velocidade do avanço rápido, 2 a 10 (vezes a normal; padrão 4); `rewind`, `rewind_seconds`; `save`: o arquivo de `saves/` que o jogo carrega e grava (padrão `boej.sav`; a aba Saves do launcher escolhe) |
| `[ui]` | `language`: idioma do launcher e do menu de pausa do jogo, `en` (padrão) ou `pt`; o texto do jogo não muda. O launcher troca na aba Jogar ("Language / Idioma") ou em Configurações → Launcher |
| `[mods]` | `enabled`: mods de `mods/` ligados (nomes das pastas e dos `.zip`), separados por vírgula; quando dois mods trocam o mesmo arquivo, vale o que vem depois (a aba Mods do launcher cuida disso; veja [`MODS.md`](MODS.md)) |
| `[keyboard]` | uma linha por botão do DS (`A`, `B`, ..., `UP`, `DOWN`) e por atalho (`fast_forward`, `fast_forward_toggle`, `turbo`, `turbo_toggle`, `screenshot`, `debug_dump`, `fullscreen`, `save_state`, `load_state`, `state_slot_prev`, `state_slot_next`), com nomes de tecla do SDL separados por vírgula, por exemplo `SELECT = Right Shift, Backspace`. Vazio desliga. |
| `[gamepad]` | o mesmo, com botões pela posição (`south`, `east`, `west`, `north`), `start`, `back`, `leftshoulder`, `dpup` etc., e eixos com direção (`lefty-` = analógico para cima, `righttrigger`). Por padrão, o analógico esquerdo também move o direcional. |

Valores inválidos aparecem no log da sessão (`logs/sessoes/`) com o número da linha, e o padrão é usado no lugar.
Sem janela (`NTR_HEADLESS`), o arquivo só é lido se `NTR_CONFIG` apontar para ele, para os testes não dependerem
das configurações de quem joga.

## Arquivos de diagnóstico

Cada abertura do jogo é uma sessão, com nome de data e hora (`2026-10-27_14-03-12`). O jogo grava, na pasta `logs/`
ao lado do executável:

| Arquivo | Conteúdo |
|---|---|
| `logs/sessoes/<sessão>.log` | tudo o que aparece no terminal, com o número do quadro e a hora em cada linha, mais os arquivos que o jogo abre. Linhas `[sessao]` marcam o que o jogador fez: saves do jogo, savestates, menu de pausa, telas conhecidas (tabela em `runtime/src/cpu.cpp`). Falhas e travamentos (nenhum quadro por 5 s) entram no log com o estado da máquina, e cada segundo com quadros acima de 16,7 ms (queda de FPS, som engasgando) gera uma linha `[perf]`. |
| `logs/replays/<sessão>.txt` + `.sav` | a sessão inteira (botões e toque por quadro, a hora inicial do relógio e o save de onde ela partiu). Com eles a sessão é repetida exatamente igual: `NTR_REPLAY=logs/replays/<sessão>.txt NTR_SAVE=<cópia do .sav>`. |
| `logs/historico.txt` | uma linha quando cada sessão de jogador começa (versão, mods, save) e outra quando termina (duração, quadros, motivo, saves, savestates, quedas de FPS, travamentos, última tela). Um início sem fim = o jogo foi fechado à força. Nunca é apagado. |
| `logs/launcher.log` | o log do launcher (o anterior em `launcher.old.log`). |

Ficam as 20 sessões mais recentes. Execuções roteirizadas ou sem janela (`NTR_INPUT`, `NTR_REPLAY`, `NTR_HEADLESS`)
não entram no histórico; com `NTR_LOG_FILE` o log vai para o caminho dado e a pasta `logs/` nem é usada. Se algo
der errado, aperte **F8** no momento (grava o estado no log e uma captura de tela), feche o jogo e
envie esses arquivos.

## Windows (compilação cruzada a partir do WSL/Linux)

Requer MinGW-w64 (`pacman -S mingw-w64-gcc` no Arch). O `ntr-recomp` continua sendo compilado e executado
no Linux; só o jogo é compilado para Windows. O CMake baixa o pacote oficial `SDL3-devel-*-mingw`. O resultado
é o `boej.exe` (libstdc++ estática) mais o `SDL3.dll`, que precisa ficar na mesma pasta. O `--dist` copia os dois
e o `boej.pak`; o pacote só é recopiado quando muda.

```sh
python3 tools/build.py --windows                       # build/cmake-win/game/boej.exe
python3 tools/build.py --windows --dist /mnt/c/Games/ie3  # e copia para C:\Games\ie3
```

Aberto pelo Explorer, o `boej.exe` esconde a janela de console (tudo vai para o log da sessão em `logs/sessoes/`); aberto de um
terminal, a saída continua nele, e `NTR_CONSOLE=1` mantém o console.

## Release (launcher + SDK)

```sh
python3 tools/release.py                    # build/release/ie3ogre-recomp-<VERSION>-windows.zip, <mod>.zip, sha256sums.txt
python3 tools/release.py --platform linux   # o mesmo para Linux (compilador e SDL3 do sistema)
```

Não precisa do dump. O pacote leva o launcher, o `SDL3.dll` e o `sdk/`:
- `ntr-recomp` e o `dsd` v0.12.1 (que, no PC do jogador, extrai o código do dump e decifra o DS Protect da ov129);
- o manifesto de funções com caminhos `{modules}/...` (`analyze.py manifest --template`);
- as fontes do runtime, do `game/main.cpp`, do menu de pausa (`runtime/ui`) e do Dear ImGui;
- os headers e a biblioteca de importação do SDL3;
- o `toolchain.txt`, com o llvm-mingw fixado por URL e SHA-256.

Cada mod oficial (`mods/<nome>/` com `mod.ini`) vira `build/release/mod-<nome>-<version>.zip` (`version` do
`mod.ini`) e entra no `sha256sums.txt`. Na página da release, o `.zip` aparece com esse nome, sem rótulo
(`build/release/assets.txt`, lido pelo CI), e as notas listam os mods oficiais com a versão. A release é recusada se um mod oficial tiver um arquivo
inteiro do jogo em vez de um patch `.bps`, ou se ele mudou desde a tag anterior e o `version` do `mod.ini` não.

O launcher e o `ntr-recomp` são compilados com o mesmo llvm-mingw (`cmake/toolchains/llvm-mingw-x86_64.cmake`),
baixado uma vez para `build/tools/`. O CI (`.github/workflows/release.yml`) roda isso ao receber uma tag `vX.Y.Z`,
que precisa bater com o arquivo `VERSION`, e publica os zips e o `sha256sums.txt` na release. O texto da release sai
das mensagens de commit desde a tag anterior (`tools/changelog.py`; título de cada commit como seção, corpo em
tópicos), seguido de como instalar e conferir o download. Para ver antes de criar a tag:
`python3 tools/changelog.py HEAD`.

O launcher aceita na linha de comando `--prepare [dump.nds]` (prepara sem janela), `--update` (procura, baixa e
instala a versão nova), `--install-mod <mod.zip ou pasta>` (copia para `mods/`) e `--mod-from-dump hack.nds`. Para testes: `IE3_UPDATE_API` troca a
URL da API de releases (aceita `file://`), `IE3_LAUNCHER_TAB=0..4` abre uma aba (3 = Saves), `IE3_LAUNCHER_NEW_SAVE=<nome>` abre a caixa "Novo save" com esse nome, `IE3_LAUNCHER_MOD=<pasta>` seleciona
um mod na aba Mods e `IE3_LAUNCHER_SHOT=arquivo.bmp` salva a janela e fecha. O jogo aceita `--check-mods` (a
checagem dos mods que a aba Mods mostra).

`tools/build.py --trace` gera uma build com rastreamento de funções em `build/cmake`. Ao receber Ctrl+C,
ela imprime as últimas 256 funções chamadas.

Variáveis de ambiente:

- **Save:** `NTR_SAVE` (padrão: `saves/<[game] save>` na pasta do executável, `saves/boej.sav` sem a chave; um save que só exista em `saves/` da pasta atual, onde as versões anteriores gravavam, é copiado na primeira vez).
- **Configurações:** `NTR_CONFIG` (outro arquivo no lugar do `boej.ini`). As variáveis abaixo têm prioridade sobre ele.
- **Mods:** `NTR_MODS=pasta1,mod2.zip` liga esses mods no lugar do `[mods] enabled` (útil sem janela, quando o `boej.ini` não é lido).
- **Janela e ritmo:** `NTR_SCALE` (escala inicial; padrão do `boej.ini`, 2); `NTR_FAST` (sem limite de 60 Hz); `NTR_HEADLESS` (sem janela e sem limite).
- **Resolução do 3D:** `NTR_3D_SCALE=1..10` (padrão: `resolution_3d` do `boej.ini` com janela, 2; 1 sem janela). Só o 3D ganha resolução, e as camadas 2D são esticadas. O jogo e a captura de tela do DS continuam vendo a imagem nativa. As capturas (F12, F8, `NTR_SCREENSHOT_FRAMES`) saem no tamanho mostrado na janela. O 3D usa até 16 threads (`NTR_3D_THREADS=1` desliga), e a imagem é a mesma com qualquer número.
- **Áudio:** `NTR_MUTE` (sem dispositivo de áudio); `NTR_WAV=arquivo.wav` grava a saída de som (funciona também sem janela).
- **Relógio:** o RTC usa a hora local do PC; `NTR_RTC="2010-07-01 12:00:00"` parte de um momento fixo e avança com o tempo emulado (testes reproduzíveis).
- **Testes automáticos:**
  - `NTR_INPUT` roteiriza a entrada por quadro, por exemplo `300-310:TOUCH@128,96;900-905:A+START`;
  - `NTR_SCREENSHOT_FRAMES` (lista separada por vírgulas) com `NTR_SCREENSHOT` (caminho; `%d` vira o número do quadro) salva quadros em BMP;
  - `NTR_EXIT_FRAME` encerra depois de N quadros;
  - `NTR_STATE_SAVE=N` / `NTR_STATE_LOAD=N` salvam no / carregam do slot 1 de savestate no quadro N.
- **Depuração:**
  - `NTR_LOG_IRQ`, `NTR_LOG_THREAD`, `NTR_LOG_CARD`, `NTR_DUMP_SND` (cada comando SND), `NTR_LOG_TP`, `NTR_LOG_3D` (polígonos e comandos do GX a cada 60 trocas de buffer);
  - `NTR_PEEK=endereço:tamanho` imprime memória do jogo a cada quadro;
  - `NTR_PROFILE=início-fim` (só na build `--trace`) conta as funções chamadas nesses quadros.
