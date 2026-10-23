# Diário de implementação

Registro cronológico do que foi feito, descoberto e decidido. As entradas mais novas ficam no fim.
Endereços são de RAM do DS, a menos que digam outra coisa.

---

## 2026-10-21: Análise inicial e plano

**ROM de entrada.** Dump próprio, modificado: menus em português (`Menu / Dados / Sistema`) e tamanho
declarado no header de 0x1BF74804 (~469 MB), que é atípico para DS.
SHA-1 do arquivo: `a70e9a336f6ea8ff78003938dcd024533f034a68`.

**Header.** `INAZUMA11`, código `BOEJ`, maker `HF`, unitcode 0 (NTR puro, não TWL).

**ARM9**
- Carregado em 0x02000000, entry 0x02000800.
- Comprimido com BLZ até 0x020628E8. Descomprimido tem 0x976B8 bytes.
- Module params em ARM9+0xB88; NitroCode em +0xBA4, gravado como `0xDEC00621, 0x2106C0DE`.
- NitroSDK `0x05047531`, ou seja 5.4 relstep 30001.
- Código estático até 0x02093920. Depois vêm os blocos de autoload: ITCM em 0x01FF8000 (0x3D20 bytes) e DTCM em 0x027E0000 (0x60 + 0x20 de bss).
- BSS estático vai de 0x02093920 a 0x020BCB40, e é aí que começa o primeiro slot de overlay.

**ARM7.** Carregado em 0x02380000 com 0x2642C bytes. É o componente padrão do SDK (string `2.88.00`) e vai inteiro para HLE.

**Overlays.** 133 overlays do ARM9, todos com BLZ, em 6 slots:

| Slot | Overlays | Observação |
|---|---|---|
| 0x020BCB40 | 16 (0–15) | o ov015 é Wi-Fi/DWC |
| 0x020EDE60 | 2 (16–17) | |
| 0x02119F00 | 74 (18–91) | overlays de cena |
| 0x02124D00 | 34 (92–125) | |
| 0x0212A9C0 | 5 (126–130) | |
| 0x0213ACC0 | 2 (131–132) | o ov132 é a lógica da partida |

O plano inicial dizia "7 slots", mas a contagem certa é 6.

**SDKs linkados (tags `[SDK+…]`)**
- NINTENDO: BACKUP, WiFi 3.3, DWC-DL 5.3 PATCH2
- UBIQUITOUS: CPS, SSL
- Actimagine Mobiclip 1.2.1
- Pelas strings, também há NitroSystem: nsbmd/nsbtx e `sound_data.sdat`.

**BIOS.** No ARM9 há 17 stubs Thumb `swi N; bx lr` entre 0x0200006A e 0x0200078C. A tabela completa está no plano.

**NitroFS.** 1852 arquivos.
- Pastas: `/data_iz/{bfmt,effect3d,face2d,font,logic,map2d,model,movie,obj2d,pic2d,pic3d,script,sound,spr}` e `/dwc/utility.bin`.
- Formatos:
  - `.pkb/.pkh`: packs com cabeçalho "PackNum"
  - `.pac_` e `*_`: LZ10 (cabeçalho 0x10)
  - SFP (`.SPF_/.SPD/.SPL`)
  - SSD (scripts)
  - `sadl` (`.SAD`): streams de áudio
  - `.mods`: vídeos Mobiclip
  - `.NFTR`: fontes

**Decisão.** Não existe decomp de IE3. Por isso o caminho é a recompilação estática ARM → C, como no FFCCEOT-Recomp, e não a compilação de uma decomp, como no khdays-port.

**Ferramentas.** Ghidra 12.1.4 via REA. O ARM9 foi aberto como raw ARM em 0x02000000 e a auto-análise achou 3132 funções.

---

## 2026-10-21: Fase 1, ferramentas de extração

**Repositório**
- `git init`.
- `.gitignore` bloqueia ROMs (`*.nds`), saves e `build/`. Nenhum dado do jogo é versionado.
- `config/boej/rom.json` guarda só metadados (endereços, tamanhos, SHA-1).

**Código**
- `tools/ntr/blz.py`: descompressor BLZ que valida o footer.
- `tools/ntr/rom.py`:
  - `Rom` faz o parse de header, FAT e FNT (em Shift-JIS) e da tabela de overlays.
  - `module_params()` localiza os module params pelo NitroCode.
  - `arm9_decompressed()` descomprime o ARM9.
  - `autoloads()` separa ITCM e DTCM.
  - `overlay_decompressed()` descomprime cada overlay.
- `tools/extract.py`: CLI que grava em `build/extract/` e gera `config/boej/rom.json`. Recusa ROMs que não sejam BOEJ/HF, falha se o tamanho descomprimido de um overlay não bater com a tabela e não escreve fora do diretório de saída.
- `tools/tests/test_ntr.py`: testes com ROM e streams BLZ sintéticos, sem precisar de dados do jogo.

**Resultado**
- Os 4 testes passam.
- A extração gera 453 MB em `build/extract/`.
- O ARM9 descomprimido é idêntico byte a byte ao da análise inicial (SHA-1 `fdf67406…`).
- O tamanho das 133 overlays bate com a tabela.

**Bug encontrado.** A ordem das palavras do NitroCode no arquivo é `DEC00621` e depois `2106C0DE`. Eu tinha colocado ao contrário.

**Próximos passos**
1. Carregar no Ghidra um mapa completo de memória: ARM9 estático, ITCM, DTCM e um overlay por slot, para nomear funções do SDK.
2. Descobrir se o som usa o driver padrão do SDK (SDAT) ou um driver Level-5 (`sadl`/`sound.pkb`).
3. Mapear o loader de overlays (`FS_LoadOverlay`) e os `sinit` de cada overlay.

---

## 2026-10-21: README estruturado

- O README foi reescrito no formato do FFCCEOT-Recomp, com uma tabela de dumps suportados (região, código, dump, SHA-1).
- O objetivo final ficou registrado: releases plug and play em que o usuário seleciona o próprio dump e o programa confere o SHA-1, extrai e prepara tudo localmente.
- O SHA-1 do dump retail japonês sem modificações ainda está **a confirmar**. Ele será o alvo principal, enquanto o dump modificado (PT-BR) é o de desenvolvimento.
- Pendência: decidir se o verificador da release vai aceitar o dump modificado ou só o retail. Os overlays e o ARM9 podem ser diferentes entre os dois.

---

## 2026-10-21: Dump retail e suporte a várias variantes

**Novo dump.** Chegou o dump do cartucho original, sem modificações:
- arquivo `Inazuma Eleven 3 - Sekai e no Chousen!! - The Ogre (Japan).nds`
- 536.870.912 bytes
- SHA-1 `9380f1d7ee75e7c6ef24f1b76c88ecdc85088957`

Ele passa a ser a **referência canônica** (`retail`). O dump anterior virou a variante `fan-translation`.

**Comparação retail × tradução** (`tools/compare_dumps.py`)
- ARM9 descomprimido: **idêntico** nos dois (SHA-1 `fdf67406…`).
- ARM7: idêntico.
- Header: mudam os offsets de ARM7/FNT/FAT/banner (deslocados em 0x400), o tamanho declarado (retail 0x1BFC3DAC, tradução 0x1BF74804) e os CRCs.
- Overlays: mudaram 33 das 133. Os slots e os endereços de `sinit` são os mesmos. Em 24 delas o `ram_size` diminuiu alguns bytes (textos mais curtos). As diferenças são strings Shift-JIS trocadas por ASCII (por exemplo `Transfer`, `Run exp.`, `Error`) e tabelas de dados. No ov129 há uma mudança de 3 bytes a cada 0xA0, o que parece uma tabela de structs.
- NitroFS: mesmos 1852 arquivos, 512 com conteúdo diferente. São fontes (`FONT*.NFTR`), `logic/*.dat/.STR`, textos de cutscene (`movie/txt`), 2 vídeos `.mods` e texturas.

**Conclusão.** A tradução não mexe em código, então o recompilador pode partir só do retail, e os dados de cada variante vêm do dump do próprio usuário em tempo de execução. Ainda falta confirmar isso no Ghidra: em 7 overlays (0, 20, 46, 49, 56, 65, 129) a primeira diferença fica antes de `sinit_start`. Pelo conteúdo parecem `.rodata` (strings e tabelas), já que nos binários do CodeWarrior a `.rodata` vem antes da `.init`, mas é preciso conferir se nenhuma cai dentro de uma função.

**Decisão: retail e modificadas.**
1. O código é recompilado a partir do retail. Endereços de funções, hooks e símbolos ficam em configs do retail.
2. Uma variante modificada só é aceita se o código for igual ao do retail. O verificador de release vai checar isso por hash de função ou de faixa de código, e não pelo hash do dump inteiro.
3. Os dados (overlays inteiras, arquivos) sempre vêm do dump do usuário.

**Mudanças nas ferramentas**
- `config/boej/dumps.json`: catálogo de dumps conhecidos (variante, região, nome, tamanho, SHA-1).
- `tools/extract.py`:
  - identifica o dump pelo SHA-1, grava em `build/extract/<variante>/` e gera `config/boej/layout/<variante>.json`;
  - recusa dump desconhecido (a menos que se passe `--allow-unknown`);
  - lança erro, em vez de seguir, quando o tamanho de uma overlay não bate.
- `config/boej/rom.json` foi substituído por `layout/retail.json` e `layout/fan-translation.json`.
- `tools/compare_dumps.py`: compara dois dumps (ARM9/ARM7, faixas de diferença por overlay, arquivos) e exporta JSON.
- Agora são 8 testes, todos passando. Os dois dumps são extraídos sem erro.
- README: tabela de dumps com SHA-1 real e a política para retail e modificadas.

**Próximos passos (atualizados)**
1. No Ghidra, montar o mapa de memória do **retail** (ARM9 estático, ITCM, DTCM, overlays por slot) e nomear as funções do SDK.
2. Usar o mapa de funções para confirmar que as diferenças da tradução estão só em dados.
3. Descobrir se o som usa SDAT/SND padrão ou um driver Level-5.

---

## 2026-10-21: Fase 2, mapa de memória no Ghidra e identificação do SDK

**Ferramentas novas**
- **ds-decomp (`dsd`) v0.12.1**, instalado via `cargo install --git … --tag v0.12.1 --root ~/tools/ds-decomp-0.12.1`. O pipeline exige exatamente essa versão.
  - `dsd init` encontrou 16.054 funções (15.873 ARM e 181 Thumb) no ARM9, no ITCM e nas 133 overlays.
  - Também gerou as seções de cada módulo (`.text/.rodata/.init/.ctor/.data/.bss`) e as relocações (chamadas e literais).
  - A config fica versionada em `config/boej/dsd/` (8,7 MB, só endereços) e é a fonte da verdade dos símbolos.
  - As 2 assinaturas embutidas do dsd (`FS_LoadOverlay`/`FS_UnloadOverlay`) **não batem** com o SDK 5.4 deste jogo.
- **`tools/analyze.py`** reúne os passos de análise:
  - `dsd <retail.nds>` roda dsd extract + init. Só aceita o dump retail e não sobrescreve a config sem `--force`.
  - `names` aplica `config/boej/names.tsv` aos `symbols.txt` do dsd. Recusa nomes duplicados e endereços sem função.
  - `ghidra` monta o projeto headless em `build/ghidra/boej.gpr` (ARM:LE:32:v5t). O script é `tools/ghidra/ImportBoej.java`.
  - `decompile <alvo>…` decompila funções avulsas (`tools/ghidra/Decompile.java`).
  - `export <bloco>…` decompila blocos inteiros para `build/ghidra/export/<bloco>.c` (`tools/ghidra/ExportC.java`).

**Mapa de memória no Ghidra**
- `ram` 0x02000000: só o módulo estático, até 0x02093920.
- `itcm` 0x01FF8000 e `dtcm` 0x027E0000.
- Uma overlay block por overlay (`ov000`…`ov132`), cada uma no seu espaço de endereços.
- O modo ARM/Thumb de cada função é definido antes de qualquer desmontagem; senão há conflito de contexto.
- Resultado: 16.037 funções e 7.689 rótulos, sem falhas.
- Limitação: chamadas de uma overlay para outra (por exemplo ov089 → 0x0210EFAC, no slot do ov016/017) aparecem como "memória inexistente", porque o Ghidra não sabe qual overlay está carregada. O `relocs.txt` do dsd indica o módulo alvo e pode alimentar essas referências depois.
- O REA não abre projetos `.gpr`, então as consultas são feitas pelos scripts headless acima.

**DS Protect no ov129.** O dsd detecta e descriptografa funções protegidas pelo DS Protect (antipirataria) no ov129. O símbolo traz `dsprot=(…)` para indicar isso.
- O Ghidra usa a cópia descriptografada do dsd.
- As diferenças do ov129 na tradução são 3 bytes no offset 0x6D das 4 primeiras funções protegidas (blocos de 0xA0). Isso é um **crack do DS Protect**, não tradução. No port o DS Protect vai ser neutralizado de qualquer jeito, então essa diferença não pesa.
- O `dsd rom extract` **falha** no dump da tradução ("length-distance pair … outside of decompressed stream"). O `tools/extract.py` lida com ele normalmente.

**Diferenças da tradução por seção** (com as seções do dsd). Todas as 33 overlays alteradas mudam só `.data`/`.rodata`, com duas exceções:
- ov051 e ov065: alguns bytes caem fora das seções (padding e o byte extra no fim do ov065).
- ov129: `.text`, que é o crack do DS Protect descrito acima.

A pendência da fase 1 está resolvida: **a tradução não altera código do jogo**.

**Identificação do SDK** (`config/boej/names.tsv`, 232 nomes por enquanto). A identificação foi manual, comparando o pseudo-C exportado com a estrutura dos fontes do NitroSDK, o uso de registradores e a sequência de chamadas. Cada linha registra a evidência.
- **crt0**: `_start`, `init_cp15`, `MIi_UncompressBackward`, `do_autoload`, `_fp_init`, `__call_static_initializers`, `main` e `NitroMain` (0x02029C08).
- **OS**: IRQ, threads, contexto, mensagens, mutex, cache, arena/heap, tick, alarm/valarm, interrupções, `OS_Init` (sequência conferida com o SDK) e reset.
- **ITCM**: `OS_IrqHandler` (0x01FF8000), `OS_IrqHandler_ThreadSwitch` e o caminho de reset.
- **CARD**: init, locks de recurso, comandos de ROM (0xB5/0xB7/0xB8) e `CARDi_ReadRom` (0x0201121C).
- **FS**: overlays (`FS_LoadOverlay` 0x0200E580, `FS_StartOverlay`, `FS_EndOverlay`…). O boot carrega o **ov126** a partir da `NitroMain`.
- **PXI**: `PXI_Init`, `PXI_SetFifoRecvCallback` e `PXI_SendWordByFifo` (0x02015B04).
- **GX**: só o básico (`GX_Init`, DispOn/Off, modos), porque o modelo de hardware cobre o resto.

**Serviços do ARM7 usados pelo jogo** (tags PXI com callback registrado ou envio, no esquema EX=0 do SDK 5):

| Tag | Serviço | Função no ARM9 |
|---|---|---|
| 5 | RTC | `RTC_Init` |
| 6 | Touch | `TP_Init` |
| 7 | Som | `SNDi_InitPxi`, `SND_FlushCommand` |
| 8 | PM (energia) | `PM_Init` |
| 10 | Wi-Fi (WMW) | `WM_Init` |
| 11 | Cartão/FS | `CARDi_*` |
| 12 | Reset do OS | `OS_InitReset` |
| 13 / 17 | Slot-2 (CTRDG / CTRDG_PHI) | `CTRDG_Init` |
| 14 | Remoção do cartão | `CARD_InitPulledOutCallback` |

As tags 3 (NVRAM) e 9 (MIC) não aparecem no ARM9 estático; falta conferir nas overlays.

**Decisões para o HLE** (que vão guiar o recompilador)
1. **Fronteira com o ARM7 = PXI.** O runtime intercepta `PXI_SendWordByFifo` por tag e responde pelos callbacks registrados. O ARM7 não é recompilado.
2. **Threads e contexto**: `OS_SaveContext`, `OS_LoadContext`, `OS_IrqHandler_ThreadSwitch` e `OSi_RescheduleThread` não funcionam como C recompilado, porque trocam a pilha. Vão virar HLE com fibers/corrotinas no host.
3. **Espera**: `OS_Halt`, `OS_WaitIrq`, `OS_WaitVBlankIntr` e `OSi_WaitVCount0` passam a ceder ao scheduler de frames do runtime.
4. **ROM**: o hook fica em `CARDi_ReadRom` (cópia direta da imagem da ROM do usuário). O cartão não é emulado.
5. **Overlays**: `FS_StartOverlay`/`FS_EndOverlay` ativam e desativam o código recompilado de cada overlay.
6. **Cache** (`DC_*`/`IC_*`): no-op.

**Próximos passos**
1. Nomear o restante do SDK que importa para o HLE: MI (DMA/cópias), SND, TP, RTC, PM, CTRDG e a API de arquivos do FS. Depois, as funções de NitroSystem (NNS) e Mobiclip.
2. Referências entre overlays no Ghidra, a partir do `relocs.txt`.
3. Tabela de quais overlays ficam carregadas juntas (slot × cena), necessária para o dispatcher de chamadas indiretas.
4. Começar o `ntr-recomp` (decoder ARMv5TE/Thumb + emissor C) usando os limites de função do dsd.

---

## 2026-10-21: Fase 2, mais nomes do SDK (384 no total)

**Nomes novos** (todos em `config/boej/names.tsv`, cada um com a evidência)
- **MI**: DMA (`MI_DmaFill32`/`Copy32`/`Copy16` e as versões assíncronas, `MI_WaitDma`, `MI_StopDma`), cópias pela CPU (`MIi_CpuCopy*`, `MI_CpuFill8`, `MI_CpuCopy8`), `MI_SwapWord`, `MI_UncompressLZ8`, `MI_UncompressHuffman` e `MI_SetWramBank`. No ITCM, `MIi_DmaSetParameters`.
- **FX/CP**: `FX_Div`, `FX_Inv`, `FX_Sqrt` e as variantes assíncronas; `VEC_*`; `FX_Atan2Idx`; `CP_SaveContext`/`CP_RestoreContext`; `MTX_Identity*`; `MATH_CountPopulation`.
- **RTC**: `RTC_GetDate`/`Time`/`DateTime` e as versões assíncronas. Os comandos PXI que o ARM7 precisa responder são 0x10 (data+hora), 0x11 (data) e 0x12 (hora).
- **SND**: `SND_Init`, o pool de comandos (`SND_CommandInit`: 256 comandos de 0x18 bytes), alloc/push/flush/reply e os envoltórios da API, cada um com seu `SND_COMMAND_*` (1–3, 6, 7, 9, 12–21, 25–27, 31, 32). Isso é a especificação do driver de som que o ARM7 em HLE vai ter que executar.
- **TP**: amostragem simples e automática, calibração, `TP_WaitBusy`.
- **PM**: `PMi_CommonCallback`, `PM_GoSleepMode`, LCD power.
- **FS** (API de arquivos do `fs_file.c`): `FSi_SendCommand`, `FS_OpenFile`/`Ex`, `FS_ReadFile`, `FS_SeekFile`, `FS_CloseFile`, `FS_ConvertPathToFileID`, `FS_GetFileLength`/`Position` e `FSi_ReadRomDirect` (o arquivo ROM chama `CARDi_ReadRom`).
- **CARD (backup)**: `CARD_IdentifyBackup`, `CARDi_RequestStreamCommand` e `CARD_GetResultCode`.
  - O jogo identifica o backup como **0x1001 = EEPROM de 64 KB**, então o `.sav` do port tem 64 KB.
  - As leituras usam o comando 6. As escritas usam o comando 8 em EEPROM/FRAM e o 7 em FLASH.
  - As funções de save do próprio jogo ficam em 0x02063408 (identify), 0x020634B0 (read) e 0x02063554 (write).
- **Runtime do CodeWarrior**:
  - inteiros de 64 bits: `_ll_mul`, `_ll_sdiv`, `_ll_mod`, `_ull_div` e `_ull_mod`;
  - divisão de 32 bits: `_s32_div_f` e `_u32_div_f`.
  - O `_ll_sdiv` e o `_ull_mod` são **entradas no meio** de outra função; o dsd os registra como `label`, e `analyze.py names` agora aceita rótulos.
  - Todas essas rotinas são candidatas a operação nativa no recompilador.
- **MSL**: `memcpy`, `memmove`, `memset`, `memchr`, `memcmp`, `__fill_mem`, `wcstombs`.
- **NitroSystem FND**: listas (`NNS_FndInitList`, `Append`, `Prepend`, `Insert`, `Remove`, `GetNext`, `GetPrev`). É o primeiro trecho de NNS, em 0x0201769C.

**Correções**
- `SND_AllocCommand` e `SND_RecvCommandReply` estavam trocados. O envoltório `SNDi_PushCommandImpl` chama 0x0200BD98, que verifica disponibilidade e aloca, então esse endereço é a `SND_AllocCommand`.

**Ferramentas**
- `tools/analyze.py asm <alvo>…` mostra a desmontagem pelo Ghidra (`tools/ghidra/Disasm.java`). Não há `objdump` para ARM no sistema.
- `analyze.py decompile` e `asm` usam o mesmo executor de consultas.

**Como está organizado o ARM9 estático** (estimativa)
- 0x02000000–0x02017400: NitroSDK.
- 0x0201769C em diante: NitroSystem.
- Por volta de 0x0201D000–0x02022000: runtime do CodeWarrior e MSL (ponto flutuante em software, divisão, printf).
- Por volta de 0x02029000 em diante: código do jogo (`NitroMain` fica em 0x02029C08).

**Pendente**
- As rotinas de ponto flutuante em software (por volta de 0x0201E678–0x02020AF8) vão ser identificadas **empiricamente**: rodar cada uma com vetores de teste, quando o recompilador existir, em vez de chutar nomes pelo pseudo-C.
- Também faltam: o restante de GX/G2/G3 e das VRAM banks, CTRDG, WM, NNS (G2D/G3D/SND/heaps) e Mobiclip.

---

## 2026-10-22: Etapa 3, recompilador e runtime (primeiro marco atingido)

**Decisão.** Recompilador e runtime em C++20, seguindo o modelo do FFCCEOT-Recomp.

**Pipeline**
1. `tools/analyze.py manifest` gera `build/recomp/manifest.tsv` a partir do dsd, com módulos, seções, funções, entradas no meio de funções (`label`) e a lista HLE (`config/boej/hle.tsv`).
2. `ntr-recomp` (em `recompiler/`) gera `build/recomp/gen`:
   - os blocos `<módulo>_<n>.cpp`;
   - o `funcs.h`;
   - o `registry.cpp`, com a tabela de funções de cada módulo para as chamadas indiretas.
3. O CMake compila tudo junto com o runtime (`runtime/`) no executável `game/boej`.
4. `tools/build.py` faz os três passos de uma vez.

**Recompilador**
- Análise de fluxo por função (alcançabilidade), de modo que os literal pools não são decodificados como código.
- Labels e `goto` para desvios locais. Cada instrução vira um bloco C++ e condições viram `if`.
- **Instruções ARM**: processamento de dados com barrel shifter e carry; MUL/MLA; multiplicações longas; multiplicações DSP (SMLAxy…); QADD; CLZ; LDR/STR, halfword e LDRD/STRD; LDM/STM; SWP; MRS/MSR; MCR/MRC; SWI; BLX.
- **Instruções Thumb**: o conjunto v5 completo.
- **Chamadas**: BL vira chamada direta C++ quando o alvo é conhecido. Alvos no slot de outra overlay vão para `call_indirect`. Desvio para fora da função vira tail call.
- **Retornos**: `bx lr`, `pop {pc}`, `ldmia sp!,{..,pc}`, `ldr pc,[sp],#4` e `mov pc, lr` viram `return`.
- **Jump tables**: tanto `addls pc, pc, rN, lsl #2` quanto `ldr pc, [pc, rN, lsl #2]` viram `switch`.
  - Bug encontrado: o limite vinha do `cmp` mais próximo, mas o CodeWarrior também emite `cmp rN,#max; bgt; cmp rN,#0; addge pc…`.
  - Agora o limite vem do `cmp` seguido de `bhi`/`bgt`, ou do próprio despacho `ls`/`le`; senão, da contagem dos `b` da tabela.
  - Esse bug deixava o `__strtoul` do MSL em laço infinito.
- **Literal pools**: as cargas viram constantes, porque o código vem do retail.
- **Tempo emulado**: todo desvio para trás decrementa `c.budget`; quando ele zera, o runtime roda `poll()`, que avança o tempo e entrega IRQs. É isso que faz espera ativa em RAM terminar.
- **HLE**: funções em `hle.tsv` viram `hle_<id>(c, &orig_…)`, e o hook pode chamar a versão original recompilada.
- **Números**: 16.057 funções e 747 mil instruções, em menos de 1 s. Só 1 caso não tratado ("fell off function end"). A build do código gerado leva ~80 s com 16 núcleos (`-O1`).

**Runtime**
- **Memória**: tabela de páginas de 4 KiB para leitura e escrita, com RAM principal espelhada, ITCM, DTCM, WRAM, paleta, VRAM (só a visão LCDC por enquanto) e OAM. Um caminho lento trata o MMIO.
- **I/O**: arquivo de registradores mais DIV/SQRT, DISPSTAT/VCOUNT, timers, IF (escrever 1 limpa), DMA imediato, IPC/PXI e o barramento do cartão (respostas a 0xB8 e 0xB7).
- **Tempo**: ciclos emulados e scanlines (VBlank/HBlank/VCount); timers com IRQ de overflow.
- **CPU**: modos com bancos de r13/r14/SPSR, CP15 (ID, cache e TCM) e BIOS em HLE (Div, Sqrt, CpuSet, CpuFastSet, CRC16, LZ77, RL, IsDebugger).
- **Boot fiel ao firmware**: o ARM9 **comprimido** é copiado da ROM do usuário, e o crt0 do jogo o descomprime e faz o autoload. O header vai para 0x027FFE00, e as configurações do usuário (inclusive a calibração do touch) para 0x027FFC80.
- **Threads em fibers do host** (ucontext no Linux, Fibers no Windows):
  - `OS_SaveContext`/`OS_LoadContext` guardam e retomam o estado da CPU por fiber.
  - Um contexto novo cria uma fiber que começa no pc do contexto.
  - `OS_InitContext` descarta a fiber associada a um `OSThread` reaproveitado.
- **IRQ**: `OS_IrqHandler` virou HLE completo em C++. O original usa `lr = OS_IrqHandler_ThreadSwitch` para encadear a troca de thread, e isso não sobrevive como chamada do host. A troca de thread dentro da IRQ libera as IRQs para a nova thread.
- **Overlays**: `FS_StartOverlay`/`FS_EndOverlay` marcam qual overlay ocupa cada slot. Todas as 133 estão sempre recompiladas.
- **Cartão**: só `CARDi_ReadRomWithCPU` (cópia direta da ROM) e `CARDi_TryReadCardDma` (devolve 0) foram substituídas. A lógica de tarefa e callback do SDK roda original, na thread do cartão.
- **ARM7 em HLE**:
  - handshake do IPCSYNC e bitmap de tags prontas em 0x02FFFF8C;
  - **cartão/backup** (tag 11): INIT com ponteiro compartilhado, leitura, escrita, verificação e apagamento sobre `saves/boej.sav` (64 KB), com escrita atômica (arquivo temporário e rename);
  - **touch** (tag 6): por enquanto sempre "sem toque";
  - **som** (tag 7): só conta os comandos e avança o `finishCommandTag` (o ponteiro dele fica em 0x02095920).
- **Depuração**:
  - `NTR_TRACE` mantém um buffer circular das últimas funções, impresso ao receber SIGINT/SIGTERM;
  - logs por variável de ambiente: `NTR_LOG_IRQ`, `NTR_LOG_THREAD`, `NTR_LOG_CARD`, `NTR_LOG_SND`.

**Resultado**
- O boot do dump retail executa o crt0, `OS_Init` (PXI, threads, cartão, PM, CTRDG) e `NitroMain`.
- Carrega as overlays 130, 16, 126, 131 e 5 e cria 10 threads.
- Atende as IRQs de VBlank e timer, lê o save inteiro e chega a escrever na VRAM do motor B (0x0620xxxx). Isso dá 12 mil escritas "não mapeadas", porque o mapeamento dos bancos de VRAM ainda não existe.
- Não há travamento nem instrução não tratada até esse ponto.

**Limitações conhecidas**
- O tempo emulado avança rápido demais: cerca de 48 mil VBlanks em 20 s. Ainda falta sincronizar com o relógio do host.
- `OS_SaveContext` não salva o contexto do divisor (CP) do jogo.
- O DMA de modos temporizados (VBlank, HBlank, GX FIFO) roda na hora.
- O GPU 2D/3D, o mapeamento de VRAM e o áudio não existem. RTC, PM, WM e CTRDG ainda só registram os pedidos.
- Pendente desde a Fase 2: identificar as rotinas de ponto flutuante em software. O teste diferencial com Unicorn ainda não foi montado.

**Próximos passos**
1. Mapeamento dos bancos de VRAM (VRAMCNT) e renderização 2D/3D (melonDS como referência, ou um renderer próprio), com janela SDL3.
2. Ritmo de quadros em 60 Hz, sincronizado com o host.
3. Entrada (teclas e touch) e RTC.
4. Driver de som do ARM7 em HLE (SDAT/sequências, streams `sadl`).
5. Teste diferencial por função contra Unicorn.

---

## 2026-10-22: Build para Windows com MinGW-w64 (compilação cruzada)

- Pacote `mingw-w64-gcc` 16.2.0 no WSL (Arch).
- `cmake/toolchains/mingw-w64-x86_64.cmake` linka estaticamente libstdc++, libgcc e winpthread. O `boej.exe` depende só de `KERNEL32` e das DLLs do UCRT (`api-ms-win-crt-*`), que vêm com o Windows 10 e 11.
- `NTR_BUILD_RECOMPILER=OFF` na build cruzada. O `ntr-recomp` é uma ferramenta do host de build e sempre vem da build nativa.
- O código gerado precisa de `-Wa,-mbig-obj` no MinGW, por causa do limite de seções do COFF.
- Portabilidade: o `trace.cpp` usava `_exit` de `unistd.h`; agora usa `std::_Exit`.
- `tools/build.py --windows [--dist DIR]`: compila o recompilador nativo, gera o código, faz a compilação cruzada do jogo e copia o `.exe`.

**Resultado.** O `boej.exe` rodou pelo interop do WSL e chegou ao mesmo ponto da build Linux: overlays 130, 16, 126, 131 e 5, 10 threads, escritas na VRAM do motor B. Isso valida as fibers no Windows (`ConvertThreadToFiber`/`CreateFiber`/`SwitchToFiber`).

**Observação.** Um processo Windows que lê a ROM de 512 MB por `\\wsl$` leva mais de 20 s só para carregá-la.
- Recomendação: deixar a ROM num disco do Windows.
- Melhoria futura: mapear a ROM em memória (mmap / `CreateFileMapping`) em vez de ler o arquivo inteiro.

---

## 2026-10-22: Etapa 4: vídeo 2D, janela SDL3, entrada e ritmo de 60 Hz

**VRAM** (`runtime/src/vram.cpp`)
- O `VRAMCNT_A..I` coloca cada banco na área dada pelos campos MST/OFS: BG/OBJ dos motores A e B, LCDC, texturas, paletas de textura e paletas estendidas de BG/OBJ.
- Cada área é uma "view" em grânulos de 16 KiB. As páginas da CPU em 0x06000000-0x068A3FFF são refeitas a cada escrita em `VRAMCNT`, com espelhamento por janela de 2 MiB.
- VRAM sem banco lê 0 e ignora escritas, sem log (as 12 mil escritas "não mapeadas" da etapa 3 sumiram).

**Renderizador 2D** (`runtime/src/gpu2d.cpp`): uma linha por vez, quando o LCD começa a desenhá-la.
- **BGs:** texto (4/8 bpp, paletas estendidas), afim, estendidos (mapa de 16 bits, bitmap de 256 cores e cor direta) e bitmap grande.
- **OBJs:** de tiles 1D/2D, bitmap, afins e de tamanho duplo, semitransparentes e janela de OBJ.
- **Composição:** janelas 0/1/OBJ, alpha blending, clarear/escurecer, brilho mestre, modo de exibição de VRAM e captura de tela.
- Os pontos de referência de BG2/BG3 são recarregados no VBlank e em escritas, e avançam por dmx/dmy a cada linha.
- `POWCNT1` decide qual motor vai para a tela de cima.
- Ainda falta: 3D (o BG0 em modo 3D fica transparente), mosaico e o modo de exibição da memória principal.

**Janela e entrada** (`runtime/src/host.cpp`, SDL3)
- As duas telas ficam empilhadas, com letterbox e escala inteira inicial 2×. O ritmo é de 59,826 Hz pelo relógio do host, com ressincronização se ficar mais de 4 quadros atrasado.
- Entrada:
  - teclado e controle (SDL gamepad) vão para o `KEYINPUT`;
  - X/Y passam pelo ARM7;
  - o mouse na tela de baixo vira toque, convertido em ADC pela mesma calibração das configurações do usuário.
- Modo sem janela (`NTR_HEADLESS`), roteiro de entrada (`NTR_INPUT`), BMP de um quadro (`NTR_SCREENSHOT_FRAME`) e saída automática (`NTR_EXIT_FRAME`). Assim dá para testar sem interação: 600 quadros levam ~3 s.
- Depuração: `NTR_PEEK=endereço:tamanho` imprime memória a cada quadro; `NTR_PROFILE=a-b` (build `--trace`) conta as funções chamadas numa janela de quadros.

**Bugs do ARM7 em HLE** (o aviso anti-pirataria aparecia, mas não aceitava toque):
1. `HW_BUTTON_XY_BUF` (0x027FFFA8) não é o EXTKEYIN cru, e sim o layout do `PAD_Read`: X no bit 10, Y no 11, debug no 13 (1 = solto) e tampa fechada no 15. Com o valor errado, o jogo via X+Y+DEBUG sempre apertados.
2. O ARM7 incrementa o contador de VBlank em 0x027FFC3C (`OS_GetVBlankCount`), e ninguém fazia isso. O jogo mede o tempo de cada quadro por ele (delta em 0x0209A110). O temporizador da cena do aviso (3600 quadros, ou 30 depois do toque) nunca andava.
- O caminho foi: o toque chega certo ao jogo (x=127, y=95) → despachante 0x0202AB80 → cena da ov005 (0x020BD63C marca o toque; o vtable+0x64 põe +0x22 = 1) → máquina de estados 0x020BD1FC, que espera o temporizador.

**Tarefas cooperativas do jogo** (`runtime/src/task.cpp`)
- O motor do jogo tem um escalonador próprio de 16 tarefas, cada uma com sua pilha (0x0206EDC0-0x0206F0B0):
  - `Task_RunAll` retoma cada tarefa uma vez por quadro;
  - `Task_Yield` empilha r0-r11/lr, salva o sp e troca para a pilha do escalonador;
  - `Task_Exit` encerra a tarefa.
- Para retomar, ele salta para o meio de `Task_Yield` (0x0206EE98), onde o código recompilado não tem entrada.
- Virou HLE sobre fibers, como as threads do SDK:
  - cada tarefa é uma fiber;
  - o primeiro quadro emula o frame inicial montado por `Task_Create`;
  - um slot reaproveitado (sp diferente do que deixamos) descarta a fiber antiga.
- Ganhou `fiber_destroy`. Endereços: sp do escalonador 0x020B9D4C, tarefa atual 0x020B9D50, tabela de sp 0x020B9D54, entradas 0x020B9D94.

**Blob de código e recompilador**
- A ov127 (abertura) chama por ponteiro um blob em 0x02130754 que o dsd classificou como dados. Ele começa com `b 0x02131E44` e é o decodificador **Mobiclip** (os vídeos são `data_iz/movie/*.mods`).
- O blob é assembly escrito à mão e usa:
  - `add rX, pc, #k` para achar rotinas;
  - `add lr, pc, #k` + `bx rX` como chamada;
  - `add pc, rT, rB` sobre tabelas de offsets relativos, com `lr` apontando para um ponto de retorno adiante.
- Mudanças no recompilador:
  - `config/boej/code_blobs.tsv` declara regiões de código vistas como dados (o manifesto emite cada uma como função);
  - passada de descoberta: alvos diretos de BL/B sem entrada que caem dentro de uma função, e endereços `pc`-relativos dentro de blobs, viram entradas alternativas, repetindo até estabilizar (5 passadas, +38 entradas);
  - `add lr, pc` / `mov lr, pc` logo antes de `bx rM` / `mov pc, rM` que volta para a instrução seguinte vira chamada, e não salto final.
- As tabelas de offsets com `lr` adiantado ainda não são tratadas. Por isso, por enquanto, o blob inteiro é HLE (`hle_mobiclip_skip`): devolve 0 bytes consumidos, e os contadores de quadros do jogo levam o vídeo até o fim.
- A abertura toca em preto, só com a letra da música renderizada pelo motor 2D.

**Build para Windows**
- O CMake baixa o `SDL3-devel-3.4.18-mingw` (FetchContent) e o `--dist` copia o `SDL3.dll` junto do `.exe`.
- A toolchain ganhou `CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY`: sem isso, o `find_package(SDL3)` achava o SDL3 do Linux.

**Resultado**
- Boot → aviso anti-pirataria (tela de baixo) → toque → abertura (vídeo pulado, letra visível) → **tela de título** (logo em cima; "はじめから" sobre o mapa-múndi embaixo) → abertura de novo, em modo demonstração.
- Na janela, o ritmo fica em 60 fps, com ~50% de um núcleo. O `boej.exe` chega ao mesmo ponto pelo interop do WSL.

**Próximos passos**
1. Motor 3D (GX FIFO, geometria, rasterização) para o campo e as partidas. O título já usa só 2D.
2. Decodificador Mobiclip nativo (o `libavcodec/mobiclip.c` serve de referência) no lugar do blob, com o áudio do vídeo.
3. RTC, driver de som do ARM7 e mapeamento da ROM em memória (o carregamento por `\\wsl$` é lento).

---

## 2026-10-22: Pacote de dados: o dump só é usado na preparação

O pedido: usar a ROM só no processo inicial de cada usuário, e depois rodar sem ela.

**Pacote `boej.pak`** (formato em `tools/ntr/pak.py`)
- Guarda as regiões da ROM que o jogo pode ler, nos offsets originais:
  - header e área segura (0-0x4000);
  - ARM9 e ARM7;
  - FNT, FAT e tabelas de overlay;
  - banner e todos os arquivos do FAT.
- Regiões a menos de 4 KiB umas das outras viram uma só. Cada região tem o offset na ROM, o tamanho e a posição no pacote, alinhada a 4 KiB. Fora das regiões, a leitura devolve 0xFF, como o espaço vazio do cartucho.
- O cabeçalho guarda o código do jogo e o SHA-1 do dump de origem.
- No BOEJ retail, tudo fica contíguo até 0x1BFC3DAC: uma única região de 448 MiB (a ROM tem 512 MiB).
- O jogo lê o cartucho por offset (boot, `CARDi_ReadRomWithCPU`, comando 0xB7). Por isso, manter os offsets dispensou qualquer mudança no lado do jogo.
- O `tools/extract.py` gera o pacote em 2 s, mas só para o dump retail: o código é recompilado dele, e a tradução de fãs muda dados dentro das overlays.

**Runtime** (`runtime/src/rom.cpp`)
- Lê o pacote ou um `.nds` por mmap (`MapViewOfFile` no Windows), no lugar de carregar o arquivo inteiro em um `std::vector`. A interface virou `rom_read`/`rom_read32`.
- Sem argumentos, o `boej` procura o `boej.pak` na pasta do executável (`SDL_GetBasePath`) e depois na pasta atual. O erro explica como gerar o pacote.
- O `tools/build.py` faz um hardlink do pacote ao lado do executável. No `--dist`, copia o pacote, mas só quando ele mudou.

**Verificação**
- Rodando de outra pasta, sem argumentos, o quadro 8000 (tela de título) saiu idêntico byte a byte ao da execução com o `.nds`.
- O `boej.exe` no Windows, sem argumentos, faz 600 quadros em 2,2 s, contra 27,6 s antes: a ROM não é mais lida inteira por `\\wsl$`.
- Novo teste unitário: ida e volta do pacote com uma ROM sintética.

---

## 2026-10-22: Etapa 5: motor 3D

**Do título à história**: tocar em "はじめから" revelou dois bugs nas overlays.
- `overlay_set_active` marcava as páginas de 4 KiB a partir da base sem alinhar. A ov032 (0x02119F00, 0x8C0 bytes) não marcava a página 0x0211A, onde fica o `__sinit`, e a chamada indireta falhava.
- O jogo chama uma função da ov032 logo depois do `FS_EndOverlay`. No hardware o código continua na memória até outra overlay ocupar o lugar. Agora o `FS_EndOverlay` não desativa nada: cada página fica com a última overlay carregada nela.
- Depois disso vêm o aviso sobre ferramentas de modificação de save, a tela de nome (teclado por toque) e a abertura da história. Nesse ponto, `DISPCNT` A = 0x00010F08 (BG0 em 3D) e `POWCNT1` = 0x020F (motor A na tela de baixo).

**Geometria** (`runtime/src/gpu3d.cpp`)
- FIFO empacotado (0x04000400-0x0400043F, comandos sem parâmetro executados na hora) e portas diretas (0x04000440-0x040005FF), com buffers de parâmetros separados. Misturar os dois corrompia as matrizes.
- Pilhas de projeção, posição/vetor (31 níveis, flag de erro) e textura, com MTX_MODE 0-3. SCALE não afeta a matriz de vetor.
- Matrizes em 20.12, aplicadas a vetores linha. A matriz de clip é pos × proj, recalculada só quando muda.
- Vértices: VTX_16/10/XY/XZ/YZ/DIFF, COLOR, NORMAL com iluminação (4 luzes, difusa/ambiente/especular/emissão e tabela de brilho) e os modos de transformação de TEXCOORD (por texcoord, normal ou vértice).
- Polígonos: triângulos, quads e as duas strips (a triangle strip inverte a ordem nos ímpares; a quad strip usa v0 v1 v3 v2).
- Facing pelo produto vetorial em espaço de clip, como no melonDS. Culling pelos bits 6/7.
- Polígonos que cruzam o plano distante são descartados, salvo pelo bit 12. Recorte Sutherland-Hodgman nos 6 planos, em seguida viewport.
- BOX_TEST (conservador), POS_TEST, VEC_TEST, RAM_COUNT, CLIPMTX_RESULT/VECMTX_RESULT e GXSTAT (pilhas, erro, FIFO sempre vazio). A IRQ do GXSTAT dispara sempre que habilitada.

**Renderização**
- O `SWAP_BUFFERS` renderiza a lista no back buffer, que aparece no próximo VBlank. Assim, comandos enviados depois do swap já entram no quadro seguinte, como no hardware.
- Ordem: os opacos primeiro, depois os translúcidos, ordenados por Y (ou na ordem de envio, com o bit 0).
- Rasterização em leque de triângulos, com funções de aresta e interpolação perspectiva-correta (1/w) de cor e texcoords. Buffer Z (z/w → 24 bits) ou W.
- Os 7 formatos de textura (incluindo 4x4 comprimido), com repeat/flip/clamp. Modulate, decal, toon/highlight e sombra (stencil por polígono de ID 0).
- Alpha test, blending, regra de ID de translúcidos, atualização de profundidade dos translúcidos (bit 11), edge marking, névoa (tabela com interpolação, modo só-alpha) e cor/profundidade de limpeza.
- Ainda falta: anti-aliasing, wireframe (alpha 0 não desenha), polígonos de 1 ponto e o plano traseiro em bitmap.
- No 2D, o BG0 em modo 3D mostra o quadro 3D (rolado pelo BG0HOFS). Pixels translúcidos se misturam com a camada de baixo pelo próprio alpha. A captura de tela com fonte "só 3D" também usa o quadro 3D.

**Resultado**
- A abertura da história mostra a cidade em 3D: texturas, sprites-billboard, sombras dos personagens e a câmera em movimento, com os diálogos em 2D por cima.
- Depois vem a exploração livre, com o personagem andando pelo direcional.
- ~250 quadros/s sem janela na cena 3D, e a build de Windows funciona.
- A tela de cima fica preta nessas cenas por escolha do jogo (brilho mestre do motor B = 0x8010).

**Ferramentas**
- `NTR_LOG_3D`: polígonos, culling, recorte e histograma de comandos a cada 60 swaps.
- `NTR_SCREENSHOT_FRAMES`: vários quadros numa execução, com `%d` no caminho.
- `NTR_PEEK` lê MMIO do arquivo de registradores. Ler DISPSTAT pelo caminho normal avançava o tempo e reentrava no VBlank.

---

## 2026-10-22: Etapa 6: som

**O que o jogo usa.** Nos primeiros 12 mil quadros, o ARM9 mandou só 6 comandos SND:
- `SHARED_WORK`;
- `LOCK_CHANNEL 0xFFFF` (trava os 16 canais);
- `SETUP_ALARM` (alarme 0, 0x1474 ticks ≈ 10 ms, id 2) e `START_TIMER` só desse alarme.

Ou seja: o jogo não usa o sequenciador do SDK. Ele tem um mixer próprio no ARM9, ritmado por alarmes do driver de som, e esperava o primeiro alarme para começar. O HLE anterior só confirmava os comandos, então nunca começava.

**Driver** (`runtime/src/arm7_sound.cpp`, reescrito)
- Os alarmes contam em tempo emulado (1 tick = 64 ciclos do barramento). A mensagem PXI tem o formato do callback do ARM9 (`SNDi_PxiFifoCallback` → 0x0200C330): nº do alarme | id << 8. O id é conferido do lado do ARM9.
- Os empacotamentos vêm da descompilação das funções `SND_*` do ARM9:
  - PCM: `ch | timer<<16`, endereço, `len | shift<<22 | vol<<24`, `loopStart | pan<<16 | formato<<24 | loop<<26`, com tamanhos em palavras;
  - PSG/ruído: `vol | shift<<8`, `pan | timer<<8`, duty;
  - captura: destino, tamanho, flags nos bits 27-31;
  - `CHANNEL_TIMER/VOLUME/PAN` por máscara;
  - `START/STOP_TIMER` com máscaras de canal, captura e alarme.
- Pegadinha: o `timer` dos comandos é o **período** (512 → 32728 Hz). O valor do registrador (0x10000 − período) é calculado no ARM7.
- Canais: PCM8, PCM16, IMA-ADPCM (cabeçalho de 4 bytes, estado salvo no início do loop), PSG com duty de 8 passos e ruído por LFSR de 15 bits. Volume com divisor (1/2/4/16) e pan.
- Repetição, one-shot e captura do mix. O status dos canais vai para o shared work (+8) e o finished-command tag avança a cada lista.
- Os comandos do sequenciador (0-11) só geram um aviso, e o jogo não os usa.

**Mixer e saída**
- 32768 Hz estéreo, gerados a partir do tempo emulado (`sound_advance` no `advance_time`). As amostras saem por quadro para um `SDL_AudioStream`, que descarta o excesso quando o jogo roda acima da velocidade real.
- `NTR_WAV` grava a saída para testes sem janela; `NTR_MUTE` desliga o dispositivo; `NTR_DUMP_SND` lista cada comando.

**Como o jogo toca** (pelo dump de comandos):
- Música e vozes: o ARM9 mixa num anel PCM16 de 1024 amostras em loop nos canais 8/9 (32728 Hz) e reabastece meio anel a cada alarme de 0x2000 ticks.
- Áudio dos vídeos Mobiclip: blocos IMA-ADPCM one-shot nos canais 0/1. A música da abertura toca, embora a imagem ainda seja pulada.
- Efeitos: canais disparados com milhares de `CHANNEL_TIMER/VOLUME`.

**Verificação** (sem ouvir, pelo WAV):
- 8000 quadros geram 133,7 s de áudio, o mesmo que 8000/59,83.
- Espectrogramas mostram o jingle do aviso, a música da abertura, a do título, e música e efeitos na história.
- A razão RMS(derivada)/RMS = 0,30 indica sinal musical, não ruído.
- Nenhum comando sem implementação até a exploração.
- O `boej.exe` também grava o WAV.

---

## 2026-10-22: Etapa 7: vídeos Mobiclip

**Como o jogo toca os vídeos** (ov127, pela descompilação)
- O player do jogo lê o contêiner `.mods` (cabeçalho `MODSN3\n`, 256x192, 20 ou 24 fps). Cada quadro traz no fim a palavra do próximo: tamanho em `>> 14`.
- O decodificador recebe só o estado (r0) e devolve os bytes consumidos. O estado tem:
  - +0: ponteiro para os dados do quadro;
  - +4/+8: largura e altura;
  - +0x0C..+0x20: 6 planos Y (o atual e 5 referências);
  - +0x24..+0x38: 6 planos de croma, com Co e Cg lado a lado (128 + 128 bytes por linha);
  - +0x3C/+0x44: tabelas copiadas pelo jogo.
- A conversão YCoCg → RGB555 (`func_ov127_0212a9c0`) e o filtro de blocos (`0x0212c018`) são código C normal do jogo, já recompilado.
- Todos os 63 vídeos do jogo são 256x192, de uma tela só. A tela de cima preta durante a abertura é o comportamento esperado.

**O problema**
- O decodificador é o blob de assembly escrito à mão em 0x02130754 (0x659C bytes).
- Ele chama sub-rotinas pondo em `lr` um ponto de retorno mais adiante e saltando por tabelas de offsets (`add pc, rT, rB`). O modelo recompilado (`bx lr` = retorno da função C++) não consegue seguir isso.

**Solução: interpretador ARM** (`runtime/src/interp.cpp`)
- Interpretador ARMv5TE em estado ARM:
  - processamento de dados com todos os shifts;
  - MRS/MSR;
  - MUL/MLA e multiplicações longas, multiplicações DSP (SMLAxy etc.), QADD/QSUB, CLZ, SWP;
  - LDR/STR, LDRH/LDRSB/LDRSH/LDRD/STRD, LDM/STM;
  - B/BL/BX/BLX, SWI e MCR/MRC.
- `interpret_arm(c, entry)` executa até voltar ao `lr` da entrada. Ele busca as instruções da memória do guest, então funcionaria também com o código copiado para a ITCM.
- Thumb e LDM/STM no banco de usuário param com `unhandled` (o blob não usa).
- O hook `mobiclip_skip` virou `mobiclip_decode`, que roda o blob original no interpretador. É bit a bit o decodificador do jogo, sem reimplementar o codec.

**Resultado**
- A abertura (`op00f.mods`, 2000 quadros a 20 fps) toca inteira com imagem:
  - logo Mobiclip;
  - cenas animadas com a letra;
  - logo do jogo;
  - tela de título no fim, como antes.
- As cores conferem (conversão YCoCg do próprio jogo).
- Custo: 3000 quadros em modo rápido passaram de 6,7 s para 8,7 s, ~0,7 ms por quadro de jogo. O orçamento é de 16,7 ms.
- O build para Windows compila.

---

## 2026-10-22: Etapa 8: relógio (RTC)

**Protocolo** (pela descompilação de `RTCi_SendPxiCommand` e `RTCi_CallbackFromPxi`)
- O ARM9 manda o comando nos bits 8-14 da tag 5. O ARM7 responde `comando << 8 | resultado` (0 = sucesso). Respostas com `0x30xx` seriam a interrupção de alarme.
- As leituras ficam em memória compartilhada (literais em 0x0200B464-0x0200B470):
  - 0x02FFFDE8, data: ano BCD desde 2000 (bits 0-7), mês (8-12), dia (16-21), dia da semana (24-26). O SDK recalcula o dia da semana a partir da data.
  - 0x02FFFDEC, hora: horas (0-5), minutos (8-14), segundos (16-22), em 24 horas.
- Comandos: 0x10 data+hora, 0x11 data, 0x12 hora. O resto (status, alarmes e escritas, como o 0x27 que grava o status de alarme) é só confirmado.

**Implementação** (`runtime/src/arm7_rtc.cpp`)
- Usa a hora local do PC. Uma leitura é deixada na memória já no boot, como faz o firmware.
- `NTR_RTC="AAAA-MM-DD HH:MM:SS"` parte de um momento fixo e avança com o tempo emulado (ciclos / 33.513.982), para testes reproduzíveis.
- A tag 5 saiu da lista de serviços que só registram pedidos.

**Quem usa o relógio:** as overlays 15 (várias chamadas a `RTC_GetDate`/`RTC_GetDateTime`), 17 e 131 (`RTC_GetTime`). Nenhuma delas pede a hora no caminho testado até a exploração livre.

**Verificação**
- Chamada temporária ao `RTC_GetDateTime` do próprio jogo (com IRQ do PXI e callback do SDK), feita de dentro do escalonador de tarefas e removida depois.
- Hora do PC: 2026-10-22, quinta-feira (dia 4), 17:02:06.
- Com `NTR_RTC="2010-07-01 23:59:58"`: 2010-07-02, sexta-feira, 00:00:11. A virada do dia e o dia da semana estão certos.
- Builds Linux e Windows compilam.

---

## 2026-10-22: Atalhos para testar mais rápido

O pedido: um atalho de avanço rápido e outro para passar as mensagens.

**Atalhos** (`runtime/src/host.cpp`)

| Atalho | Teclado | Controle |
|---|---|---|
| Acelerar | Tab segurado (já existia) ou F2 liga/desliga | gatilho direito |
| A automático | C segurado ou F3 liga/desliga | gatilho esquerdo |

- **Acelerar** tira o limite de 59,83 Hz. A janela só redesenha 1 de cada 4 quadros, para o desenho não segurar a velocidade. O jogo continua renderizando todos os quadros.
- **A automático:** A fica 3 quadros apertado e 3 solto, então o jogo vê um novo toque a cada 6 quadros. Ele soma com o teclado, o controle e o `NTR_INPUT`. Como também confirma menus, convém desligá-lo antes de escolhas.
- O título da janela mostra `[>> acelerado]` e `[A automatico]` quando estão ligados.

**Verificação**
- Teste temporário (removido depois) injetou F2 e F3 como eventos SDL na janela.
- O A automático alterna certo: quadros 12-14 com A, 15-17 sem.
- Acelerado com janela: 1500 quadros em 6,1 s, cerca de 245 fps, ~4x o normal.
- Ao desligar no quadro 1500, volta a 60 fps.
- Windows compila.

---

## 2026-10-22: Diagnóstico: log em arquivo, gravação de sessão, F8 e watchdog

O relato: na parte do flashback do Endo, a tela fica toda branca e o jogo parece travar. O terminal só mostrava overlays (… 0, 127, 126, 129, 126), sem erro. O pedido: logs melhores e em arquivo.

**Primeira análise**
- O roteiro de teste para na exploração livre (quadro ~14000). Não alcança o flashback sem saber o caminho.
- A única instrução não traduzida da ov129 (que carrega logo antes) é dado depois de um desvio sempre tomado (`__sinit`). Não é a causa.
- A sequência do log mostra um vídeo (ov127 e a thread do player) tocando antes da ov129.
- Sem reproduzir, a causa fica em aberto. A solução foi dar ferramentas para que uma sessão real possa ser repetida aqui.

**`runtime/src/diag.cpp`**
- **Log em arquivo:** `boej.log` ao lado do executável. `NTR_LOG_FILE` muda o caminho; vazio desliga.
  - Cada linha leva o número do quadro.
  - `NTR_LOG` vira `log_printf` (terminal + arquivo, com mutex e flush por linha). `log_detail` só vai para o arquivo.
  - A sessão anterior fica em `boej.old.log`.
  - Os erros do `main` (pak ausente, janela) também vão para o log.
- **Arquivos abertos:** hooks em `FS_OpenFileEx` (caminho) e `FS_OpenFileFast` (id) registram cada arquivo, só no arquivo de log.
- **Falhas:** handlers de SIGSEGV/SIGILL/SIGFPE/SIGABRT, e `SetUnhandledExceptionFilter` no Windows. Gravam o quadro e um dump de estado.
- **Watchdog** (só com janela): uma thread nota se passam 5 s sem quadro, grava um dump e avisa quando os quadros voltam.
- **Dump de estado** (também no F8, junto com uma captura de tela):
  - DISPCNT, MASTER_BRIGHT, POWCNT1, BLDCNT/BLDALPHA/BLDY dos dois motores;
  - VRAMCNT, DISP3DCNT, IME/IE/IF;
  - overlays ativas (`overlay_active_list` em cpu.cpp);
  - tarefa atual do jogo;
  - threads do SDK com o estado de cada uma (OSThreadInfo em 0x01FF81AC: +4 atual, lista por +0x68, estado em +0x64).
- **Gravação de sessão:** `boej-replay.txt` no formato do `NTR_INPUT` (um intervalo por linha, gravado assim que termina).
  - O cabeçalho traz `# rtc <início do relógio>` e o save de partida, copiado para `boej-replay.sav`.
  - Grava sempre que há janela e nenhuma entrada roteirizada; `NTR_RECORD` força a gravação.
  - `NTR_REPLAY=arquivo` repete a sessão.
  - O estado gravado no quadro F é o que o jogo vê até o quadro F+1: depois dos eventos, ou do roteiro sem janela.

**Determinismo**
- O relógio agora sempre parte de um instante fixo e avança com o tempo emulado. O início é a hora do PC no boot, ou `NTR_RTC`, ou o que veio na gravação.
- Assim a execução depende só do início do relógio, do save e da entrada. O avanço rápido não muda nada, porque só tira o limite de ritmo.

**Verificação**
- Gravei o roteiro da história (14000 quadros) com `NTR_RECORD` e repeti pela gravação. As capturas dos quadros 9000 e 14000 ficaram **idênticas byte a byte**.
- `boej.log` com quadros e 324 aberturas de arquivo (`/data_iz/script/eve.pkb`, `/data_iz/sound/J14.SAD`…).
- SIGSEGV enviado a uma sessão com janela: `[fatal] … at frame 350` mais o dump completo no log.
- Watchdog: uma pausa temporária de 7 s no quadro 200 gerou o aviso e o dump, e "frames are coming again" no 235. A pausa foi removida depois.
- `boej.exe` numa pasta do C: grava `boej.log` e `boej-replay.txt` ao lado do executável.
- No MinGW, o atributo de formato virou `gnu_printf` (`NTR_PRINTF_FORMAT`), por causa do `%zu`.

**Próximo passo:** com o `boej-replay.txt` + `boej-replay.sav` (ou o `.old`) da sessão do flashback, repetir aqui até a tela branca e investigar com os dumps.

## 2026-10-22 — Tela branca no flashback do Endo: DS Protect (antipirataria)

**Reprodução.** O usuário jogou até o flashback (jogo novo, ~22000 quadros), apertou F8 na tela branca e mandou `boej.log` + `boej-replay.txt`. A repetição aqui deu a captura do quadro 21826 **idêntica byte a byte** à dele.
- Dump: `MASTER_BRIGHT A 4010 B 4010` (as duas telas em branco total), quadros normais, nenhuma thread presa. O fade de entrada nunca começava.
- Logo antes, a ov129 carregava. Ela é inteira DS Protect.

**Como o DS Protect é chamado** (ov131 0x021490F8, ao entrar na cena):
- Seis "portões" de 0x3C bytes (0x0212AD60…0x0212AE8C, e outros 11 iguais) põem em r12 o endereço de um bloco de parâmetros e saltam com `stmdb sp!,{r12}` … `ldmia sp!,{pc}` para 0x0212B7A8.
- Esse despachante descriptografa a função real (0x0212B610), chama, criptografa de novo (0x0212B6D0) e volta ao chamador.
- Cada detecção (0x0212A9C0…) soma o resultado de duas verificações. Se a soma não for múltipla de 241, chama o callback (r0) com r1 = função de penalidade da ov131 (0x02148FF8, 0x02149038, 0x02149078). As versões "Not" funcionam ao contrário.

**Três problemas, achados com hooks temporários (já removidos) que registravam o retorno dos portões e as penalidades:**
1. **Recompilador:** o `ldmia sp!,{pc}` virava `return`, então o portão voltava sem rodar nada e o jogo via o ponteiro do callback como resultado. Agora, numa função ARM que nunca guarda lr, um `ldmia sp!,{pc}` (só pc) é salto indireto. No código gerado, só os 17 portões da ov129 mudaram. A primeira versão, sem exigir "só pc", também mudava funções cujo lr é guardado por outro pedaço; foi descartada.
2. **Detecção de flashcart** (0x0212BACC): lê o cartucho direto (comando B7 via ROMCTRL) em 0x1000/0x1200/0x1400 e compara com 0x8000/0x8200/0x8400 lidos pelo SDK. Num cartucho real, B7 abaixo de 0x8000 devolve os dados de 0x8000 + (addr & 0x1FF). O `card_start` (io.cpp) agora faz igual.
3. **Detecção de emulador** (0x0212B824): falha com MAC zerado, ou com o MAC padrão dos emuladores 00:09:BF:00:00:31 junto com aniversário 1/1 e mensagem vazia. O boot agora grava o MAC 00:09:BF:4E:54:52 em 0x027FFCF4.

**Resultado:**
- Os seis portões passam e nenhuma penalidade é chamada.
- O flashback segue: o fade abre, e a fala da mãe ("…守! 守ー 起きなさい!") aparece e fica esperando o A, porque a gravação acaba ali.
- O roteiro da história continua idêntico byte a byte nos quadros 9000 e 14000.

## 2026-10-22 — Crash ao iniciar a primeira partida (contra os Outlaws, para salvar o Toramaru)

**Sintoma.** No quadro 30681, ao começar a partida, o log mostra `[cpu] unhandled at 0213A3AC: indirect call to unknown address`; lr 01FF9D30 (ITCM).

**Causa.** 0x0213A3AC é `func_ov126_0213a3ac`, e a ov126 continuava carregada. A tabela de overlays ativas era por página de 4 KiB:
- a ov126 termina em 0x0213ACC0 e a ov132 começa exatamente ali, na mesma página 0x0213A000;
- ao carregar a ov132 (quadro 29912), a página inteira passava a ser dela, e as funções da ov126 entre 0x0213A000 e 0x0213ACBF ficavam inalcançáveis.

**Correção** (cpu.cpp). A tabela agora tem um `uint16_t` por bloco de 32 bytes da RAM principal (alinhamento das overlays), 256 KiB no total.

**Verificação:**
- A repetição da sessão do usuário passa do quadro 30681 e a partida começa (placar 0×0, 15:00, campo com os jogadores).
- O roteiro da história continua idêntico byte a byte nos quadros 9000 e 14000.

## 2026-10-22 — Mapa da tela de cima não aparecia: tabela de `switch` cortada pelo recompilador

**Sintoma.** Na exploração, a tela de cima ficava preta. O dump mostrou o motor B (tela de cima) com BG0 + OBJ ativos, mas a VRAM de BG do banco H zerada e a OAM do motor B toda escondida.

**Investigação** (instrumentação temporária, já removida):
- Armadilha de escrita em BG/OBJ/OAM do motor B: depois da entrada no campo (quadro ~2136), só houve limpezas e a carga dos gráficos de OBJ. O desenho do mapa nunca acontecia, e o `map2d.pkb` (arquivo 398) nunca era aberto; só o índice `map2d.pkh`.
- Com o perfilador (`--trace`, `NTR_PROFILE`), achei o objeto do mapa (ov000, atualizado por `func_ov000_020c9110`). Ele ficava preso no estado 9: entrava com 9 e saía com 9.
- No original, `case 9` sempre termina em estado 3. O código gerado só tinha `case 0..2`.

**Causa.** `switch_case_count` (translator.cpp) só aceitava o `cmp rN, #max` logo antes do `addls pc, pc, rN, lsl #2`. Aqui havia um `mov r5, #1` no meio:
- sem o `cmp`, o recompilador contava a tabela até a primeira entrada que não fosse `b`;
- a entrada 3 é um `ldmia sp!, {…, pc}` (retorno), então só sobravam 3 casos, e os estados 3–9 caíam no default, que retorna.

**Correção.** O `cmp` vale até 24 bytes antes do dispatch condicional, desde que as instruções no meio sejam processamento de dados sem S, incondicionais e sem escrever em rN.
- Mudaram 10 tabelas no jogo, todas conferidas com o `cmp`: #9, #8, #8, #14, #9, #5, #5, #5, #6 e #8.
- Algumas tinham perdido casos reais, como a do mapa. Outras tinham casos a mais (código depois da tabela), o que era inofensivo.

**Resultado:**
- O mapa da tela de cima aparece: nome da área (住宅街 / 未来ストリート), o mapa com marcadores e o objetivo ("風丸たちと合流だ！スポーツショップへ！").
- Gravações feitas antes desta correção deixam de repetir a mesma sessão, porque agora o jogo se comporta de outro jeito.

**Ferramentas que ficaram:**
- O dump de estado agora traz os nove VRAMCNT (A–I) e BG0–3CNT dos dois motores.
- Cada captura de `NTR_SCREENSHOT_FRAMES` também grava o dump, como no F8.

## 2026-10-22 — Etapa 9: Wi-Fi/DWC e comunicação sem fio desativados

**Onde o jogo usa o rádio.** Três overlays chamam a biblioteca WM do SDK (main, 0x02011E14 em diante):
- ov015: DWC/Nintendo WFC (strings "NINTENDOWFC", "Wayport", "FREESPOT");
- ov017 e ov128: provavelmente a comunicação local (código Thumb).

Também há uma função do jogo em main (0x0203E004, chamada de 0x0204655C) que abre a sessão sem fio. Todas passam por `WM_Initialize` (0x020130B4).

**Protocolo** (lido no código):
- O ARM9 pega um buffer de comando de um pool (0x02096BE0, 10 × 0x100, bit 0x8000 = livre), escreve `[0]` = id da API e os argumentos, e manda o ponteiro pela FIFO com a tag 10 (WMW).
- O ARM7 escreve o resultado no mesmo buffer (`[1]` = código de erro) e devolve o ponteiro. O callback de recepção (0x02012344) chama a função registrada para aquela API.

**Problema.** O runtime registrava a tag 10 só para log. Assim o `WM_Init` achava a biblioteca do ARM7 pronta, e qualquer pedido ficaria sem resposta, com o jogo esperando para sempre.

**Decisão.** Não registrar a tag 10.
- O `WM_Init` (0x02011E14) testa `PXI_IsCallbackReady(10, ARM7)` antes de tudo e devolve 4 (`WM_ERRCODE_WM_DISABLE`). É a mesma resposta de um console cujo ARM7 não tem o sem fio. Nenhuma outra parte do jogo consulta a tag 10.
- O `WM_Initialize` repassa esse erro, e a função do jogo em 0x0203E004 trata qualquer resultado diferente de 2 (`OPERATING`) como falha e devolve 0.
- O DWC (ov015) inicializa pelo mesmo `WM_Initialize`, então a conexão Wi-Fi também falha logo no começo.
- Responder com erro a cada comando, em vez disso, levaria as máquinas de estado do WM a tentar `WM_End` e repetir. O "indisponível" logo no início é mais simples e fiel.

**Verificação:**
- Um teste temporário (já removido) chamou o `WM_Init` e o `WM_Initialize` do próprio jogo no quadro 300: os dois devolveram 4 na hora.
- O roteiro da história continua idêntico byte a byte no quadro 14000.
- Ainda não vi os menus de comunicação dentro do jogo. Eles pedem um save ou bem mais tempo de jogo; fica para o teste do usuário (a mensagem de erro do próprio jogo deve aparecer, sem travar).

## 2026-10-22 — Etapa 10: resolução interna do 3D, anti-aliasing e wireframe

**Resolução interna** (`NTR_3D_SCALE`, 1 a 4; padrão 2 com janela e 1 sem janela, para os testes continuarem no tamanho nativo):
- **3D** (gpu3d.cpp): os buffers de cor, alfa e atributos agora são do tamanho 256·N × 192·N, e os vértices são multiplicados por N só na rasterização. Na troca de quadro sai também uma cópia nativa (uma amostra no centro de cada pixel nativo), que é o que o motor 2D e a captura de tela do DS leem. O jogo vê a mesma imagem 3D em qualquer escala.
- **2D** (gpu2d.cpp): cada linha continua sendo desenhada e composta em 256 pixels.
  - Para a janela, as linhas do motor A que mostram 3D são compostas de novo em cada sub-linha: o 3D grande, as camadas 2D esticadas e os mesmos efeitos (alfa do 3D, blending, janelas, brilho mestre). O `compose` ganhou escala e uma fonte opcional para o BG0. O BG0HOFS é multiplicado por N.
  - As outras linhas (2D puro, motor B, tela desligada) são repetidas N×N.
- **Host:** `host_present(top, bottom, scale)`, com textura do tamanho certo criada sob demanda. As capturas (F12, F8 e as roteirizadas) saem no tamanho da janela.
- **Marcação de borda:** os vizinhos ficam a N pixels, para o contorno manter a espessura de 1 pixel nativo.

**Anti-aliasing** (DISP3DCNT bit 4; o jogo liga, com 0x1B):
- Cada pixel opaco guarda o que havia embaixo (cor e alfa) e a cobertura: 0,5 na borda do polígono e 1 a meio pixel para dentro. A distância é calculada só nos lados do polígono, não nas diagonais da triangulação em leque.
- Uma passada final mistura com o que está embaixo só nas silhuetas, isto é, onde um vizinho tem outro ID de polígono ou não é pixel opaco. Sobre o fundo vazio, a cobertura vira o alfa do 3D, e a camada 2D de baixo aparece.
- É uma aproximação, conservadora de propósito: bordas entre polígonos do mesmo modelo continuam sólidas, para não aparecerem emendas. No campo, a 1×, poucos pixels mudam.

**Wireframe** (alfa 0): antes esses polígonos eram ignorados. Agora só a borda é desenhada, opaca e com 1 pixel nativo de largura.

**Desempenho** (roteiro da história, 3000 quadros, sem janela): 3,2 ms/quadro a 1×, 4,0 a 2×, 5,0 a 3× e 6,5 a 4×. O orçamento é 16,7 ms.

**Verificação:**
- No campo (quadro 14000), a 2× e a 4× o cenário fica liso (calçada, meio-fio, poste, carro). Os personagens mantêm o pixel art da textura, e o ícone 2D é esticado.
- Telas 2D (quadro 9000) saem iguais em qualquer escala.
- A 1×, a imagem difere da versão anterior só em poucos pixels de silhueta (anti-aliasing).

**Ajuste: avanço rápido mais lento com a resolução maior.** O usuário notou que o avanço rápido ficou mais lento. A janela só mostra 1 de cada 4 quadros no avanço, mas todos eram renderizados a N× e recompostos em tamanho grande.
- Agora, durante o avanço, o 3D renderiza no tamanho nativo e a imagem grande não é montada (`host_fast_forward()`).
- A escala muda só no `render_frame`. O quadro da frente mantém o tamanho até a troca (`g_front_scale`), então ligar ou desligar o avanço não apaga a imagem.
- Teste com um gancho temporário (já removido) que ligava e desligava o avanço a cada 500 quadros: as capturas mudam de 512×768 para 256×384 e voltam, sem quadro em branco.
- No avanço, o custo volta ao do modo nativo (cerca de 3,2 ms por quadro, contra 4,0 ms a 2×).

## 2026-10-22 — Crash na tela de vitória, FPS baixo e som engasgando em cenas 3D pesadas

O usuário jogou do começo até a luta contra os que importunavam o Toramaru, apertou F8 nos pontos em que o som engasgava (quadros 16691, 20252 e 34869) e mandou o log e a gravação. A repetição bate byte a byte com as três capturas do F8.

**Crash no quadro 35591** (tela バトル勝利, ov083): `indirect call to unknown address` em 0x0211AD48.
- Era outra forma de `switch`: `cmp r0,#0xa; ldmiahi sp!,{…,pc}; mov r4,r8; add pc,pc,r0,lsl #2`. O limite vem de um retorno condicional (não de um `bhi`), o despacho é incondicional, e as primeiras entradas da tabela também são retornos. Sem o limite, o recompilador não reconheceu a tabela, e o caso 8 virou chamada para um endereço no meio da função.
- `switch_case_count` reescrito: para cada `cmp rN,#max` candidato, olha para a frente até o despacho. Ele vale se o despacho for LS/LE com as flags intactas, ou se houver uma saída HI/GT (desvio ou `ldm` com pc) logo depois dele com as flags intactas. Nada no caminho pode escrever em rN.
- A forma com sinal (`cmp r1,#9; bgt; cmp r1,#0; addge pc`) continua funcionando: a primeira versão parava no `cmp #0` e encolhia 4 tabelas; foi corrigida antes de seguir. No código gerado, só a tabela da ov083 mudou (casos 0–10).
- O teste de `ldr pc,[pc,rN,lsl #2]` usava a máscara `0x0FFF0FF0`, que nunca casava com `0x079FF100` (o compilador avisava). Agora é `0x0FFFFFF0`. O jogo não usa essa instrução, então nada mudou.
- Agora a tela de vitória mostra os pontos, os "level up" e o item (赤いコイン).

**FPS baixo e som engasgando.** Medição por quadro na repetição (sem janela, a 2×): média de 12,3 ms, 14.031 quadros acima de 16,7 ms e picos de 53 ms perto das marcas (mapa 3D do capítulo, flashback com imagens grandes translúcidas, supertécnica Dragon Crash). Com o `perf`, 78% do tempo estava no rasterizador (`draw_polygon` 49%, `sample` 29%) e 13% na recomposição a 2×. Quando um quadro passa de 16,7 ms o jogo atrasa e o buffer de som esvazia: o engasgo vinha daí.

Otimizações, mantendo a imagem idêntica:
- **Faixa por linha:** cada triângulo calcula, em cada linha, o intervalo de x onde os centros podem estar dentro (alargado em 1 pixel de cada lado). O teste exato por pixel continua o mesmo, por isso o resultado não muda.
- **Estado por quadro:** as views de textura e paleta são buscadas uma vez por quadro (antes, uma chamada por texel), o DISP3DCNT é lido uma vez por polígono e não mais por pixel, e `VramView::ptr` só divide quando o offset passa do tamanho.
- **Faixas em paralelo:** um pool de até 8 threads (`NTR_3D_THREADS`). A thread k desenha as linhas y com y % N == k, passando por todos os polígonos na ordem da lista, então a imagem é a mesma com qualquer N. O pós-processamento (marcação de borda, AA, névoa) roda numa segunda passada, depois que todas as faixas terminam, porque lê as linhas vizinhas. Ele agora escreve no lugar, já que lê só o buffer de atributos.
- O pool nunca é destruído e as threads ficam desanexadas: com o pool estático, a saída do programa travava ao destruí-lo com as threads esperando.

Resultado (sessão inteira, 36.000 quadros a 2×):

| | antes | 1 thread | multi-thread |
|---|---|---|---|
| média por quadro | 12,3 ms | 10,6 ms | 6,5 ms |
| quadros acima de 16,7 ms | 14.031 | 7.807 | 6 |
| perto das marcas (média / pico) | 23 / 53 ms | 19 / 42 ms | 7,7 / 14 ms |

- Imagens de referência (8 quadros pesados, a 1× e a 2×) **idênticas byte a byte** antes e depois. O roteiro da história continua idêntico no quadro 14000.

**Diagnóstico novo:** com janela, cada segundo que tiver quadros acima de 16,7 ms (fora do avanço rápido) gera `[perf] N frames took longer than 16.7 ms in the last second (worst X ms, frame F)` no `boej.log`. Testado no WSLg a 4× com uma thread só.

## 2026-10-23 — Crash ao abrir o menu de save, cópias curtas de memória e pontos invisíveis na recarga de TP

O usuário jogou até depois da primeira partida, apertou F8 num ponto azul de recarga de TP (quadro 91921, a caixa "ねっけつ ___P" sem o número) e o jogo fechou ao abrir o menu para salvar (quadro 105317). A repetição bate byte a byte com a captura do F8.

**Crash no menu (ov061, 0x0211A954).** Outra tabela de `switch` descartada: entre `cmp r1,#0xe` e `addls pc` havia um `ldrh r0,[r0]`. A regra só aceitava processamento de dados no meio, mas loads/stores não mexem nas flags. Agora `keeps_flags` aceita processamento de dados sem S e loads/stores de palavra e meia-palavra, e `writes_reg` também reconhece `ldrh` em rN. Isso também consertou uma tabela da ov016 (0x02106AC0, casos 0–4).

**Duff's device nas cópias do SDK** (0x02007A1C, 0x02007C60, 0x02007F50): para 1 a 8 bytes, entram no meio de uma sequência desenrolada com `rsb r3,r2,#8; add pc,pc,r3,lsl #2` (ou `lsl #3`). O limite vem de outro registrador, então nenhuma regra servia, e o salto viraria crash. Agora existe `straight_line_cases`: se a tabela não é de desvios, os casos são as entradas a cada 4 ou 8 bytes até a primeira saída incondicional, e nenhuma instrução no caminho pode ser desvio. Os três viraram `switch` de 9 casos (o 8 é o `bx lr`). O único `add pc` restante está nas divisões, que rodam em HLE. Uma varredura de todas as tabelas comparou o número de casos com o `cmp` que limita cada uma; as formas `cmp; bgt; subs; addpl` batem com X − base.

**Pontos invisíveis.** Investigação (ganchos temporários, já removidos):
- A tela de baixo do campo é **toda 3D** (com o 3D desligado fica preta). A caixa é um quadrilátero texturizado, e cada dígito é um quadrilátero de 8×16 com uma tira de textura de dígitos (0x6E836450, 8×256, 16 cores).
- No caminho foram descartadas outras causas: o `sprintf` do jogo (MSL, 0x02023C40) formata "%7d" certo; a conversão para caracteres largos (ov016 0x020F1F5C) também; a busca de glifos (`func_020182ac`) nunca falhou; as fontes têm os dígitos; nenhum DMA temporizado é usado.
- Os dígitos tinham **a mesma profundidade** do painel (204288) e chegavam depois dele na lista, então o teste "menor que" os rejeitava.

**Causa: faltava a ordenação de polígonos do hardware.** O DS desenha os opacos primeiro, ordenados pela linha de baixo e depois pela de cima, e os translúcidos depois, ordenados do mesmo jeito, exceto com o bit 0 do `SWAP_BUFFERS` (ordem manual). O port só ordenava os translúcidos e deixava os opacos na ordem de envio. Agora a chave é `(linha de baixo << 8) | linha de cima` em linhas inteiras, com `stable_sort`, para os empates manterem a ordem de envio. Os dígitos (até a linha 118) passam a ser desenhados antes do painel (até a 128), e o painel, na mesma profundidade, não os cobre.

**Resultado:**
- A caixa mostra "ねっけつ 203P".
- O menu de save abre. Os campos (nível do time, なかま, tempo, nome, vitórias) ficam em branco porque mostram o save que já existe no slot, e nessa sessão o jogo nunca tinha salvado. Falta conferir depois de salvar de verdade.
- O roteiro da história continua idêntico byte a byte no quadro 14000.

## 2026-10-23 — Etapa 11: configurações (`boej.ini`) e controles remapeáveis

**Arquivo de configurações** (`runtime/src/config.cpp`): `boej.ini` ao lado do executável (`NTR_CONFIG` escolhe outro). É um INI simples (`[seção]`, `chave = valor`, comentários com `;` ou `#`, chaves sem diferença de maiúsculas).
- Os padrões são o próprio texto do arquivo, embutido no executável. Ele é lido primeiro, e o arquivo do jogador por cima. Assim, um arquivo antigo ou com chaves apagadas continua recebendo o padrão no que faltar.
- Na primeira execução com janela, o arquivo é criado com esse texto, comentado em português.
- Valores inválidos vão para o `boej.log` com arquivo e linha (`[config] boej.ini:3: "9" is not a number from 1 to 4`), e o padrão é usado no lugar.
- Sem janela, o arquivo só é lido com `NTR_CONFIG`, para os testes e as repetições não dependerem das configurações de quem joga. As variáveis `NTR_SCALE`, `NTR_3D_SCALE` e `NTR_MUTE` continuam com prioridade.

**Opções:**
- `[video]`: `window_scale`, `resolution_3d` (1 a 4), `fullscreen`, `filter` (`nearest`/`linear`, a escala da textura no SDL) e `integer_scale` (`SDL_LOGICAL_PRESENTATION_INTEGER_SCALE`).
- `[audio]`: `volume` de 0 a 100 (ganho do `SDL_AudioStream`; o WAV do `NTR_WAV` não muda).
- `[keyboard]` e `[gamepad]`: uma linha por botão do DS e por atalho (`fast_forward`, `fast_forward_toggle`, `turbo`, `turbo_toggle`, `screenshot`, `debug_dump`, `fullscreen`), com várias entradas separadas por vírgula. Vazio desliga.
  - Teclas pelos nomes do SDL (`SDL_GetKeyFromName`).
  - Controle: botões pela posição (`south/east/west/north`, para "east" ser o mesmo botão em qualquer controle) ou pelos nomes do SDL (`start`, `back`, `dpup`...). Eixos com direção (`lefty-`), acionados a meio curso. Por padrão, o analógico esquerdo também move o direcional (novo).
- Atalho novo: **F11** alterna a tela cheia.

**Entrada reescrita no host:** cada fonte (teclado e controles) calcula a máscara das ações seguradas, e a combinação das duas define os botões do DS e os atalhos de segurar. Os atalhos de apertar (alternar, captura, F8, tela cheia) disparam na borda de subida da máscara combinada. Isso substituiu as teclas fixas (`key_bit`/`gamepad_bit`) e os campos separados do controle (`pad_keys`, `ff_pad`, `turbo_pad`).
- Os controles abertos ficam numa lista e são fechados quando desconectados (antes nunca eram).
- Ao perder o foco, a janela solta todas as teclas: o key-up vai para outra janela, e antes o botão ficava preso.

**Verificação:**
- O roteiro da história, sem janela, continua idêntico byte a byte nos quadros 9000 e 14000.
- Com janela (WSLg), sem arquivo: o `boej.ini` criado é igual ao texto embutido.
- Arquivo com erros de propósito (`resolution_3d = 9`, `filter = blurry`, `fullscreen = talvez`, tecla e eixo inexistentes, linha sem `=`): cada um gerou o aviso com a linha certa, o 3D ficou em 2× e a janela abriu a 3× (768×1152).
- Gancho temporário (já removido) que injetava eventos de teclado no SDL, com `A = K` e `B =`: K apertou A, Z não fez nada, ↑ apertou UP, perder o foco soltou tudo, F2 ligou o avanço e F11 ligou a tela cheia.
- Não testado: controle físico (não há um no WSL). O caminho é o mesmo das teclas, com o estado lido por `SDL_GetGamepadButton`/`SDL_GetGamepadAxis`.
- Build de Windows compilada sem avisos (copiada para `build/dist-win`).

**Próximo passo** pelo plano: release portátil para Windows (pacote com `boej.exe`, `SDL3.dll` e um jeito de gerar o `boej.pak` sem Python, a partir do dump do jogador).

## 2026-10-23 — Etapa 12: pacote portátil para Windows; o executável prepara o `boej.pak` sozinho

**Primeira execução sem Python** (`runtime/src/setup.cpp`, `host_first_run` em host.cpp):
- Sem `boej.pak`, o jogo, com janela, mostra uma mensagem ("Escolher o dump..." / "Sair") e abre o diálogo de arquivo do sistema (`SDL_ShowOpenFileDialog`, filtro `.nds`).
- Um `.nds` passado na linha de comando, que é o que acontece ao arrastá-lo para cima do `boej.exe`, faz o mesmo sem perguntar. Se o pacote já existe, um `.nds` na linha de comando roda direto, como antes (uso de desenvolvimento).
- O dump é conferido por SHA-1 (implementação própria; conferida com o `sha1sum` num arquivo de tamanho não múltiplo de 64). Só o retail gera pacote:
  - a tradução de fãs recebe "versão modificada que ainda não é suportada";
  - um arquivo qualquer recebe o SHA-1 encontrado e o esperado.
- O pacote é gravado ao lado do executável, por um `.tmp` renomeado no fim, no mesmo formato do `tools/ntr/pak.py` (mesmas faixas, fusão e alinhamento).
- O trabalho roda numa thread. A janela continua respondendo e mostra uma barra de progresso, com texto pela fonte de depuração do SDL (só ASCII). A metade inicial é o hash, a outra a gravação.
- Em erro, o diálogo volta a aparecer. Fechar a janela sai.
- A tabela de dumps conhecidos fica no C++. Um teste novo confere que ela bate com o `config/boej/dumps.json`.
- Para isso, a janela agora abre antes da ROM (`host_init` vem primeiro no `main`).

**Correções que a versão portátil pedia:**
- **Save ao lado do executável.** O padrão era `saves/boej.sav` relativo à pasta atual. Abrindo por um atalho, ou arrastando o dump para o executável, a pasta atual pode ser outra, e o save "sumiria". Agora é `<pasta do executável>/saves/boej.sav`. Um save que só exista em `saves/` da pasta atual, onde as versões anteriores gravavam, é copiado uma vez, e o original fica onde está.
- **Caminhos com acentos no Windows.** Com `std::string`, o `fopen`/`fs::path` interpretam o caminho na página de código ANSI. Então log, gravação da sessão, configurações, capturas e save falhariam numa pasta como `C:\Users\João`.
  - Agora há `fs_path`/`utf8_string`/`open_file` (UTF-8 → `_wfopen` no Windows), usados em todos esses lugares.
  - Os argumentos vêm de `CommandLineToArgvW`, convertidos para UTF-8.
- **Console:** aberto pelo Explorer, o console pertence só ao `boej.exe` (`GetConsoleProcessList` == 1) e é escondido (`FreeConsole`). De um terminal, a saída continua nele, e `NTR_CONSOLE=1` mantém o console.

**Build:** `tools/build.py --windows --zip` gera `build/ie3ogre-recomp-win64.zip` (14 MiB), com `boej.exe`, `SDL3.dll` e `LEIA-ME.txt` (`game/LEIA-ME.txt`: primeira vez, arquivos criados, controles). Ele não leva o `boej.pak`. O executável tem código do jogo recompilado, então o zip continua sendo de uso pessoal.

**Verificação:**
- Linux, executável copiado para uma pasta vazia, com o dump retail como argumento: o `boej.pak` gerado ficou **idêntico byte a byte** ao do `tools/extract.py`, e o jogo rodou 600 quadros a partir dele.
- Na segunda execução, sem argumentos, a partir de outra pasta atual com um `saves/boej.sav`: o save foi copiado para a pasta do executável e carregado.
- Erros (gancho temporário, já removido, chamando `pak_create` direto): tradução de fãs, arquivo aleatório de 1 MB e arquivo inexistente deram as mensagens certas, e nenhum `.tmp` ficou para trás.
- **Windows** (`boej.exe` do zip, via interop do WSL), numa pasta `%TEMP%\Jogo ção` com o dump `dump ção.nds`:
  - o pacote saiu idêntico byte a byte;
  - log, `boej.ini` e gravação da sessão foram criados na pasta com acento;
  - preparação + 300 quadros em 8 s;
  - a segunda execução, sem argumentos e sem o dump, abriu pelo pacote;
  - pasta apagada depois.
- O roteiro da história, sem janela, continua idêntico byte a byte nos quadros 9000 e 14000. Testes Python: 10, todos passando.
- **Não testado:** o caminho da mensagem e do diálogo de arquivo, que precisa de clique. Fica para o usuário conferir no Windows, abrindo o `boej.exe` numa pasta sem `boej.pak`.

## 2026-10-23 — Etapa 13: launcher e releases sem nada do jogo (formato do gen1recomp)

**O pedido.** Releases parecidas com as do [gen1recomp](https://github.com/bryanthaboi/gen1recomp):
- um pacote por plataforma, com o `sha256sums.txt`;
- nenhum dado do jogo no pacote;
- um launcher que pede a ROM uma vez, confere o SHA-1, prepara tudo e depois só abre o jogo;
- atualizações dentro do app.

A diferença: lá o motor é Lua escrito à mão, mas aqui o `boej.exe` *é* o código do jogo recompilado. As decisões do usuário:
1. o launcher recompila e compila **no PC do jogador**, e a release não leva nenhum código do jogo;
2. os dados ficam ao lado do executável;
3. a primeira versão tem configurações na interface, checagem de atualizações, release pelo CI e um espaço reservado para mods.

**Viabilidade, medida antes de escrever o launcher:**
- **Extração sem Python:** o `dsd rom extract` (v0.12.1, MIT, binário oficial para Windows) dá o ARM9, a ITCM/DTCM e as overlays já descomprimidas. A ov129 sai com as funções do DS Protect decifradas, a única overlay que precisa disso. Recompilando a partir dessa extração, o código gerado ficou **idêntico** ao atual. A única diferença nos módulos é o campo de tamanho comprimido nos module params, que o dsd zera e não é código.
- **Compilador do jogador:** llvm-mingw 20260922 (clang + libc++, Apache 2.0). O código gerado (918 mil linhas) compila sem nenhum aviso: 342 s de CPU, ou 27 s com 16 threads. O `boej.exe` resultante (28 MB, depende só do `SDL3.dll`) deu a mesma imagem da build GCC no Windows, com a mesma velocidade.

**Biblioteca `ntr-common`** (usada pelo jogo e pelo launcher), com o header `ntr/common.h`:
- `files.cpp`: o `map_file` e os auxiliares UTF-8 saíram do `rom.cpp`;
- `config.cpp`: ganhou `Config::set` e `config_save`, que regrava o texto padrão com os valores atuais e mantém comentários e ordem;
- `hash.cpp`: SHA-1 e SHA-256 novo;
- `setup.cpp`: `find_dump`, `sha1_file`, `dump_problem` e `pak_create`.

O `log_printf` é fornecido por cada programa. O SDL3 passou a ser procurado no CMake da raiz.

**Launcher** (`launcher/`, SDL3 + Dear ImGui v1.92.9b), com quatro abas:
- **Jogar:**
  - Sem `boej.pak`, pede o dump por um diálogo ou por arrastar para a janela. Depois faz, numa thread:
    1. `pak_create` (SHA-1 + pacote);
    2. `dsd rom extract`, guardando só `arm9/` e `arm9_overlays/` em `cache/modules/` (3,5 MB). O resto da extração é apagado;
    3. o llvm-mingw, baixado uma vez com o `curl.exe` do System32, conferido pelo SHA-256 do `sdk/toolchain.txt` e extraído com o `tar.exe` sem os alvos armv7/i686/aarch64 (525 MB instalado);
    4. o manifesto com `{modules}` trocado pelo caminho local, e o `ntr-recomp`;
    5. a compilação de 173 arquivos com todos os núcleos (código gerado primeiro);
    6. a ligação por arquivo de resposta (com `/`, porque o clang lê `\` como escape).
  - Barra de progresso, detalhes e "Cancelar". O `cache/build.stamp` guarda a versão do SDK e o compilador: depois de uma atualização o launcher pede "Preparar o jogo" de novo, sem o dump.
- **Configurações:** vídeo, áudio, a seção nova `[launcher]` (`check_updates`, `close_on_play`) e a tabela de controles. "Mudar" e "+" capturam a próxima tecla, ou o próximo botão ou eixo do controle; "x" desliga. Cada mudança grava o `boej.ini`.
- **Mods:** espaço reservado. Lista a pasta `mods/` e tem um botão para abri-la.
- **Atualizações:**
  - consulta `api.github.com/repos/matheus2x/ie3ogre-nds-recomp/releases/latest`;
  - baixa o `ie3ogre-recomp-<versão>-<plataforma>.zip` e o `sha256sums.txt`;
  - confere o SHA-256 e instala por cima, renomeando cada item antigo para `.old` (no Windows dá para renomear o exe em uso). Os `.old` são apagados na abertura seguinte.
  - Os dados do jogador não são tocados.
- **Processos sem console:** a função própria `run_process` (`CreateProcessW` com `CREATE_NO_WINDOW` no Windows, `posix_spawn` no Linux) captura a saída e devolve o código de saída. O `SDL_CreateProcess` em segundo plano perderia o código de saída.
- **Linha de comando:** `--prepare [dump]` e `--update` (o mesmo caminho da interface, sem janela). Para testes: `IE3_UPDATE_API`, `IE3_LAUNCHER_TAB` e `IE3_LAUNCHER_SHOT`.

**Release** (`tools/release.py`, `VERSION` = 0.1.0):
- O zip tem uma pasta `ie3ogre-recomp/` com:
  - o launcher, o `SDL3.dll`, `LEIA-ME.txt`, `TERCEIROS.txt` e `licenses/` (SDL3, ImGui e dsd, já que o MIT exige o texto da licença);
  - o `sdk/`: `ntr-recomp`, `dsd`, o manifesto com `{modules}`, as fontes do runtime e do `main.cpp`, os headers e a biblioteca de importação do SDL3, e o `toolchain.txt`.
- **8,4 MiB** para Windows. Não leva nenhum byte do jogo: o manifesto só tem endereços, tamanhos e nomes.
- O `analyze.py manifest --template` gera o manifesto sem o dump. O corpo é idêntico ao normal.
- O launcher e o `ntr-recomp` são compilados com o mesmo llvm-mingw (toolchain novo `cmake/toolchains/llvm-mingw-x86_64.cmake`, baixado e conferido em `build/tools/`). O `--zip` da etapa anterior saiu do `build.py`, porque levava o código do jogo.
- **CI** (`.github/workflows/release.yml`): numa tag `vX.Y.Z` que bate com o `VERSION`, roda os testes e o `release.py`, depois publica o zip e o `sha256sums.txt` com `gh release create`. Rodado à mão, só guarda o pacote como artefato.

**Bugs achados nos testes:**
- O `ntr-recomp` abria arquivos com `std::string` e falhava na pasta `IE3 Teste ção` no Windows. Agora usa caminhos UTF-8 e argumentos de `CommandLineToArgvW`.
- O toolchain do llvm-mingw não chegava aos projetos de teste do `try_compile`. Passou porque aqui a variável de ambiente estava definida, e falharia no CI; resolvido com `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES`.

**Verificação:**
- **Linux** (release Linux descompactada numa pasta vazia, `--prepare` com o dump retail):
  - dump → pacote → dsd → recompilação (4,5 s) → compilação (47 s) → `boej` em 56 s;
  - o roteiro da história com esse `boej` ficou **idêntico byte a byte** nos quadros 9000 e 14000;
  - um segundo `--prepare` não refaz nada.
- **Windows** (release Windows, via interop do WSL, numa pasta com acento e um dump chamado `meu dump ção.nds`):
  - baixou o llvm-mingw real (182 MB), conferiu, instalou, recompilou (7 s) e compilou (35 s);
  - o roteiro da história com o `boej.exe` feito no Windows também ficou **idêntico byte a byte**.
- **Atualização** (release falsa 0.2.0 por `file://`):
  - baixou, conferiu, instalou e criou os `.old`, que sumiram na abertura seguinte;
  - em seguida o `--prepare` recompilou sem o dump;
  - com o `sha256sums.txt` adulterado, recusou com "Nada foi alterado", e o launcher e a versão continuaram os mesmos.
- **Interface:** capturas das abas no Linux (fonte padrão do ImGui) e no Windows (Segoe UI, escala 150%) mostram os acentos e o layout certos.
- **CI simulado:** numa cópia limpa só com os arquivos versionados, os testes passaram e o `release.py` baixou as ferramentas e gerou um pacote com os mesmos 134 arquivos.
- A build de desenvolvimento, a build GCC de Windows e o roteiro da história continuam iguais.
- **Pendências:**
  - o repositório é privado, então a API responde 404 e a aba mostra "não foi possível consultar". Isso funciona quando houver releases públicas;
  - o diálogo de arquivo e a captura de controle precisam de teste com clique e com um controle de verdade;
  - o workflow do CI ainda não rodou no GitHub.

**Notas de release automáticas** (`tools/changelog.py`):
- O workflow usava o `--generate-notes` do GitHub, que lista *pull requests*. Como os commits vão direto para a `main`, a release sairia só com o link de comparação.
- Agora o texto vem das mensagens de commit entre a tag `vX.Y.Z` anterior (`git describe --tags --match 'v[0-9]*'`) e a nova:
  - cada commit vira uma seção, com o título e o corpo já em tópicos, sem as linhas `Co-Authored-By`;
  - na primeira release, sem tag anterior, só os títulos.
- No fim vêm "Como instalar", a conferência do SHA-256 e o link de comparação.
- O workflow faz checkout com `fetch-depth: 0` (histórico e tags) e publica com `--notes-file`. Rodado à mão, mostra a prévia no resumo da execução.
- Testado com duas tags temporárias (já apagadas): achou a anterior, listou os dois commits do intervalo e gerou o link `compare/v0.0.1...v0.0.2`.

## 2026-10-23 — Menu de pausa no jogo, com as configurações do launcher

**O pedido:** abrir as configurações com Esc dentro do jogo, sem precisar reiniciar (ou com um botão de reiniciar no menu).

**Solução: menu de pausa no próprio jogo, em vez de abrir o launcher.** Quase tudo do `boej.ini` pode valer na hora:
- volume: ganho do `SDL_AudioStream`;
- filtro e ampliação inteira: modo da textura e apresentação lógica;
- tela cheia e tamanho da janela;
- resolução do 3D: o `gpu3d_set_scale` já trocava entre quadros, por causa do avanço rápido;
- controles: as associações são relidas.

Então o menu não precisa de reinício para nada. Mesmo assim, tem "Reiniciar o jogo".

**Biblioteca `ntr-ui`** (`runtime/ui/ui.cpp`, header `ntr/ui.h` sem tipos do ImGui):
- O painel de configurações saiu do `launcher/settings.cpp` (que virou uma casca de 20 linhas). O launcher e o jogo usam o mesmo código: seções, tabela de controles, captura de tecla/botão/eixo e "Restaurar o padrão".
- Também ficaram nela a inicialização do ImGui (fonte do sistema e escala) e o `pause_menu`.
- Dentro do jogo o painel esconde as opções do launcher e chama `on_change` depois de cada gravação.
- O ImGui passou a ser buscado no CMake do runtime (`ntr-ui` é ligada ao `ntr-runtime`).
- No controle, os nomes de posição aparecem curtos (`east`), com a explicação ("botão da direita: B no Xbox, A no Nintendo") num tooltip: a janela do jogo é estreita.

**No jogo** (`host.cpp`):
- Nova ação `menu`: padrão Esc no teclado e "guide" no controle, configurável como os outros atalhos.
- Ao apertar, o `host_present` entra no laço do menu e não volta até o jogador continuar. Nada do jogo avança, nem o relógio, e o áudio fica pausado (`SDL_PauseAudioStreamDevice`).
- Atrás do menu aparece o último quadro, escurecido, com a apresentação lógica desligada enquanto o menu está aberto.
- O `diag_frame` continua sendo chamado, para o watchdog não registrar um travamento.
- Ao voltar:
  - a apresentação é restaurada e o áudio retomado;
  - as teclas são recalculadas a partir do que está apertado, para nenhuma ficar presa;
  - o ritmo recomeça do instante atual.
- `apply_settings` aplica tudo depois de cada mudança. Tela cheia e tamanho da janela só mudam quando a própria opção muda, para não desfazer o F11 nem um redimensionamento manual. `NTR_SCALE` e `NTR_3D_SCALE` continuam com prioridade.
- **Reiniciar:** fecha a gravação da sessão e o log (`log_close`, novo), abre um processo novo com a mesma linha de comando (`host_set_command_line`, chamado pelo `main`; `SDL_CreateProcessWithProperties` em segundo plano) e sai. Assim o jogo novo guarda a sessão anterior em `boej.old.log` / `boej-replay.old.*`.
- "Reiniciar" e "Sair" pedem confirmação ("o progresso desde o último save será perdido").

**SDK da release:** passou a levar `sdk/ui/` e `sdk/imgui/` (o núcleo do ImGui mais os backends SDL3 e SDL_Renderer, já na lista de licenças). O launcher compila esses arquivos com o jogo: 180 arquivos em vez de 173. O zip do Windows foi de 8,4 para 9,2 MiB.

**Verificação** (ganchos temporários, já removidos, que apertavam Esc no quadro 300 e agiam dentro do menu):
- **Linux:**
  - o jogo pausou no quadro 300 e o menu apareceu sobre a tela escurecida (captura conferida);
  - mudar a resolução do 3D pelo caminho da interface gravou o `boej.ini` de teste;
  - a captura do quadro 410 saiu em 256×384, contra 512×768 antes, ou seja, mudou sem reiniciar;
  - Esc fechou o menu e o jogo seguiu.
- **Reiniciar, no Linux e no Windows:** a sessão antiga terminou com "paused at frame 300" / "restarting the game" no `.old.log`, e o jogo novo começou do quadro 0 e foi até o 600.
- **Release** (Linux, e Windows numa pasta com acento, com download novo do compilador): o launcher compilou os 180 arquivos (58 s / 47 s), e o roteiro da história com o `boej.exe` feito no Windows ficou idêntico byte a byte.
- O roteiro da história na build de desenvolvimento continua idêntico, e os testes Python passam.
- **Não testado:** o menu com cliques de verdade e com controle físico (navegação do ImGui pelo controle).
