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

## 2026-10-23 — Tela larga 16:9 (padrão)

Pedido: por padrão, uma janela 16:9 dimensionada pela altura do monitor, com as duas telas do DS no terço da esquerda e uma cópia ampliada da tela de baixo no resto (como o layout "híbrido" do melonDS). O modo antigo (telas empilhadas e centralizadas, com bordas pretas) continua disponível nas configurações.

- **Opção nova** `video.wide_layout` (padrão `true`). Como os padrões embutidos são lidos antes do `boej.ini`, quem já tem o arquivo também passa a ver a tela larga e pode desligar no menu ou no launcher ("Tela larga (16:9, tela de baixo ampliada)").
- **Geometria** (`host.cpp`): tamanho lógico 2048×1152 (16:9). As telas empilhadas ficam em 3× (768×1152, 37,5% da largura) e a tela de baixo em 5× (1280×960) à direita, centralizada na vertical, com faixas pretas de 96 px em cima e embaixo. As duas escalas são inteiras e a proporção 4:3 é mantida. O print do melonDS esticava a imagem para não ter faixas; aqui preferi não distorcer.
- **Janela:** 85% da altura útil do monitor (`SDL_GetDisplayUsableBounds`), com largura = altura × 16/9, limitada a 95% da largura útil. Num monitor de 1440p a janela ficou com 2176×1224. Nesse modo o `window_scale` e o `integer_scale` não se aplicam: a escala inteira de 2048×1152 não caberia na maioria das janelas. Os dois controles ficam desabilitados no painel. Ligar ou desligar a tela larga no menu redimensiona e centraliza a janela (fora da tela cheia).
- **Desenho:** a função `draw_screens` é usada no quadro normal (com apresentação lógica) e no quadro escurecido atrás do menu de pausa (sem apresentação lógica, com o encaixe calculado à mão).
- **Toque:** o `to_touch` aceita as duas cópias da tela de baixo.
- As capturas (F12, `NTR_SCREENSHOT_FRAMES`) continuam com as telas empilhadas em 256×384×escala, para não mudar os testes.

**Verificação:** com um gancho temporário (já removido), gravei o que a janela mostrava no quadro 650 nos dois modos. Na tela larga saiu 2176×1224 com o layout esperado; no modo empilhado, 512×768 como antes.

**Não testado:** clique do mouse nas duas cópias da tela de baixo, alternar o modo pelo menu de pausa e o build do Windows.

**Ajuste pedido em seguida:** esticar a imagem como o melonDS, sem faixas pretas. O tamanho lógico passou a 2304×1296. As telas empilhadas ocupam exatamente o terço da esquerda (768×1296) e a tela de baixo ocupa os dois terços da direita (1536×1296). A proporção não é mais a do DS: os pixels ficam 12,5% mais altos. O toque continua mapeado pelas mesmas duas áreas.

## 2026-10-23 — Configurações mais limpas (launcher e menu de pausa)

Pedido: deixar as configurações menos poluídas e mais fáceis de alterar, tanto no launcher quanto no menu do Esc. As duas telas usam o mesmo painel (`runtime/ui/ui.cpp`), então a mudança vale para as duas.

- **Uma seção por vez:** no lugar dos cabeçalhos recolhíveis empilhados, há uma lista lateral (Vídeo, Áudio, Controles, Atalhos e, só no launcher, Launcher), e a seção escolhida aparece ao lado. A seção escolhida continua a mesma entre aberturas do menu.
- **Linhas rótulo → controle:** cada opção tem o nome à esquerda (com uma dica curta em cinza embaixo, quando ajuda) e o controle à direita, ocupando a coluna. Numa janela estreita (menos de 22 "em", como no modo empilhado de 512 px), o controle vai para baixo do rótulo.
- **Vídeo:** a caixa "Tela larga" virou um seletor "Layout" (Tela larga 16:9 / Telas empilhadas). Tamanho da janela e "Só múltiplos inteiros" só aparecem no modo empilhado, porque não valem na tela larga (antes ficavam desabilitados na tela).
- **Controles e atalhos:** cada ação tem um único botão que mostra o que está atribuído ("Right Shift / Backspace"). Antes eram três botõezinhos (Mudar, +, x) por célula. Um seletor Teclado | Controle no topo mostra uma coluna por vez. Ao clicar, a janela de captura mostra a atribuição atual, a opção "Manter as atuais e adicionar esta" (o antigo "+") e os botões "Remover todas" (o antigo "x") e "Cancelar (Esc)".
- **Menu de pausa:** sem barra de título. A lista lateral começa com "Pausado" e um botão "Continuar" em destaque, e termina com "Reiniciar" e "Sair do jogo" presos no fim (ambos ainda pedem confirmação). O tamanho é limitado a 52×36 "em", e a fonte cresce com a janela (`style.FontScaleMain`, entre 1× e 3×) para o menu ocupar uns 3/4 da altura. Antes, na janela 16:9 grande, o texto ficava minúsculo.
- **Estilo:** espaçamentos maiores, cantos arredondados, cores neutras e um azul de destaque (botões principais, item selecionado, marcações). Isso vale também para as outras abas do launcher.
- O rodapé tem "Restaurar o padrão" e o aviso "Tudo é salvo na hora." (ou "Salvo no boej.ini." depois de uma mudança), que some quando não cabe.
- `SettingsOptions` ganhou `nav_top` / `nav_bottom` / `nav_bottom_height`, por onde o menu de pausa põe as suas ações na lista lateral. O `host.cpp` agora preenche as opções campo a campo, para não gerar `-Wmissing-field-initializers`.

**Verificação** (capturas com `IE3_LAUNCHER_SHOT` e com ganchos temporários no jogo, já removidos, que abriam o menu no quadro 300 e a janela de captura de tecla):
- Launcher em 820×640: as seções Vídeo, Controles e Atalhos estão legíveis, sem cortes.
- Menu de pausa na tela larga (2176×1224) e no modo empilhado (512×768): a fonte foi escalada na tela larga, e no modo empilhado as linhas ficam uma embaixo da outra.
- A janela de captura aparece sobre a seção Controles.
- O build não gera avisos, e os testes Python passam.

**Não testado:** cliques e navegação por controle de verdade (trocar uma tecla de fato, a caixa "adicionar", "Remover todas") e o build do Windows.

**Pasta de teste do Windows** (`C:\Games\ie3-teste\ie3ogre-recomp`): atualizada com a release 0.1.2 gerada a partir deste código. Troquei o `sdk/`, o launcher e o `SDL3.dll` e mantive o `boej.ini`, o `boej.pak`, o `cache/` e o `toolchain/`. O `ie3-launcher.exe --prepare` recompilou o jogo no Windows (180 arquivos em 37 s; `build.stamp` 0.1.2).

## 2026-10-23 — Resolução do 3D: 4× sem travar e opções até 10×

Pedido: a 4× o FPS caía abaixo de 60 e o som engasgava. O usuário também pediu resoluções maiores, até 10× como no melonDS. Decisão (perguntada ao usuário): continuar com o renderizador em software, otimizar e abrir 1× a 10×. O melonDS só passa de 1× com o renderizador OpenGL.

**Medição antes** (roteiro da história, 14000 quadros, sem janela, Release): 3,0 ms/quadro a 1× e 10,2 ms a 4×. Com o `perf` a 4×, a thread principal ficava 100% ocupada e as threads de rasterização só ~10%. A recomposição 2D em alta resolução (`compose` 25%, `output_hi` 16%, `render_engine_line` 6%) rodava inteira na thread principal, uns 8 dos 10 ms. O som engasga porque cada quadro acima de 16,7 ms esvazia a fila.

**Mudanças:**
- **Imagem grande do motor 2D no VBlank, em paralelo** (gpu2d.cpp):
  - Cada linha do motor A com 3D guarda as duas camadas 2D mais altas de cada pixel nativo, sem o BG0 (`HiLine`/`HiPixel`), com uma chave de ordem (prioridade×6, OBJ antes dos BGs). Guarda também os registradores de efeito, o brilho mestre e o BG0HOFS da linha.
  - No VBlank, antes de o próximo quadro 3D ser trocado, cada pixel grande só encaixa o 3D entre essas duas camadas e aplica os mesmos efeitos. As 192 linhas são compostas no pool de threads.
  - O `compose` nativo foi dividido em `top_layers` + `finish_pixel`, usados pelos dois caminhos.
- **Pool compartilhado:** o `BandPool` saiu do gpu3d.cpp para `pool.h`/`pool.cpp`. O limite subiu de 8 para 16 threads (`NTR_3D_THREADS` continua valendo).
- **Uma escala por tela** (`host_present(top, top_scale, bottom, bottom_scale)`): uma tela sem 3D no quadro vai no tamanho nativo e a GPU amplia. Antes, o motor B era repetido N×N e enviado inteiro todo quadro. São duas texturas, e o `draw_screens` desenha cada uma no seu retângulo. As capturas (F12, `NTR_SCREENSHOT_FRAMES`) continuam no tamanho da resolução do 3D; a tela menor é ampliada repetindo pixels.
- **3D** (gpu3d.cpp):
  - Cada banda limpa as linhas que desenha (antes a limpeza era feita na thread principal). A partir de 4× a limpeza usa escrita não temporal (`_mm_stream_si128`).
  - Na troca de quadro, `std::swap` dos buffers em vez de copiar.
  - Cache de texturas a partir de 2×: cada (tex, pal) usado no quadro é decodificado uma vez, em paralelo, e o `sample` só faz o wrap e lê do vetor. O `sample` antigo virou `fetch` + wrap, e o cache usa o próprio `fetch`.
  - Os atributos agora ficam em planos: profundidade (`uint32_t`), flags (`uint16_t`: ID, borda, AA, névoa, translúcido, stencil) e o plano do AA (cor e alfa de baixo, cobertura). O plano do AA só é lido onde a flag de AA está ligada, então nunca é limpo. O pós-processamento varre só as flags.
  - A tabela de toon é lida uma vez por quadro (antes, duas chamadas a `memory()` por pixel).
- **Até 10×:** `kMax3dScale = 10`. O `resolution_3d` vai de 1 a 10. No painel (menu e launcher) a lista mostra o tamanho ("4× (1024×768)") e avisa que acima de 4× é preciso um processador com vários núcleos.
- **Folga no som:** quando a fila do SDL tem menos de um quadro de som (secou), entram 2 quadros de silêncio antes do próximo envio. Assim um quadro atrasado isolado não vira um engasgo atrás do outro. A latência sobe cerca de 33 ms só depois de um atraso.
- **Diagnóstico:** sem janela, ao sair, o log traz `[perf] N frames: X ms on average, worst Y ms (frame F), Z longer than 16.7 ms`.

**Resultado** (mesmo roteiro, sem janela; esta máquina tem 16 threads lógicas):

| escala | antes (média) | depois (média) | pior quadro | quadros > 16,7 ms |
|---|---|---|---|---|
| 1× | 3,0 ms | 3,1 ms | 15,9 ms | 0 |
| 4× | 10,2 ms | 4,4 ms | 19,3 ms | 2 |
| 4× (8 threads) | — | 4,7 ms | 19,9 ms | 3 |
| 6× (8 threads) | — | 6,2 ms | 31 ms | 1713 |
| 8× (8 threads) | — | 8,5 ms | 45 ms | 2614 |
| 10× | — | 10,0 ms | 63 ms | 2617 |
| 10× (8 threads) | — | 11,7 ms | 90 ms | 2615 |

- A 4× ficou folgado. De 6× para cima, o campo (quadros ~10500–14000) passa de 16,7 ms.
- Nas escalas altas o custo é o trabalho por pixel do `draw_polygon` + `sample`, espalhado entre interpolação, divisões e sombreamento (perfil por linha do fonte, compilando o gpu3d.cpp com `-g`). Os quadros pesados têm poucos polígonos (100–370), mas grandes e sobrepostos.
- A 10× são ~50 ms de CPU por quadro. Ir além disso pede um rasterizador SIMD ou um renderizador na GPU.
- A separação em planos e a escrita não temporal economizaram só ~3% de CPU a 10×: as amostras do `perf` nas limpezas exageravam o peso delas. O cache de texturas economiza ~5% de CPU a 2× e a 10×.

**Verificação:**
- 25 capturas **idênticas byte a byte** às do binário anterior:
  - roteiro da história nos quadros 3000, 6000, 9000, 12000 e 14000, a 1×, 2× e 4×;
  - sessão do bug3 (`build/debug/bug3`) nos quadros 16691, 20252, 25000, 30000 e 34869, a 1× e 4×.
- Testes Python: 10, todos passando. O `boej.exe` (MinGW) compila sem avisos.
- Com janela (WSLg, tela larga, 4×, roteiro até o quadro 12500, nada mais rodando): 16 segundos com algum quadro acima de 16,7 ms, 1 ou 2 por segundo, e o pior com 17,8 ms. Um teste anterior, rodando junto com builds e outras execuções, teve 30 por segundo no campo: era concorrência pela CPU.
- Desenho das duas texturas conferido com um gancho temporário (`SDL_RenderReadPixels` no quadro 11861, já removido). Na tela larga, a tela de cima vai nativa e ampliada, e a de baixo (3D) vai a 4× nos dois lugares. No modo empilhado, sai 512×768 certo.

**Não testado:** no Windows de verdade (desempenho e o envio das texturas pelo D3D), o som com engasgo de verdade (os testes rodaram mudos) e as escalas 5×–10× com janela.

**Pasta de teste do Windows** (`C:\Games\ie3-teste\ie3ogre-recomp`): atualizada com uma release 0.1.3 gerada deste código (ainda não é a 0.1.3 publicada).
- Troquei o `sdk/`, `licenses/`, o launcher, o `SDL3.dll` e os textos. Mantive `boej.ini`, `saves/`, `boej.pak`, `cache/` e `toolchain/`. Apaguei o `build.stamp` para forçar a recompilação.
- A primeira tentativa do `--prepare` falhou: o libc++ do llvm-mingw instancia o destrutor de `std::vector<HiLine>` na definição de `g_engines`, onde o `HiLine` ainda era só declarado (o GCC aceitava). As structs `Effects`/`HiPixel`/`HiLine` foram para antes do `Engine`.
- Na segunda tentativa, `boej.exe` recompilado em 43 s, sem avisos.
- Teste do `boej.exe` sem janela, pelo interop (roteiro da história até o quadro 12000, a 4×): 4,83 ms de média, pior quadro de 18,4 ms, 3 acima de 16,7 ms.

## 2026-10-23 — Velocidade do avanço rápido configurável

- Antes o avanço rápido não tinha limite: o `pace()` não esperava, e a janela recebia 1 a cada 4 quadros. Pelas medições sem janela (3,1 ms por quadro a 1×), isso dava ~5× em média nesta máquina, variando com a cena.
- Opção nova `[game] fast_forward_speed` no `boej.ini`: 2 a 10 vezes a velocidade normal, padrão 4. Sem opção ilimitada, a pedido do usuário. Um valor fora da faixa (o 0 de um ini antigo, por exemplo) gera um aviso no log e volta ao padrão.
- `pace()`: com o avanço rápido, o prazo de cada quadro passa a ser `kFrameNs / velocidade`. Se o processador não aguentar, vai no máximo que conseguir.
- A janela recebe 1 a cada N quadros (~60 imagens por segundo).
- Launcher e menu de pausa: combo "Velocidade ao acelerar" no topo da página Atalhos (2× a 10×). Vale na hora, pelo `apply_settings()`.
- README atualizado. `build/cmake-rel` compila. **Não testado com janela.**

## 2026-10-23 — Launcher volta quando o jogo fecha

- Pedido: ao fechar o jogo, o launcher reabre; opção para não reabrir, desmarcada por padrão.
- `start_process()` ganhou um `on_exit` opcional: uma thread espera o processo (`WaitForSingleObject` / `waitpid`) e o chama no fim. O launcher usa isso para empurrar um evento SDL próprio (`SDL_RegisterEvents`).
- Com `close_on_play` (padrão), o "Jogar" agora esconde a janela em vez de sair; escondido, o laço fica em `SDL_WaitEvent` sem desenhar. Quando o jogo fecha, a janela volta (`SDL_ShowWindow` + `SDL_RaiseWindow`) e o `boej.ini` é relido, porque o menu de pausa pode ter mudado as configurações.
- Opção nova `[launcher] stay_closed` (padrão `false`): com ela, o launcher fecha de vez como antes. Na página Launcher, "Não reabrir ao fechar o jogo" só aparece com "Fechar o launcher ao abrir o jogo" ligado.
- README atualizado. Launcher compila em `build/cmake-rel` e `build/cmake-win`. **Não testado com janela.**

## 2026-10-23 — Savestates (5 slots)

- Pedido: savestate com recarga, até 5 slots, cada um num arquivo em `savestates/` junto do executável.
- **O problema:** o estado do DS (RAM, VRAM, registradores) não basta. O código recompilado guarda a cadeia de chamadas do jogo nas pilhas do host, uma por fiber (threads do NitroSDK e tasks do motor). Recriar essas pilhas a partir do estado do guest não dá (o interpretador só faz ARM, e os quadros do host guardam valores próprios).
- **A solução:** copiar as pilhas das fibers e devolvê-las nos mesmos endereços:
  - `fiber.cpp` reescrito: troca de contexto própria em assembly (Win64 e System V; registradores salvos na própria pilha) no lugar de `CreateFiber`/`ucontext`. As pilhas são slots de 1 MiB (com 64 KiB de guarda) numa região reservada em endereço fixo (`0x200000000000`). No Windows o `StackBase`/`StackLimit` do TIB acompanha a fiber, como no `SwitchToFiber`.
  - O `boej` é ligado sem ASLR (`--disable-dynamicbase` no Windows, `-no-pie` no Linux; no CMake e no launcher) e sem stack protector (o canário muda a cada execução).
  - A thread principal do jogo passou a rodar numa fiber da região; a pilha da thread do SO fica só com o `savestate_run`, que faz o trabalho de salvar/carregar.
  - O pedido (atalho ou menu) vale no próximo `poll()`: a fiber que está rodando passa o controle à fiber principal (`savestate_poll`), com todas as fibers do jogo suspensas. Entre o `poll` e a troca não há ponteiros para o heap na pilha.
- **O que vai no arquivo** (`savestate.cpp`, cada módulo com seu `*_state(StateIo&)`): pilhas das fibers, toda a memória do DS, CPU (bancos, CP15, overlays ativos), tempo e timers, IRQ, contextos das threads e tasks, FIFO do cartão, PXI, 2D (linhas já desenhadas no quadro), 3D (geometria, lista de polígonos, quadro na tela e o renderizado esperando o VBlank), som (canais, captura, alarmes, amostras do quadro), cartão e início do relógio. Cabeçalho com uma identidade da build (data da compilação, endereços de funções, região das pilhas) e um hash do conteúdo. ~6 MB por slot.
- O save do próprio jogo (`saves/boej.sav`) fica de fora: carregar um savestate nunca volta o save do jogador.
- Carregar guarda uma cópia do estado atual antes; se o arquivo falhar no meio, ela volta. Arquivo de outra build: "de outra versão do jogo" (depois de uma atualização os slots antigos não abrem).
- `OS_InitContext` agora libera a fiber da thread antiga (antes vazava; com 128 slots fixos, precisava).
- **Interface:** F5 salva no slot escolhido, F9 carrega, F6/F7 trocam o slot (1 a 5), configuráveis em Atalhos. Mensagem no canto da tela por 2,5 s. Menu de pausa: página "Savestates" com a data de cada slot, Salvar (pede confirmação para substituir) e Carregar. Depois de carregar, a fila de som é limpa, o ritmo recomeça e a gravação de sessão para (não daria mais para reproduzi-la do boot).
- Testes: `NTR_STATE_SAVE=N` / `NTR_STATE_LOAD=N` (slot 1).
- **Testado** (sem janela, roteiro da história `bug3`): salvar no quadro N de um processo, carregar no quadro 10 de outro e seguir 600 quadros com a mesma entrada dá **a mesma imagem byte a byte** (e o mesmo áudio no teste do quadro 34000):
  - Linux: N = 3000, 9000, 15017, 21000, 27500, 30001, 34000 e 36000 (3D a 2× e 3×); carregar, salvar de novo e carregar esse segundo estado também; estado de 2× carregado a 1× e a 4× roda normal.
  - Windows (`boej.exe` do llvm-mingw, pelo interop): N = 3000 a 3× e 27500 a 4×.
  - Salvar não muda a execução (a imagem do quadro 900 é igual à de uma execução sem savestate).
- Com janela (Linux, WSLg): salvar e carregar por `NTR_STATE_*` funciona; a mensagem "Slot 1 salvo" e a página Savestates do menu aparecem certas (conferidas lendo a imagem do renderer, com um ajuste temporário). **Não testado:** apertar os atalhos e os botões de verdade, e no Windows com janela.

## 2026-10-23 — Lado das telas pequenas na tela larga

- Pedido: na tela larga (16:9), escolher se as duas telas pequenas ficam à esquerda (padrão, como antes) ou à direita.
- Opção nova `[video] wide_side` (`left` ou `right`, padrão `left`). No `host.cpp`, os retângulos fixos `kWideStack`/`kWideBottom` viraram `wide_stack()`/`wide_bottom()`, que dependem do lado; desenho e toque do mouse (nas duas cópias da tela de baixo) usam essas funções. A mensagem dos savestates vai para o canto de cima do lado das telas pequenas.
- Launcher e menu de pausa: combo "Telas pequenas" (À esquerda / À direita) logo abaixo de "Layout", só no modo tela larga. Vale na hora, pelo `apply_presentation()`.
- README atualizado. `build/cmake-rel` compila sem avisos.
- **Testado** com janela (WSLg, ajuste temporário lendo a imagem do renderer no quadro 600): com `right`, a tela de baixo ampliada fica à esquerda e as duas pequenas à direita; com `left`, igual a antes. Um clique em (50, 50) da janela vira toque com `right` e não com `left` (cai na tela de cima). **Não testado:** trocar pelo menu de pausa com o jogo rodando e o build do Windows.

## 2026-10-23 — Mods: troca de arquivos e dados das overlays, aba Mods e tools/mod.py

Pedido: a próxima etapa para facilitar a criação de mods. Os mods planejados pelo usuário são tradução, troca de sprites, upscaling e um gerenciador de técnicas (estilo Mini Hack). Esta etapa monta a base comum a todos: o jogo ler arquivos de mods sem mexer no `boej.pak`, o launcher gerenciar os mods e ferramentas para quem os cria. Formato e fluxo em [`docs/MODS.md`](MODS.md).

**Análise.**
- Todo acesso do jogo ao cartucho passa por `rom_read` (o `CARDi_ReadRomWithCPU` é HLE). Então dá para trocar arquivos do lado do host, sem tocar no código recompilado.
- A tradução de fãs (BAHAMUT) muda 512 arquivos e 33 overlays. Com os símbolos do ds-decomp, **nenhuma** diferença das overlays cai dentro de uma função, exceto na ov129 (DS Protect, cifrada na ROM). São só strings e tabelas.
- O tradutor embute em C as constantes dos literal pools (`ldr rX, [pc, #imm]` vira `c.r[X] = 0x...`). Por isso um mod que mude bytes de código ou de pools não teria efeito (ou teria meio efeito). É preciso conferir isso.
- A tradução encolhe o `ram_size` de 25 overlays na tabela de overlays e aumenta em 1 byte o da ov065. As overlays retail descomprimidas têm exatamente o `ram_size`.

**Runtime.**
- `ntr/nitro.h` + `nitro.cpp` (no `ntr-common`, compartilhado com o launcher): o `RomImage` (antes dentro do `rom.cpp`; .nds ou pacote, mapeado em memória), a FNT/FAT (`nitro_files`), a tabela de overlays (`nitro_overlays`, `nitro_overlay_image`) e o BLZ em C++.
- `rom.cpp`: camada de substituições (`rom_override`) por cima da imagem, aplicada em cada `rom_read`.
- `mods.cpp`:
  - lê `[mods] enabled` do `boej.ini` (ou `NTR_MODS`) e, de cada pasta, o `mod.ini`, `files/<caminho do NitroFS>` e `overlays/ovNNN.bin`;
  - os arquivos do mod ficam mapeados e são colocados em endereços da ROM depois do fim da imagem. Cópias da FAT e da tabela de overlays apontam para eles (overlay marcada como não comprimida e com `ram_size` = máximo entre o original e o do mod). O tamanho dos arquivos pode mudar;
  - uma overlay menor é completada com os bytes originais. A ov129 é recusada;
  - mod com erro fica de fora inteiro; caminho desconhecido vira aviso; quando dois mods trocam o mesmo arquivo, vale o que vem depois (com aviso). Tudo vai para o `boej.log` (`[mods]`).
- `boej --check-mods`: a mesma checagem para todas as pastas de `mods/`, em linhas separadas por tab, para o launcher. Não grava `boej.log` (`log_disable_file`).
- Savestates: a identidade inclui `mods_fingerprint()` (pastas, caminhos, tamanhos e datas dos arquivos, conteúdo das overlays). A mensagem virou "De outra versão do jogo ou com outros mods".
- `boej.ini`: seção nova `[mods]` com `enabled =`.

**Recompilador.** O `Module` marca cada byte lido durante a tradução (`baked`: instruções, literais e tabelas). O `registry.cpp` ganhou, por módulo, `kCode_<módulo>`: as faixas [início, fim) da união dos corpos das funções com esses bytes (`ModuleEntry::code`). O jogo compara a overlay do mod com a original nessas faixas e recusa o mod se algum byte mudou, informando o endereço. Na tradução, todas as 32 overlays (sem a 129) passam.

**Launcher (aba Mods, `launcher/mods.cpp`).**
- Lista as pastas de `mods/` com nome, versão, autor e descrição do `mod.ini`. Cada mod tem caixa para ligar, setas para a ordem e o estado (ok / avisos / com erro), que vem do `boej --check-mods` rodado em segundo plano.
- Detalhes do mod selecionado: erros, avisos, quantos arquivos e overlays ele troca, abrir a pasta e remover (com confirmação).
- **Instalar mod (.zip)** (botão ou arrastando o `.zip` para a janela), com `tar` no Windows e `unzip` no Linux, como na atualização. O `mod.ini` pode estar na raiz do zip ou numa pasta única; reinstalar atualiza a pasta.
- **Criar de um dump modificado:** compara o `.nds` com o `boej.pak` (arquivos pela FNT, overlays descomprimidas com o BLZ em C++) e cria a pasta do mod. Recusa se o ARM9 for diferente. Nome da pasta sem o número da lista de releases ("5421 - ") e sem cortar caracteres UTF-8.
- Linha de comando: `--install-mod <zip>` e `--mod-from-dump <nds>`. Teste: `IE3_LAUNCHER_MOD=<pasta>` seleciona um mod.
- `settings_set` grava o `boej.ini` (a lista `enabled`).

**`tools/mod.py`:** `new`, `unpack` (arquivos originais e overlays, do `boej.pak` ou do `.nds`), `from-dump`, `check` (com os tamanhos de função do ds-decomp) e `pack` (zip). Testes em `tools/tests/test_mod.py` (ROM sintética).

**Correção de build:** desde os savestates, o `boej.exe` não ligava com o MinGW (GCC) do `tools/build.py --windows`: o `ld` do GNU não aceita `--large-address-aware` em imagens de 64 bits, que já têm a flag. Agora a opção só vai para o Clang (llvm-mingw, que é o usado no PC do jogador).

**Verificação:**
- `tools/mod.py from-dump boej.pak <dump BAHAMUT>` gerou o mod com 512 arquivos e 32 overlays. O launcher (`--mod-from-dump`) gerou **os mesmos arquivos byte a byte** (só o `mod.ini` difere), o que confere o BLZ e a leitura da FNT em C++.
- Roteiro da história (sem janela), quadros 1500, 3000, 6000, 9000 e 14000: retail + mod de tradução deu imagens **idênticas byte a byte** às do dump traduzido rodado direto. Em relação ao retail sem mod, 4 dos 5 quadros diferem (o texto). No Windows (`boej.exe` do MinGW, pelo interop), os quadros 3000 e 9000 também ficaram idênticos.
- Mod de teste com um byte de código mudado na ov022 e um arquivo inexistente: erro "muda o código do jogo em 0x02119F10" e aviso do caminho, no jogo e na aba Mods (captura da janela conferida).
- Instalação de `.zip` no Linux e no Windows (pasta com acento no nome). Testes Python: 14, todos passando. Linux e Windows compilam sem avisos.
- Observação: na tradução BAHAMUT, nesse ponto da história, o texto das cenas aparece com "?" no lugar dos kanji (a fonte foi trocada). É igual no dump traduzido, então é da tradução e não do mod.

**Não testado:** cliques de verdade na aba Mods (ligar, setas, remover, diálogos de arquivo), o launcher do Windows com janela e o build pelo llvm-mingw no PC do jogador.

**Próximos passos** (em `docs/MODS.md`): patches por arquivo (mods sem dados do jogo), formatos documentados com conversores (texto, fonte, imagem, tabelas), pacotes de texturas por hash para o upscaling e um editor de técnicas que gere um mod.

## 2026-10-24 — README enxuto

- Pedido: README mais curto, só com o essencial, e considerando só o dump retail (a tradução de fãs sai da tabela; depois o código vai aceitar só o retail como base).
- README (297 → 111 linhas): o que é, estado, como jogar, dump suportado (só o retail), controles e atalhos, mods, como relatar problemas, funcionalidades resumidas, build mínimo, referências, contribuição e aviso legal.
- O que saiu foi para `docs/DESENVOLVIMENTO.md`: estrutura do repositório, análise com dsd/Ghidra, extração, configurações do `boej.ini`, arquivos de diagnóstico, build para Windows, release/CI e variáveis de ambiente. Acrescentei os comandos de mods do launcher e do jogo e o `NTR_MODS`, e corrigi valores que estavam velhos (`resolution_3d` e `NTR_3D_SCALE` vão até 10; o 3D usa até 16 threads).
- `docs/MODS.md` não cita mais a tabela de dumps do README. `tools/build.py` aponta para o novo documento.
- Ainda não mudou: o `config/boej/dumps.json`, o `setup.cpp` e o `compare_dumps.py` continuam conhecendo a tradução de fãs.

## 2026-10-24 — Mods em pasta ou .zip, direto na pasta mods/

- Pedido: cada mod é uma pasta **ou um .zip** dentro de `mods/`, ao lado do `.exe`; é ali que o jogador coloca os mods e de onde o launcher os lê.
- Antes, só pastas eram lidas, e o botão "Instalar" extraía o `.zip` com `tar`/`unzip`.
- **`ModPackage`** (`ntr/package.h` + `package.cpp`, no `ntr-common`):
  - trata pasta e zip do mesmo jeito: lista os arquivos a partir da raiz do mod;
  - num zip, a raiz é o topo ou a única pasta de cima (o que o `tools/mod.py pack` e o "Compactar" do Windows geram);
  - leitor de zip próprio, sem dependência nova: diretório central, entradas guardadas ou Deflate, CRC-32. Zip com senha e zip64 são recusados com mensagem;
  - inflate próprio no estilo do "puff" do zlib.
- **Jogo** (`mods.cpp`):
  - os nomes em `[mods] enabled` são o da pasta ou o do arquivo (`Tradução.zip`);
  - arquivos de pasta e entradas sem compressão ficam mapeados;
  - os comprimidos entram por `rom_override_lazy` (novo no `rom.cpp`): são descomprimidos uma única vez, por uma thread em segundo plano que começa ao abrir o jogo, ou na hora, se o jogo pedir um antes dela (`std::call_once`);
  - um arquivo danificado é lido como zeros, com erro no log;
  - `--check-mods` lista pastas e `.zip`, e descomprime tudo para conferir o CRC;
  - arquivos ao lado do `mod.ini` (leia-me, imagem) são ignorados; fora de `files/` e `overlays/`, geram aviso.
- **Desempenho:** a primeira versão descomprimia só na hora da leitura. O `pcm.pkb` da tradução (31 MB) gerou um quadro de 225 ms. Com a thread em segundo plano, o pior quadro voltou a 14 ms. A tradução inteira (77 MB descomprimidos) leva menos de 1 s, e um mod em zip ocupa RAM com o que descomprime.
- **Robustez:** um nome em `[mods] enabled` que não seja UTF-8 (um `boej.ini` salvo em ANSI) fazia o `std::filesystem` lançar exceção e derrubava o jogo. Agora o mod recebe um erro e o jogo abre sem ele. O `NTR_MODS` é lido com `SDL_getenv` (UTF-8 também no Windows).
- **Launcher:**
  - a lista mostra pastas e `.zip` de `mods/` e se atualiza sozinha (a cada ~1,5 s confere a pasta);
  - "Adicionar mod (.zip)", arrastar um `.zip` ou uma pasta para a janela e `--install-mod` apenas **copiam** para `mods/` (antes, extraíam), conferindo antes o `mod.ini`;
  - o `mod.ini` é lido de dentro do zip, e a lista mostra o nome do arquivo ao lado do nome do mod (dois mods podem ter o mesmo nome);
  - para um zip: "Mostrar na pasta mods", e "Remover" apaga o arquivo.
- `tools/mod.py check` aceita `.zip`. `ini_parse` novo em `config.cpp`. Docs: README, `docs/MODS.md`, `docs/DESENVOLVIMENTO.md`, `LEIA-ME.txt` e o comentário do `boej.ini`.

**Verificação:**
- Linux, roteiro da história, tradução como `.zip` (Deflate, mod dentro de uma pasta no zip): imagens **idênticas byte a byte** às do dump traduzido nos quadros 1500, 3000, 6000, 9000 e 14000.
- Windows (`boej.exe` do MinGW, pelo interop), `Tradução.zip` com acento no nome: quadros 3000 e 9000 idênticos.
- `--check-mods` com pasta, zip, zip corrompido ("não é um .zip válido") e zip sem `mod.ini` ("falta o mod.ini").
- `--install-mod` com pasta, com zip, com o zip que já está em `mods/` e com zip sem `mod.ini` (recusado). Captura da aba Mods conferida.
- Testes Python: 15, todos passando. Linux e Windows compilam sem avisos.

**Não testado:** cliques e arrastar de verdade no launcher, e o launcher do Windows com janela.

## 2026-10-24 — Patches BPS e o primeiro mod oficial: CyberLifePlus (tradução PT-BR)

- Pedido: uma pasta `mods/` no repositório, um mod oficial por pasta, cada um como `.zip` separado nas releases. O
  primeiro é a tradução PT-BR do dump traduzido (BAHAMUT), com o nome **CyberLifePlus**: a tradução é do grupo
  CyberLife, parada há meses, e vai ser continuada aqui com os créditos.
- Problema: o mod com arquivos inteiros tem 76 MB de dados do jogo modificados (545 arquivos), o que vai contra a
  regra de não versionar nem distribuir dados do jogo. Decisão (do usuário): fazer antes o passo 1 do roteiro de
  mods, **patches por arquivo**, e guardar o mod só em patches.
- **Formato:** BPS (beat / Floating IPS), conhecido de quem faz ROM hacks, com CRC-32 do original, do resultado e do
  patch. `files/<caminho>.bps` e `overlays/ovNNN.bin.bps` (sobre a overlay descomprimida).
- **`tools/ntr/bps.py`:** codificador guloso (trechos iguais na mesma posição, depois cópias achadas por um índice
  dos blocos de 8 bytes do original, preferindo o deslocamento da última cópia) e aplicador, que também lê
  TargetCopy (gerado por outras ferramentas). Uma primeira estimativa por XOR dava ~11 MB; o BPS deu 9,3 MB (5,8
  MB zipado). O que sobra é quase todo o `script/eve.pkb` (4,9 MB de patch para 12,6 MB), cujos scripts mudam de
  tamanho e deslocam tudo.
- **`tools/mod.py`:** `from-dump --patch`; `patch` (arquivos inteiros do mod viram patches; um arquivo inteiro vale
  mais que o patch do mesmo caminho, e um igual ao original sai junto com o patch); `expand` (cópia com os patches
  aplicados, para editar); `check` aplica os patches e confere. `pack` virou função, usada também pela release.
- **Jogo** (`mods.cpp` + `bps_apply`/`bps_sizes` em `package.cpp`): o patch é lido ao abrir (é pequeno), o tamanho
  final sai do cabeçalho e o tamanho do original é conferido; o resultado é feito pela mesma thread em segundo
  plano dos arquivos zipados (ou na hora, se o jogo pedir antes), sobre os bytes do `boej.pak`. `--check-mods`
  aplica tudo e confere os CRCs. Inteiro + patch do mesmo caminho no mesmo mod é erro.
- **`mods/CyberLifePlus/`:** gerado com `from-dump --patch` (512 arquivos e 32 overlays, 9,3 MB), `mod.ini` com os
  créditos (CyberLife, tradução original; matheus2x, continuação) e `LEIA-ME.md` com créditos, origem (SHA-1 dos
  dois dumps) e como continuar a tradução (`expand` → editar → `patch` → `check`).
- **Release:** `tools/release.py` empacota cada `mods/<nome>/` como `build/release/<nome>.zip` (nome fixo, para o
  mod continuar ligado quando o jogador troca o `.zip`) e o põe no `sha256sums.txt`; recusa um mod oficial com
  arquivo inteiro do jogo. O CI já publica `build/release/*.zip`; o updater do launcher procura o pacote pelo nome,
  então os zips a mais não atrapalham. As notas da release explicam os zips de mods. `mods/*.zip` no `.gitignore`.
- Docs: `docs/MODS.md` (patches, mods oficiais, distribuição em patches), README (tabela de mods oficiais),
  `docs/DESENVOLVIMENTO.md` e `LEIA-ME.txt` do launcher.

**Verificação:**
- `from-dump --patch` → `expand`: **idêntico byte a byte** ao `from-dump` com arquivos inteiros (512 arquivos, 32
  overlays). `expand` → `patch` de volta dá os mesmos `.bps`.
- Roteiro da história, quadros 1500, 3000, 6000, 9000 e 14000: mod em patches (pasta e `.zip`) com imagens
  **idênticas byte a byte** às do mod com arquivos inteiros.
- `--check-mods` e `mod.py check` com um mod com erros: patch de overlay danificado ("o patch está danificado
  (CRC)"), patch de um arquivo aplicado em outro ("para outra versão desse arquivo") e inteiro + patch do mesmo
  caminho.
- 600 quadros sem janela: 1,46 s sem mod, 1,44 s com arquivos inteiros, 1,50 s em patches (pasta), 1,58 s em
  patches (`.zip`).
- Testes Python: 19, todos passando (novo `tests/test_bps.py`). Linux (GCC), MinGW e llvm-mingw compilam sem avisos.

**Não testado:** a release completa pelo CI (só o `official_mods` foi rodado à parte) e o launcher do Windows com
o mod em patches.

**Pendente:** os créditos do CyberLife só têm o nome do grupo; faltam os nomes dos membros, links e a versão da
tradução original. Cada versão nova de um patch grande (o `eve.pkb`) aumenta o repositório em alguns MB; se
pesar, dá para passar `mods/**/*.bps` para o Git LFS.

## 2026-10-24 — Versão dos mods na release e no launcher

- Pedido: mostrar a versão do mod nas releases e no launcher.
- **Launcher:** a lista da aba Mods mostra `v<versão>` ao lado do nome (os detalhes já mostravam "versão X").
  O jogo também escreve a versão no `boej.log` (`[mods] CyberLifePlus v1.0: 512 files, 32 overlays`).
- **Release:** o nome do `.zip` continua fixo (`CyberLifePlus.zip`), para o mod continuar ligado no `boej.ini`
  quando o jogador troca o arquivo. A versão vai:
  - no rótulo do arquivo na página da release (`gh release create arquivo#rótulo`), "CyberLifePlus.zip — mod
    CyberLifePlus (tradução PT-BR), versão 1.0". O `tools/release.py` escreve `build/release/assets.txt` com
    `caminho#rótulo`, e o `release.yml` passa essa lista ao `gh`;
  - numa seção "Mods oficiais" das notas (`tools/changelog.py`, lendo os `mod.ini` da tag), com "atualizado nesta
    versão" quando a pasta do mod mudou desde a tag anterior.
- **Conferência:** a release é recusada se um mod oficial mudou desde a tag anterior e o `version` do `mod.ini`
  não mudou (ou se não tem `version`). `tools/mod.py` ganhou `ini()` para ler o `mod.ini`.

**Verificação:**
- Captura da aba Mods com o CyberLifePlus: "CyberLifePlus (tradução PT-BR) v1.0" na lista e "versão 1.0" nos
  detalhes.
- Num clone de teste: um commit que muda o mod sem mudar a versão faz o `release.py` parar com "changed since
  v0.1.7 but mod.ini still says version = 1.0"; com `version = 1.1`, sai o rótulo com "versão 1.1" e a nota
  "versão 1.1 … atualizado nesta versão".
- Linux e llvm-mingw compilam sem avisos; testes Python passando.

**Não testado:** o `gh release create` com os rótulos de verdade (só no próximo CI de tag).

## 2026-10-24 — Botão Remover na lista de mods e aviso de que o original não é mexido

- Pedido: um botão para remover um mod adicionado (apagando-o da pasta `mods`) e um aviso, ao adicionar e ao
  apagar, de que só a cópia dentro da pasta do jogo é mexida, e não o arquivo ou a pasta original.
- Antes, "Remover..." só existia nos detalhes, que só aparecem com um mod selecionado.
- **Launcher (`mods.cpp`):**
  - botão **Remover** em cada linha da lista, ao lado das setas, com tooltip; o dos detalhes continua;
  - os dois abrem a mesma confirmação ("Remover mod"), agora no nível da aba (`remove_dialog`, guiada por
    `g_remove`), com o nome do mod, o caminho em `mods/` e o aviso de que o original de onde ele foi copiado continua
    onde estava; Esc cancela. A mensagem depois de apagar repete o aviso;
  - aviso fixo no topo da aba ("Adicionar só copia… Remover só apaga essa cópia…"), tooltip no "Adicionar mod
    (.zip)…" e a mensagem depois de adicionar dizem que o original não foi alterado.
- Docs: `docs/MODS.md` e `LEIA-ME.txt`.

**Verificação:** capturas da aba (botão na linha e aviso quebrando a linha) e da confirmação (aberta por uma
alteração temporária no código, já desfeita). Linux e llvm-mingw compilam sem avisos.

**Não testado:** o clique de verdade em Apagar (o código que apaga é o mesmo de antes).

## 2026-10-24 — Levantamento do que falta traduzir no CyberLifePlus

- Pedido: mapear o que o mod de tradução ainda deixa em japonês e montar um roadmap com a porcentagem que falta.
  Só o levantamento; nada do mod mudou.
- Resultado e roadmap em `docs/TRADUCAO.md`. Resumo: **33 % do texto que o jogador lê está traduzido** (41 % dos
  textos únicos); a história dos capítulos 1–11 está em 91 %. O grosso do que falta: falas dos jogadores nas
  partidas (`evet` 3700, 0 %), perfis (`unitbase.STR`, 0 %), subeventos (`evet` 3300, 26 %) e NPCs (36xx, 3 %).
  Também sobraram ~1.200 falas e ~250 textos de tabela em inglês.
- **Formatos descobertos:**
  - a chave do `.pkh` é o número do evento em decimal (`eve31010000.ssd` → 31010000 = 0x01D92CD0);
  - `evet.pkb`/`mcht.pkb`: o texto dos diálogos. Registros `[u32 BE tamanho][texto\0 + preenchimento]`; cada fala
    tem marcadores `%nF` e é seguida de um registro de leitura (furigana) por marcador. A tradução tira os
    marcadores e deixa os registros de leitura;
  - SSD (`eve`, `mch`, `act`…): cabeçalho de 0x20 bytes, código (tamanho na palavra 4) e dados (palavra 5) com
    registros `[u16 id][u8 índice][u8 tamanho][conteúdo]`; o texto vai até o NUL, o resto é sobra do original;
  - tabelas `logic/*.STR` usam `[漢字/かな]` para furigana; os acentos da tradução são bytes únicos em
    0xA1–0xDF (o lugar do kana de meia largura: `ã` = 0xB4, `á` = 0xB2, `é` = 0xBA, `ê` = 0xBB).
- **Achado:** 822 falas traduzidas passam do fim do registro e escrevem por cima do tamanho do registro de furigana
  seguinte (por isso 15 entradas do `evet` nem andam mais pelos tamanhos). Falta ver no jogo se quebra algo.
- O scanner ficou fora do repositório (scripts temporários); dá para trazê-lo para `tools/` quando a tradução
  começar, para medir o progresso.

**Não testado:** nada foi visto no jogo; os números são de heurística (detalhes em `docs/TRADUCAO.md`). Imagens
com texto não entram na conta.

## 2026-10-24 — CyberLifePlus 1.1: fase 1 da tradução (a história inteira)

- Pedido: fazer a etapa 1 do roadmap de `docs/TRADUCAO.md`, seguindo o padrão da tradução existente para nomes
  de personagens e de técnicas.
- **Traduzido:** 1.281 falas em 366 entradas: o que faltava dos capítulos 1–11 (`evet` 3201–3211), o capítulo 12
  inteiro (3212, a prévia do GO com o Tenma), as aberturas (31xx), as mensagens de campo/lojas/máquina de
  busca/broches (35xx), o `mcht.pkb` (eventos durante as partidas) e as strings do `eve.pkb` (rótulos de quem
  fala, como "Kido?", e a máquina de recuperação). 50 linhas ficaram de fora de propósito (debug do `eve.pkb` e
  furigana sem uso). Falas longas idênticas a falas que o CyberLife já tinha traduzido foram reaproveitadas; as
  curtas não, porque a tradução original às vezes redistribuiu o conteúdo entre falas vizinhas.
- **Padrão:** os nomes vêm das falas já traduzidas, não das tabelas: o diálogo segue a dublagem brasileira
  ("Super Onze", "Mão Fantasma", "Cavaleiros da Rainha"), e as tabelas vieram de um patch em inglês ("Inazuma
  Japan", "God Hand"). Glossário e termos novos em `docs/TRADUCAO.md`.
- **Formatos descobertos:**
  - o `eve.pkb` aponta cada fala com `"@deslocamento,tamanho"`, e o tamanho é a fala + os registros de furigana
    seguintes. A fala traduzida pode usar esse espaço todo: é o que o CyberLife já fazia nas falas que "passavam"
    do registro;
  - em 41 entradas o CyberLife mudou as falas de lugar e atualizou essas referências, às vezes sem mudar o
    tamanho da entrada. A primeira aplicação desta versão escrevia nas posições do original japonês e, em 17
    dessas entradas, gravou 61 falas onde o jogo não lê (por cima de falas já traduzidas). Achado ao passar o
    trabalho para `tools/text.py`; o mod foi refeito a partir da v1.0 com as posições do script. 33 dessas falas
    já estavam traduzidas pelo CyberLife (o japonês visto era sobra num espaço sem uso) e saíram;
  - tabela de acentos tirada da fonte traduzida (`FONT12.NFTR`): ã = B4, ç = B8 etc. (lista no `TRADUCAO.md`);
  - o jogo quebra sozinho as linhas largas (~226 px), mas mostra só 3 linhas por caixa (testado no jogo com uma
    fala de 321 px: virou duas linhas, e a quarta linha não apareceu);
  - `evet`/`mcht` guardam as entradas sem compressão (edição no lugar, mesmo tamanho); o `eve.pkb` é LZ10 por
    entrada, com o `.pkh` alinhado em 16.
- `mod.ini`: versão 1.1. `LEIA-ME.md`: seção de versões e o novo jeito de continuar a tradução.

**Verificação:**
- `tools/text.py check`: 1.281 falas sem problema (cabem no espaço, até 3 linhas por caixa com a quebra
  automática, `%s` mantido, só caracteres da fonte).
- Comparado com a v1.0: das 92.112 posições de texto (falas e strings dos scripts), mudaram exatamente as 1.281
  traduzidas, e cada uma tem o texto da tradução; todas as outras estão iguais. `mod.py check`: ok.
- `text.py survey`: 29 % → 32 % do texto (com repetições), 39 % → 44 % dos textos únicos; capítulos 1–11,
  capítulo 12, 31xx, 35xx e `mcht` em 100 %.
- Roteiro da história até o quadro 14000: quadros 9500, 11000, 12500 e 14000 **idênticos byte a byte** aos da v1.0
  (o prólogo não mudou). O compressor LZ10, usado em 40 entradas do prólogo, também deu capturas idênticas.

**Não testado:** nenhuma das falas novas foi vista no jogo; o roteiro gravado para no quadro ~14000, antes delas.
O teste com a fala de 321 px confirma o mecanismo (escrever no espaço da fala, quebra automática), e o `check`
aplica as mesmas regras a todas.

**Achado à parte:** encerrar o jogo muito cedo (`NTR_EXIT_FRAME=5`) com mods em patch mostra erros falsos de
"não é um patch BPS válido" para o CyberLifePlus; com 300 quadros não aparece nenhum. Parece corrida entre o
encerramento e a verificação dos patches em segundo plano.

## 2026-10-24 — `tools/text.py`: ferramentas da tradução no repositório

- Pedido: trazer para `tools/` as ferramentas usadas na fase 1, sem versionar o texto japonês.
- **`tools/ntr/lz10.py`:** LZ10 (descompressão e compressão gulosa, sem distância 1).
- **`tools/ntr/text.py`:** formatos (pacotes `.pkh`/`.pkb`, dados e referências `@off,len` dos scripts SSD,
  listas de falas do `evet`/`mcht`), codificação com a tabela de acentos, larguras da fonte NFTR, quebra
  automática da caixa e as regras de uma fala (`problems`).
- **`tools/text.py`:** `survey` (quanto falta, por área), `extract` (as falas em japonês com o espaço de cada uma
  e as falas traduzidas em volta), `lookup` (como um termo já foi traduzido), `check` e `apply` (grava a
  tradução no mod como patches, regravando só o que mudou). As falas do `evet` são achadas pelas referências do
  script do mod, o que revelou o erro de posição da v1.1 (entrada anterior) e mais 12 falas da máquina de busca
  que faltavam (traduzidas).
- **`translations/CyberLifePlus/`:** a tradução da fase 1, um `.json` por grupo, com o lugar de cada fala
  (`"evet:32120100:4"`) e o português.
- **Sem texto do jogo no repositório:** o `text.py` recusa traduções com japonês, `extract`/`lookup` só escrevem
  dentro de `build/` (ignorada pelo git) e o `tools/tests/test_text.py` confere que `translations/` não tem
  japonês. As listas da fase 1 (com o japonês) ficaram só no scratchpad desta sessão.
- Docs: `docs/TRADUCAO.md` (números do método novo, passo a passo), LEIA-ME do mod e a tabela de pastas do
  `DESENVOLVIMENTO.md`.

**Verificação:** testes Python: 29, todos passando (novo `test_text.py`: LZ10, acentos, falas e espaço,
pacotes, referências, regras de uma fala, trava contra japonês em `translations/`, `extract` só em `build/`).
`text.py apply` refez a v1.1 a partir da v1.0 (comparação na entrada anterior).

## 2026-10-24 — CyberLifePlus 1.2: fase 2 da tradução, itens e táticas

- Pedido: continuar com a próxima etapa da tradução (fase 2 do roadmap de `docs/TRADUCAO.md`).
- **Formatos descobertos** (no Ghidra):
  - `item.dat`: 1024 registros de 0x2C bytes embaralhados. `func_02071458` carrega o arquivo e
    `func_020748c8` desfaz o embaralhamento (XOR 0xAD, rotação de 2 bits para a direita e trocas de bytes a
    distâncias 2, 4, 6 e 1). O nome do item fica em 0x00–0x1F e o índice da descrição em 0x2A;
  - `item.STR` e `tacticscmd.STR`: textos em passos de 32 bytes. Quem mostra a descrição lê 0x80 bytes a partir
    de índice × 32 (`func_ov110_02124f70` e `func_ov094_02125484` para itens, `func_ov123_02124d84` para táticas).
    Furigana `[漢字/かな]`, quebra de linha com o byte 0x0A;
  - os nomes dos itens estavam em inglês (vindos do patch em inglês) e o levantamento não os via, por causa do
    embaralhamento.
- **`tools/text.py`** passou a cobrir essas tabelas: lugares `item.dat:registro`, `item.STR:deslocamento` e
  `tacticscmd.STR:deslocamento`. A tradução fica no lugar do original, com o espaço até o texto seguinte (e no
  máximo 128 bytes). Como não se sabe se o jogo quebra as linhas dessas janelas, o `check` exige que cada linha
  caiba na largura da linha mais larga da tabela (`item.STR`: 2 linhas de 200 px; nomes: 1 de 133 px). Novo
  `extract --redo`, que também lista o que o mod tem em outra língua. O `survey` agora lê o `item.dat`.
- **Traduzido:** 820 descrições de itens, 434 nomes de itens (do inglês) e as 43 táticas (nomes e descrições).
  As descrições dos 349 cadernos de técnicas seguem uma fórmula ("Fogo  TP 46  1 jog." / "Caderno de técnica de
  chute") e foram geradas por regra; as outras foram escritas à mão. Termos novos (status, elementos, tipos de
  item) no glossário do `TRADUCAO.md`.
- **Ficaram como estavam, de propósito:** os nomes dos cadernos de técnicas (são os nomes das técnicas), das
  táticas e das habilidades (46 itens). As técnicas aparecem com os mesmos nomes em inglês no `command.STR`, e a
  troca deve ser feita junto, seguindo os nomes do diálogo ("Mão Fantasma"). Os nomes das táticas no
  `tacticscmd.STR`, que estavam em japonês, receberam os mesmos nomes em inglês dos itens. Os menus continuam
  dizendo "Kick", "Guard" etc.
- `mod.ini`: versão 1.2. `LEIA-ME.md`: versão e o que o `text.py` cobre.

**Verificação:**
- `text.py check`: 2.578 linhas sem problema (1.281 da fase 1 + 1.297 novas).
- Comparação com o mod de antes: no `item.dat`, só mudaram os nomes traduzidos (o resto de cada registro está
  igual, já desembaralhado); `item.STR` e `tacticscmd.STR` são iguais ao original, a não ser pelas traduções nos
  lugares esperados.
- Testes Python: 32, todos passando (novos: embaralhamento do `item.dat` ida e volta, textos do `.STR`, regras
  das tabelas). `mod.py check`: ok.
- `text.py survey`: 34,4 % do texto (com repetições) e 47,1 % dos textos únicos; descrições de itens 99,8 % (os
  22 caracteres que sobram são 2 textos que nenhum item usa) e táticas 100 %.

**Não testado:** nada disto foi visto no jogo. O roteiro gravado não chega a nenhum menu de itens. Falta confirmar
que as janelas de itens mostram bem o texto de meia largura com acentos (os nomes em inglês do CyberLife já
eram de meia largura) e se o jogo quebra linhas longas nessas janelas.

## 2026-10-24 — CyberLifePlus 1.2: nomes de técnicas em romaji

- Pedido: técnicas não precisam de tradução, mas devem ficar em romaji legível (ex.: "Majin the Hand").
- **Achado:** a lista de técnicas do jogo (`command.STR`, do CyberLife) tinha traduções do inglês ("Divine
  Stamp" para さばきのてっつい, "Dirt Ball", "Table-Turner", "Teleport Shot"), erros ("Mughen the Hand", "Greet
  Barrier Reef", "Safety Fist") e 4 habilidades ainda em japonês (風のごとし！…). Os nomes dos cadernos nos itens
  nem sempre batiam com ela ("Tenma Shot" no item, "Pegasus Shot" na lista).
- **Regra** (no `TRADUCAO.md`): japonês vira romaji, katakana que vem do inglês fica em inglês, ごう = "gou" sem
  hífen (com hífen, "Koutei Penguin 2-gou" passava da largura). Mesmo nome na lista e no caderno.
- **`command.dat`:** registros de 0x24 bytes, índice do nome no `command.STR` em 0x18 e da descrição em 0x1A (sem
  embaralhamento). O `text.py` passou a cobrir esses nomes (`command.dat:registro`): escreve em largura cheia,
  como o CyberLife, e o nome que não cabe no lugar vai para o fim do arquivo, com o índice novo. Largura máxima
  124 px (o nome mais largo do CyberLife, "Honoo no Kazamidori").
- **Revisado:** 360 nomes da lista (72 trocados, incluindo as 4 habilidades: "Kaze no Gotoshi!" etc.), os 349
  cadernos e as táticas com nome japonês ("Muteki no Yari", "Andes no Arijigoku", "Engetsu no Jin") nos itens e
  no `tacticscmd.STR`. As outras habilidades e os comandos básicos (Feint, Tackle…) ficaram como estavam.

**Verificação:** `text.py check`: 3.291 linhas sem problema. No `command.dat` só mudaram 8 índices de nome (os
nomes que foram para o fim do arquivo); todos os nomes lidos pelo índice são os da tradução, e as descrições
continuam as mesmas. Rodar o `apply` de novo não muda nada. Testes Python: 33, todos passando (novos: largura
cheia e a mudança de lugar de um nome). `mod.py check`: ok.

**Não testado:** nada visto no jogo, nem a lista de técnicas nem os itens.

**Pasta de teste do Windows** (`C:\Games\ie3-teste\ie3ogre-recomp`): atualizada da 0.1.3 para uma release 0.1.8
gerada deste código (`tools/release.py`). Troquei o launcher, o `SDL3.dll`, o `sdk/`, `licenses/`, `LEIA-ME.txt` e
`TERCEIROS.txt`; mantive `boej.ini`, `boej.pak`, `cache/`, `toolchain/`, `saves/` e os replays. Criei `mods/` com o
`CyberLifePlus.zip` (versão 1.2, com os itens e as técnicas desta sessão). O `ie3-launcher.exe --prepare` recompilou
o jogo (185 arquivos em 36 s; `build.stamp` 0.1.8). O mod ainda precisa ser ligado na aba Mods.

## 2026-10-24 — Objetivos da tela de cima: letras sobrepostas e textos em inglês

- Pedido: no teste do Windows, o objetivo "O que aconteceu? Vá averiguar!" aparecia com o "t" sobre o "e" e o
  "v" sobre o "e"; o card do save tinha textos em japonês; na digitação do nome do save, as letras não caem
  nos quadradinhos.
- **Card do save em japonês:** o card mostra o lugar e o objetivo gravados no save, e o save era de antes do
  mod. Os dois textos já estavam traduzidos no mod ("Futuro - Rua", o objetivo); um save novo mostra em
  português (o usuário confirmou).
- **Objetivos:** são as strings do comando 0x201D dos scripts (`[u16 id][u16 tamanho][u16 opcode]…`; a string é
  o registro 3 da instrução). São 102, e uns 80 ainda estavam em **inglês** (do patch em inglês), alguns com
  espaços no meio das palavras ("para tre inar"), que eram a tentativa do CyberLife de contornar o problema.
- **Como o jogo desenha o objetivo** (medido no jogo com textos de teste, pela sessão gravada do usuário e
  capturas; a OAM mostra 4 sprites 64×32 em x = 8, 68, 128, 188): o texto vai em 4 sprites de 64 px colocados a
  cada 60 px. O espaço de 1 px entre letras vem antes de cada letra, menos da primeira de um sprite, e uma
  letra só passa para o sprite seguinte quando não cabe nos 64 px. Em japonês (5 letras de 12 px por sprite)
  isso nunca aparece; com letras estreitas, os últimos 4 px de um sprite caem sobre o começo do próximo. Cada
  sprite desce 1 px em relação ao anterior, também no japonês original. O modelo reproduz as posições
  medidas em 6 textos de teste, pixel a pixel.
- **Correção, sem mexer em código:** o `text.py` reconhece os objetivos, simula o desenho
  (`objective_layout`) e grava cada um com espaçadores de 1 px junto dos espaços onde é preciso
  (`objective_fit`), para que nenhuma letra fique sobre outra e o vão entre palavras tenha pelo menos 4 px. O
  espaçador é o código 0xCF da `FONT12` (um glifo vazio que nenhum texto do jogo usa), que o `apply` deixa com
  largura 0 (avança 1 px). Um espaçador no começo de um sprite não conta; um espaço conta.
- **Traduzido:** os 102 objetivos, a partir do japonês, com os nomes do diálogo (Estádio Pavão, Ilha Hidra,
  Fideo, Restaurante Tora…). Alguns ficaram mais curtos por causa do espaço de cada string, que também
  precisa caber os espaçadores.
- **Nome do save:** o campo desenha o nome inteiro com a fonte proporcional, e os quadradinhos são de 12 px (uma
  letra japonesa). O teclado do CyberLife digita letras de meia largura. Não dá para alinhar só com dados: as
  letras de largura cheia da fonte traduzida também são proporcionais (e servem aos nomes das técnicas).
  Ficou anotado como limitação; resolver pede mudar o código do campo de nome.
- `mod.ini`: versão 1.3. `LEIA-ME.md` e `TRADUCAO.md` (regras dos objetivos).

**Verificação:**
- Pela sessão gravada do usuário (quadro 4405), seis objetivos traduzidos foram desenhados no jogo (no lugar do
  primeiro objetivo): nenhuma letra sobreposta, vãos entre palavras de 4 px ou mais. O zip final da 1.3 dá a
  mesma imagem ("Que barulho é esse? Vamos ver!").
- `text.py check`: 3.393 linhas sem problema. Testes Python: 35, todos passando (novos: posições medidas no
  jogo e o ajuste). `mod.py check`: ok.
- Pasta de teste do Windows: `mods\CyberLifePlus.zip` trocado pela 1.3.

**Não testado:** os outros 101 objetivos só foram simulados (o simulador reproduz as medições, e seis foram
desenhados no jogo). Saves antigos continuam mostrando o objetivo antigo no card até salvar de novo.

## 2026-10-24 — Nome do save: uma letra por quadrado (hook no runtime)

- Pedido: corrigir o campo do nome do save para cada letra (japonesa ou não) cair no seu quadrado. Corrige a
  limitação anotada na entrada anterior; a correção é no código do port, não no mod.
- **Medido** (sessão gravada do teste, quadros 1075/1110, nome "ＺＺＺＷＭＣ"): os quadrados ficam em
  x = 96 + 13·k, com 12 px cada. O teclado grava letras de **largura cheia** em SJIS (no buffer `0x0209A460`: `82 79`
  = Ｚ), no modo latino e no japonês. A nota anterior estava errada: não são letras de meia largura.
- **Como o jogo desenha:** `func_ov055_0211b07c` limpa o bitmap do campo e chama, em `0x0211B13C` (`blx r5`), o
  método +8 da classe de fonte (vtable `0x0208FFEC`), `func_02042e4c`, que desenha a string com espaçamento 2. Cada
  letra avança pelo 3º byte do CWDH + 2. Achei a função comparando os perfis (`NTR_PROFILE`) de quadros com e
  sem digitação.
- **Causa:** na `FONT12` original, 2.127 das letras têm avanço 11, e 11 + 2 = 13, o passo dos quadrados. Na fonte
  traduzida o avanço é proporcional (Ｚ = 6, ア = 10), então até os kana saem do lugar.
- **Correção:** nomes novos em `names.tsv` (`Font_DrawString`, `Font_GlyphIndex`, `Font_GlyphWidths`) e o hook
  `font_draw_string` (`hle.tsv`, `hle.cpp`). Só quando a chamada vem do campo de nome (retorno `0x0211B140`,
  overlay 55 ativa nessa página e a instrução `blx r5` no lugar), o hook desenha uma letra por chamada, no
  quadrado k. Letra com avanço 11 fica onde o jogo a põe; as outras são centralizadas pela tinta (esquerda e
  largura do CWDH). Para isso, o nome é cortado no próprio buffer, um byte de cada vez, e o byte é restaurado
  em seguida. Nas outras chamadas, o original roda direto. Novo `overlay_active_at` no `cpu.cpp`.

**Verificação:**
- Com o CyberLifePlus: "ZZZWMC" e, no modo "Jap" do teclado (toques acrescentados à sessão), "あき". Todas as
  letras ficam dentro do quadrado, com 3 px de cada lado no Z, e no pior caso 1 px de um lado e 2 do outro
  (W, あ).
- Sem mod (fonte japonesa): os quadros 1075 e 1110 saem idênticos, pixel a pixel, aos do build de antes do hook.
- Windows: release 0.1.8 gerada deste código, na pasta de teste (launcher, `sdk/`, `licenses/`, leia-me). Apaguei
  o `cache\build.stamp` para o `--prepare` recompilar (mesma versão), e ele levou 60 s. O `boej.exe` dá a mesma
  imagem do Linux no quadro 1220.
- Testes Python: 35, todos passando.

**Não testado:** os modos do campo que começam em x = 5 (valor 4, 5 ou 6 em `0x0209A458`) não apareceram na
sessão. O hook soma o quadrado ao x que recebe, então deve valer para eles também.

## 2026-10-24 — Mods oficiais na release como `mod-<nome>-<versão>.zip`, e o launcher atualiza o mod no lugar

- Pedido: o asset de cada mod oficial na release passa a se chamar `mod-<nome>-<versão>.zip` (a versão do
  `mod.ini`), por exemplo `mod-CyberLifePlus-1.3.zip`, no lugar de `CyberLifePlus.zip`.
- `tools/release.py` (nome do zip e rótulo), `tools/changelog.py` (seção "Mods oficiais"), `docs/MODS.md`,
  `docs/DESENVOLVIMENTO.md` e `launcher/LEIA-ME.txt`. O `LEIA-ME.md` do CyberLifePlus ainda fala em
  `CyberLifePlus.zip`: mudá-lo exigiria uma versão nova do mod (a release recusa um mod alterado com a mesma
  versão). Fica para a próxima versão do mod.
- O `[mods] enabled` guarda o nome do arquivo, então trocar o `.zip` por uma versão nova deixaria o mod
  desligado. Pedido seguinte: o launcher resolve isso (`launcher/mods.cpp`), reconhecendo o mesmo mod pelo
  `name` do `mod.ini`:
  - **Adicionar** (botão, arrastar ou `--install-mod`): as outras cópias do mesmo mod com versão igual ou mais
    velha são apagadas da pasta `mods`, e o novo fica no lugar delas na lista (ligado, se estavam). Uma versão
    mais velha que a da pasta entra desligada e não apaga nada.
  - **Colocado à mão** na pasta: na próxima listagem, uma versão mais nova de um mod ligado fica no lugar dele, e
    o antigo continua na pasta, desligado.
  - As versões são comparadas número a número ("1.10" é mais nova que "1.9").
  - Ao abrir o `boej` direto, sem o launcher, nada disso acontece.

**Verificação:** `tools/release.py` gera `mod-CyberLifePlus-1.3.zip`, com o mesmo nome no `assets.txt` e no
`sha256sums.txt`; as notas (`changelog.py`) citam `mod-CyberLifePlus-1.3.zip`. Testes Python: 35, todos passando.
A tag v0.1.9 já foi enviada com o nome antigo (`CyberLifePlus.zip`); o nome novo vale a partir da próxima release.

Launcher (Linux, numa pasta de teste com zips só com o `mod.ini`): `--install-mod` de `mod-CyberLifePlus-1.3.zip`
com o `CyberLifePlus.zip` 1.3 ligado (troca o antigo e mantém a ordem dos ligados, `…, Outro.zip`), depois da 1.4
(troca a 1.3) e da 1.2 (entra desligada, a 1.4 continua). Com a 1.10 posta à mão, a aba Mods (captura) a liga no
lugar da 1.4, que fica desligada. Não testado: adicionar pelo botão e arrastar na janela (o mesmo código do
`--install-mod`, aplicado na listagem seguinte).

## 2026-10-24 — Release: o mod aparece só com o nome do arquivo

- Problema: na release 0.1.10, o mod aparecia como "mod-CyberLifePlus-1.3.zip — mod CyberLifePlus (tradução
  PT-BR), versão 1.3.zip". O `release.py` passava um rótulo (`caminho#rótulo` no `assets.txt`) ao
  `gh release create`, e a página mostra o rótulo no lugar do nome. Com a versão já no nome do arquivo, o rótulo
  sobrava.
- Correção: o `assets.txt` lista só os caminhos, e a página mostra `mod-CyberLifePlus-1.3.zip`. Ajustados o
  comentário do `release.yml`, `docs/MODS.md` e `docs/DESENVOLVIMENTO.md`.
- Verificação: `tools/release.py` gera o `assets.txt` sem rótulos. Na release 0.1.10 já publicada, tirei o rótulo
  do asset pela API (`PATCH releases/assets/<id>`, `label` vazio); agora ela mostra só `mod-CyberLifePlus-1.3.zip`.

## 2026-10-24 — CyberLifePlus 1.4: títulos da galeria de vídeos

- Pedido: seguir com os próximos passos da tradução. Pelo roadmap, o resto da fase 2, a começar por "locais e
  cenas" (`fieldinf.dat`, `movie_view_f.dat`).
- **`movie_view_f.dat`** (galeria de vídeos: Continuar → Gallery → Game Video): 58 registros de 0x74 bytes, com
  id, nome do vídeo em +0x02, número ("ＮＯ．１") em +0x0C e título com furigana em +0x1B. Confirmado em
  `ov037` (`0x0211BA84`/`0x0211BA88` somam 0x0C e 0x1B ao registro).
- **`fieldinf.dat`** ficou de fora: 149 nomes de locais em registros de 0x180 bytes (nome em +0x90, até 20
  bytes; `func_0206f834` lê o registro). Parecem rótulos internos: nomes cortados no meio ("…グラウン"), treinos
  e minibatalhas, e o patch em inglês também não os traduziu. Fica na fase 5 (ver no jogo antes).
- **Galeria em branco:** com os títulos em ASCII, a caixa mostrava só o "NO.1". Testes no jogo (save do teste
  do Windows, sessão headless até a galeria) e a leitura do código explicaram o problema. O furigana é lido por
  `func_ov016_020f24e8` com `char` com sinal: todo byte ≥ 0x80 conta como primeiro de dois e é copiado junto com
  o seguinte, e o resto do ASCII é jogado fora. Achei a função cruzando o `NTR_PROFILE` (build com
  `NTR_TRACE`) com as funções que comparam com `[` e `/`. Um acento de 1 byte desalinha os pares, e então a
  segunda metade de "Ａ" (0x60) cai como ASCII e some. Isso explica o "：Ａ" que virou outros símbolos num
  dos testes.
- **Correção no `text.py`:** nova forma de tabela `PAIRED` (`paired()` em `tools/ntr/text.py`). Cada caractere
  de 1 byte vai em par com o espaçador 0xCF, que não desenha nada: ASCII vira `CF c`, acento vira `b CF`. O
  `Font_DrawString` desenha o resultado normalmente, com espaço e pontuação de meia largura. A flag de largura
  cheia das tabelas virou `WIDE` (a lista de técnicas). O `apply` também grava a `FONT12` com o espaçador sem
  tinta quando há títulos da galeria. Lugar novo: `movie_view_f.dat:registro`.
- A caixa não põe o pixel entre as letras (`Font.measure(s, gap)`), e um título de teste de 229 px, de x = 16
  até perto da borda, apareceu inteiro: esse é o limite do `check`. Como os pares dobram os bytes, cabem umas
  44 letras nos 88 bytes. As letras ficam coladas, como o "NO.1" do CyberLife. Pensei num segundo espaçador de
  1 px, mas exigiria mais um código de 1 byte na fonte, e não dá para garantir que nenhum texto o use.
- **Traduzido:** os 58 títulos (`translations/CyberLifePlus/movie_view_f.json`). Os nomes seguem as falas em
  português: Estádio Hidra, Yamaneko, Pavão, Condor, Tartaruga e Titanic, Ilha Liocott, Ogros, Senhor K,
  Hidetoshi Nakata, Michiya Kudou. Técnicas em romaji/inglês, como na lista: Tiger Drive, Snow Angel,
  Tatsumaki Otoshi, Grand Fire, Pegasus Shot, Strike Samba.
- `survey`: as duas áreas foram separadas. Títulos da galeria: 100 %. Nomes de locais: 0 % (984 caracteres).
  Total: 34,5 % com repetições e 47,3 % dos textos únicos.
- `mod.ini` 1.4. O `LEIA-ME.md` do mod ganhou a versão e o nome novo do zip (`mod-CyberLifePlus-<versão>.zip`,
  pendente desde a entrada da release 0.1.10). Também atualizei o `docs/TRADUCAO.md`.

**Verificação:**
- No jogo (Linux, `build/cmake-rel`, save do teste do Windows), o título nº 1 aparece como "Inazuma Eleven 3:
  abertura". O título mais largo ("As lágrimas de Kageyama e os óculos de Kido") e um texto de 229 px foram
  postos no registro 0 para teste: aparecem inteiros, com os acentos. Os outros 57 títulos estão bloqueados
  nesse save (capítulo 1); não foram vistos, mas seguem as mesmas regras.
- No arquivo, só os títulos mudaram (o cabeçalho dos 58 registros está igual ao original).
- `text.py check`: 3.451 linhas sem problema. Testes Python: 36, todos passando (novo: a forma `PAIRED`).
  `mod.py check`: ok.
- Pasta de teste do Windows: `mods\mod-CyberLifePlus-1.4.zip` ao lado do `CyberLifePlus.zip` (1.3). Pela
  regra do launcher, a 1.4 fica ligada no lugar da 1.3.

**Achados para depois:** o menu da galeria está em inglês ("Gallery", "Game Video", "Web Identifiant", "Choose
the video you want to replay.", "Play", "Quit", "Back"), assim como o aviso sobre saves alterados que aparece
ao carregar. Com um save de antes do mod, a faixa de cima do jogo mostra o lugar e o objetivo gravados em
japonês ("?? ストリート"), como o card do save.

## 2026-10-24 — `TRADUCAO.md`: checklist da tradução

- Pedido: o `docs/TRADUCAO.md` deve ter um checklist do que já foi feito e do que está previsto, e ganhar itens
  novos conforme as etapas revelam coisas.
- A seção "Roadmap" virou "Checklist", em listas `- [x]`/`- [ ]` por fase (1 a 5). O que estava espalhado
  também entrou: as notas "não testado" do diário (itens, técnicas, objetivos, galeria), os textos em inglês
  achados (menu da galeria, aviso de saves), os menus "Kick"/"Guard", as legendas e o aviso de ficção. Duas
  seções novas: "Fora do mod" (ARM9, saves antigos com o lugar e o objetivo em japonês) e "Créditos e pacote"
  (nomes dos membros do CyberLife, pendente desde a 1.0).
- A partir de agora, cada etapa da tradução marca o que terminou (com a versão do mod) e acrescenta o que
  descobriu.

## 2026-10-24 — CyberLifePlus 1.4: rótulos de quem fala nas partidas (`mch.pkb`)

- Pedido: o próximo item da fase 2, "rótulos de ação (27)". O número vinha do grupo "Outros scripts" do
  levantamento (`act/mch/mrobj/help.pkb`): 24 textos do `act.pkb` e 3 do `mch.pkb`.
- **O que cada string é**, pelo comando da instrução a que pertence (novo `ssd_opcodes` em `tools/ntr/text.py`):
  - 0x3070 escreve só no log de depuração. São todas as do `act.pkb` ("PK GK左ボール左×", "ブロック 成功",
    "■たくてぃくす1") e as "■■■ゲーム勝利■■■", "=%d" e "2009/7/" do `mch.pkb`. Ficam como estão;
  - 0x3019, registro 2, é o rótulo de quem fala, o mesmo que a fase 1 traduziu no `eve.pkb` ("Senhor K",
    "Daisuke?"). No `mch.pkb` sobravam ひびき, たえ e イチノセ, em 19 lugares das partidas 34010007 e 34010022.
- **`text.py`:** os scripts do `mch.pkb` entram como os do `eve.pkb` (`script_slots`, lugar
  `mch:entrada:deslocamento`). As strings do 0x3070 saem das listas e também do `survey`. As 45 linhas que o
  `eve.json` pulava com `""` eram todas desse comando e foram tiradas do arquivo. O `apply` recomprime a entrada
  quando ela era LZ10 (83 das 84 do `mch.pkb`), em vez de olhar o nome do pacote.
- **Traduzido:** Hibiki, Tae e Ichinose (`translations/CyberLifePlus/mch.json`), como no diálogo e no `eve.json`.
  Entra na 1.4, ainda não lançada.

**Verificação:** comparadas com o mod do último commit, só as duas entradas mudaram no `mch.pkb`. Nelas, só os
19 rótulos; o resto dos bytes descomprimidos está igual. Rodar o `apply` de novo não muda nada. `text.py check`:
3.470 linhas sem problema e 5 puladas (furigana sem uso). Testes Python: 37, todos passando (novo:
`ssd_opcodes`). `mod.py check`: ok. `survey`: "Outros scripts" 100 %; total 34,5 % com repetições e 47,3 % dos
textos únicos (os prints de depuração saíram da conta). O zip 1.4 da pasta de teste do Windows foi refeito.

**Não testado:** as duas partidas não foram vistas no jogo. O rótulo é desenhado como o do `eve.pkb`, que já
aparece em português.

## 2026-10-24 — CyberLifePlus 1.4: menus e mensagens nos overlays

- Pedido: o próximo item da fase 2, os menus nos overlays.
- **O que faltava:** 321 strings em japonês (236 diferentes) em 38 overlays, contando só as que estão fora do
  código e para as quais a própria overlay tem um ponteiro (o resto era código lido como texto). Quase tudo de
  comunicação (DS sem fio, Wi-Fi Connection, trocas, すれちがい, ligação com o Inazuma Eleven 2 e o
  Spark/Bomber), mais os avisos da formação (`ov057`), o quiz do `ov023` e as janelas de scout e itens premium.
- **Onde a tradução pode ficar:** o código recompilado guarda o endereço de cada string, então a tradução fica no
  lugar e cabe até o NUL e o alinhamento de 4 bytes da original. Só 4 traduções precisavam das palavras zeradas
  seguintes. Não usei esse espaço, porque essas palavras podem ser dados, e encurtei as 4.
- **Como as mensagens são desenhadas:** as janelas de mensagem dos overlays usam a classe do `ov016`
  (`func_ov016_020f2ab4` → `020f2184`/`020f2304` → `020f24e8`), o mesmo leitor de furigana da galeria, que
  descarta o ASCII. A prova veio das 76 strings que o CyberLife traduziu nos overlays: as mensagens ele sempre
  escreveu em largura cheia ("Ｅｑｕｉｐ　ｎｏｗ？"), os rótulos de botão em meia largura ("Defend"). A análise
  estática (de quem usa cada endereço até o leitor) não fechou, porque as strings passam por tabelas e objetos.
- **`text.py`:** novo grupo `ov` (lugar `ovNNN:0xdeslocamento`, `overlays.json`, `extract --only ov`).
  - As mensagens são gravadas em pares (`PAIRED`), com as linhas e a largura das mensagens originais do mesmo
    overlay (com 1 px entre letras, por segurança).
  - `paired()` ficou mais econômico: um acento carrega a letra seguinte no mesmo par. O `%s` fica inteiro para o
    `sprintf`, que roda antes do leitor.
  - Os rótulos com vizinhos em meia largura do CyberLife (`PLAIN_LABELS`: os botões do `ov050` e o "せめろ" do
    `ov057`) vão como texto comum, com 12 px por letra do original.
  - O `apply` grava os overlays como patches (`write_overlay`).
- **Traduzido:** 312 strings em 34 overlays. Sem tradução, de propósito:
  - senhas comparadas com o que o jogador digita: "ゆうきがあれば" (`ov055`), "なつみぶか"/"ぜうすくろ"
    (`ov016`), "てんま" (`ov028`), "トンコツ" (`ov131`);
  - rótulos curtos que não cabem em pares e cujo desenhista não sei: "そうび" (`ov053`), "かう"/"うる" (`ov062`),
    "カップ" (`ov072`), "ファイター" (`ov082`).

  Termos: Spark/Bomber, Inazuma Eleven 2, DS sem fio, Wi-Fi Connection, Passing (como o menu do CyberLife),
  troca, recrutar/scout, pontos de amizade, Mistre, Escaba, God Cannon. Os textos são curtos porque os pares dobram
  os bytes.

**Verificação:**
- No jogo, a galeria com um título de teste cheio de acentos e um `%s` ("Prenúncio às três: açúcar, é %s
  ônibus") aparece certa: os acentos que carregam letra e o alinhamento depois do `%s` funcionam. Nenhuma
  mensagem de overlay foi vista: na formação do save de teste (um jogador, sem formações para escolher) não deu
  para provocar os avisos do `ov057`; o menu do Y mostra os rótulos do CyberLife.
- Comparados com o mod do último commit, os 34 overlays só mudaram dentro dos espaços das strings traduzidas.
  Rodar o `apply` de novo não muda nada. `mod.py check`: ok (nenhum byte de código mudou).
- `text.py check`: 3.781 linhas sem problema e 15 puladas. Testes Python: 37, todos passando. `survey`: menus
  no código 95 %; total 35,2 % com repetições e 48,3 % dos textos únicos. Zip 1.4 da pasta de teste refeito.

**Achado:** nomes de jogador em meia largura ("Kanon") devem sumir nas mensagens com `%s` que passam pelo leitor
de furigana, inclusive nas do CyberLife. Entrou no checklist; a correção seria no port (o leitor) ou com nomes em
largura cheia.

## 2026-10-24 — Launcher compila com 80% dos núcleos; sliders do menu sem piscar

- Pedido: na recompilação, usar 80% dos núcleos (com todos, o PC fica lento); e o menu do Esc piscava ao mexer
  num slider das configurações.
- **Núcleos:** `compile_workers()` em `launcher/prepare.cpp` é 80% de `hardware_concurrency()`, no mínimo 1. Vale
  para as threads de compilação e para a mensagem "Compilando o jogo (N arquivos, M núcleos)".
- **Piscada:** a cada passo do arraste, o slider chamava `set()`, que grava o `boej.ini` e roda
  `apply_settings()`. Essa função refazia a apresentação lógica do SDL (o que perde um quadro) e, no tamanho da
  janela, redimensionava e recentralizava a janela a cada valor. Correção nos dois lados:
  - `slider_setting` (`runtime/ui/ui.cpp`): o slider mostra o próprio valor enquanto se move e só grava e
    aplica ao ser solto (`IsItemDeactivatedAfterEdit`). Usado no tamanho da janela e no volume;
  - `apply_presentation` (`runtime/src/host.cpp`) só chama `SDL_SetRenderLogicalPresentation` quando a
    largura, a altura ou o modo mudaram. Assim as outras opções (caixas de marcar, combos) também não perdem
    quadro.

**Verificação:** Linux (`build/cmake-rel`) e o pacote de Windows (`tools/release.py`) compilam sem avisos; o
`sdk/` do zip tem as duas mudanças. Não testado: o menu do Esc com a janela de verdade (o roteiro sem janela não
abre o menu) e a compilação no launcher com o número novo de núcleos. O zip foi copiado para a pasta de teste
do Windows, mas o jogo estava aberto e a instalação não foi atualizada.

## 2026-10-24 — Objetivo da tela de cima alinhado (hook no runtime)

- Pedido: no objetivo ("Hibiki está esperando! Vá à Raimon!"), a parte da direita descia um pouco e o texto
  ficava torto.
- **Causa (do jogo, não do mod):** a OAM mostra os 4 sprites do objetivo todos em y = 177; a escada está dentro
  deles. O objetivo é desenhado pelo `Font_DrawString` (`lr` = `0x020C739C`, em `func_ov000_020c72d4`) num quadro
  de 64×128 na VRAM de sprites. A função quebra a linha quando a letra não cabe nos 64 px, somando ao y a
  altura da fonte (12) mais o espaçamento entre linhas do objeto de fonte (campo +0x18, que ali vale 5): passo de
  17 linhas. Os sprites começam a cada 16 linhas do quadro (tiles 178/194/210/226, 512 bytes um do outro), então
  a linha k aparece 1 px mais baixa que a anterior. No japonês original também, mas quase não se nota.
  Achei a função pelo `NTR_PROFILE`, que agora lista todas as funções (antes parava em 1000), e por um log
  temporário dos argumentos do `Font_DrawString`.
- **Correção:** no hook `font_draw_string` (`runtime/src/hle.cpp`), a chamada do objetivo (retorno `0x020C739C`,
  overlay 0 ativa e `blx r4` no lugar) roda com espaçamento 4, e o valor original volta depois. Passo de 16: as
  linhas caem todas na mesma altura dos sprites. `hle.tsv` com a descrição nova.

**Verificação:** com o save do teste, a última linha com tinta de cada sprite do objetivo passou de 19/20/21
para 19/19/19 (comparado com o build de antes do hook). O espaçamento horizontal e os espaçadores do `text.py`
não mudam. Não testado: um objetivo em português no jogo (o save de teste guarda um objetivo antigo, em
japonês) e o objetivo do card do save na tela de Continuar, que pode ser outra chamada.

## 2026-10-24 — 3D desenhado no início do quadro: a letra velha que piscava ao abrir um diálogo

- Pedido: ao começar um diálogo, os primeiros caracteres aparecem errados e depois são trocados pelos da
  tradução; verificar se é isso mesmo.
- **Não é a tradução.** Quadro a quadro (sessão `story-replay.txt`): ao abrir uma caixa nova, a posição da
  primeira letra mostra por 2 quadros a primeira letra da fala anterior ("N" de "Não dá!" antes de "O que tá
  acontecendo?"). Quando as duas falas começam com a mesma letra, não se vê nada. Na primeira fala do jogo
  aparece um "ー", e o jogo sem mod faz o mesmo. O texto novo já está na memória nesse quadro (procurado
  nos savestates), então é imagem velha, não texto velho.
- **Causa (port):** a caixa de diálogo é 3D (o BG0 da tela de baixo), com o texto numa textura que o jogo
  atualiza no VBlank. O port desenhava o 3D no próprio comando SWAP_BUFFERS, no meio do quadro, lendo as
  texturas naquele instante. O hardware desenha a lista trocada durante o quadro seguinte, já com o que o jogo
  gravou no VBlank. Então toda textura atualizada no VBlank aparecia um quadro atrasada, e ao abrir a caixa
  sobrava a letra antiga.
- **Correção** (`runtime/src/gpu3d.cpp`, `timing.cpp`, `gpu2d.cpp`): o SWAP_BUFFERS só entrega a lista
  (`g.drawn`). O novo `gpu3d_frame_start()`, chamado no começo da linha 0, desenha essa lista e a põe na
  frente, no lugar do antigo `gpu3d_vblank()`. A latência de exibição é a mesma (a geometria do quadro N
  aparece no quadro N+1). O savestate guarda a lista pendente em vez do buffer de trás já desenhado; o
  formato mudou, mas um savestate já só vale para o mesmo build.
- De passagem: `NTR_PROFILE` lista todas as funções (antes parava em 1000).

**Verificação:** as duas aberturas de caixa testadas saem vazias e mostram direto o "O" certo. Sessão da história
comparada com o build anterior: os quadros 9000, 10500 e 12000 são idênticos pixel a pixel. Em 13000 e 14000
muda só o quadro da animação do personagem (textura animada, agora em dia com o hardware) e o objetivo (pela
correção de alinhamento anterior). Savestate salvo e carregado: a mesma diferença de 157 pixels que o build
anterior dá nesse teste (a corrida carregada não tem as entradas da sessão). Linux e Windows (MinGW) compilam
sem avisos.

## 2026-10-24 — Legendas dos vídeos sem espaço entre as palavras (regressão da 1.3)

- Pedido: em algumas cinemáticas, as palavras das legendas ficam grudadas ("MechamoUtsonomiya Toramaru!",
  "Eseeufizerisso?").
- **Causa (o mod, desde a 1.3):** as legendas (`movie/txt`: registros `[u32 quadro inicial][u32 final][u32
  tamanho][texto]`) passam pelo leitor de furigana, que descarta o ASCII. O CyberLife escreve as letras em
  largura cheia e cada espaço como "0xCF + letra seguinte" ("Ｍｅ" `CF` `c` "ｈａｍｏ"): o 0xCF é o espaço da fonte
  dele (avanço 3, 4 px com o pixel do jogo). Na 1.3 escolhi o 0xCF para o espaçador dos objetivos e o
  `fit_font` zerou a largura dele. Isso apagou esses espaços nas legendas (617), em scripts (142) e em tabelas
  (20).
- **Correção:** o `SPACER` passou para o 0xDF, que nenhum texto usa (contei nas falas, scripts, `.STR`,
  legendas e strings dos overlays). O `fit_font` zera o 0xDF e devolve ao 0xCF os valores do CyberLife
  (0, 0, 3); o `decode` mostra o 0xCF como espaço. Com o `apply`, a `FONT12`, os 102 objetivos, os títulos da
  galeria e os 34 overlays foram regravados com o 0xDF. Nenhuma tradução minha ficou com 0xCF. O mod continua
  na 1.4 (ainda não lançada).

**Verificação:** não achei como chegar no jogo a um vídeo com legenda do CyberLife (o da abertura usa outra
legenda, em japonês). Então os bytes da legenda do Toramaru (`a3m05c.dat`) foram postos no título da galeria,
que passa pelo mesmo leitor e pela mesma fonte: aparece "Me chamo Utsonomiya Toramaru!", com os espaços. O
objetivo do quadro 14000 da sessão da história e o título da galeria saem idênticos aos de antes da troca.
Testes Python: 37, todos passando. `text.py check` e `mod.py check`: ok.

## 2026-10-24 — Imagem cortada depois de fechar o menu do Esc (regressão dos sliders)

- Pedido: fora do menu do Esc, a imagem do jogo ficava maior que a janela, como se fosse preciso aumentá-la;
  dentro do menu, tudo cabia.
- **Causa:** o cache da apresentação lógica, que pus no `apply_presentation` para os sliders não piscarem. O
  `draw_paused_frame` desliga a apresentação lógica para desenhar o fundo do menu, e ao fechar o menu o
  `apply_presentation()` achava que nada tinha mudado e não a religava. O jogo ficava sem a escala.
- **Correção:** o `draw_paused_frame` invalida o cache (`g_host.presentation[2] = -1`) quando a desliga.

**Verificação:** Linux e Windows (MinGW) compilam. Não testado no jogo: o roteiro de entrada sem janela não
abre o menu.

## 2026-10-24 — Contorno preto nos efeitos translúcidos das técnicas (edge marking)

- Pedido: no Dragon Crash, o jato d'água aparecia com um traçado preto nas bordas que o emulador não mostra.
- **Causa:** o polígono translúcido gravava o próprio ID no atributo do pixel, por cima do ID do opaco. O
  edge marking via então "IDs diferentes" nas bordas do efeito e pintava a cor de contorno ali. No hardware
  (e no melonDS) o translúcido guarda o ID dele à parte, só para não se desenhar duas vezes no mesmo pixel.
  O edge marking e as sombras comparam só o ID do opaco.
- **Correção (`gpu3d.cpp`):** o ID no `g_flags` passa a ser só o do último opaco. O ID translúcido foi para um
  plano novo, `g_translucent_id`, lido só onde há `kTranslucent` (como o `g_coverage`, não precisa limpar).

**Verificação:** Linux e Windows (MinGW) compilam. Não testado no jogo: falta um save numa partida com a
técnica.

**Pasta de teste do Windows** (`C:\Games\ie3-teste\ie3ogre-recomp`): a pedido, apaguei tudo menos `mods/` e
`saves/` (inclusive `boej.ini`, `boej.pak`, `cache/`, `toolchain/` e os replays) e pus a release 0.1.10 gerada
deste código (`tools/release.py`, já com a correção do edge marking). O launcher vai baixar o toolchain de novo,
refazer o `boej.pak` a partir da ROM e recompilar o jogo.

## 2026-10-25 — CyberLifePlus 1.5: o que sobrou nas outras tabelas

- Pedido: seguir com o próximo item da fase 2 da tradução ("O que sobrou nas outras tabelas").
- **Quase tudo era sobra.** Dos 1.268 caracteres que o `survey` contava em "outras tabelas", ~1.200 eram o fim de
  strings originais depois do NUL de uma tradução mais curta (373 sequências, 234 só no `games.STR`: "Marque um
  gol!\0ばえ！"), que o jogo nunca mostra. Mais 7 eram nomes de técnica em japonês que o CyberLife deixou no
  `command.STR` ao reorganizá-lo e que nenhum registro do `command.dat` aponta.
- **`survey`:** uma sequência do mod que começa no meio de uma string original não conta mais (`leftover`; o
  critério não exige bytes iguais ao original porque 11 sobras do `games.STR` têm o "！" trocado por "!" por
  uma tradução antiga mais longa). No `command.STR` só contam as strings apontadas pelo `command.dat`
  (`shown`). Com isso caíram também os nomes de jogadores (2.135 → 1.947) e os perfis (60.390 → 60.378). A 1.4
  medida assim dá 35,4 % e 48,4 %.
- **O que faltava de verdade, e os formatos:**
  - `command.STR`: as descrições das 4 técnicas que mudam o elemento (registros 78–81 do `command.dat`). As
    outras 383 descrições estão em inglês. O `ov109` lê a descrição do item (+0x2A), da técnica (+0x1A) e da
    tática (+0x0E) no mesmo buffer (0x80 bytes de índice × 32), então vão em texto comum, como as dos itens.
  - `gamerule.dat`: 64 registros de 0x120 bytes (carregados inteiros por `func_0206f950`), com o objetivo da
    minipartida em +0x20. 10 objetivos, 4 em japonês e 6 em inglês, em largura cheia com "0xCF + letra" no
    lugar do espaço, a forma que o CyberLife usava para o leitor de furigana. Gravei todos em pares (`PAIRED`),
    que funciona nos dois leitores.
  - `PracticeGame0F`–`9F.dat` (o mapa dos jogos-treino): 34 registros de 0x4A bytes, sem cabeçalho; posição no
    mapa, e dois nomes de 22 bytes em +0x0C e +0x26, cada um depois de um id (+0x0A, +0x24). São times
    adversários e prêmios: os ids batem com o `item.dat` (78 = chave do ginásio, 894 = Circle Play Drive, 214 =
    Heaven Drive…) e com jogadores (9 = Kuusuke Matsuno, record 0x68 do `unitbase.dat`). O CyberLife traduziu
    o primeiro nome (em inglês) e deixou o segundo em japonês. Os nomes vieram do que o mod já tem no
    `item.dat`/`unitbase.dat`; "なし" virou "Nenhum" nos dois campos.
  - `premiumcondition_hurri.dat`: 200 registros de 0x40 bytes, condição do jogador premium com o time em +0x04.
    O `ov089` lê este ou o `premiumcondition.dat` (traduzido pelo CyberLife) conforme o byte 0x020A0640+0x89.
    76 times: Raimon, Imperial, Ogre e Berryz (como "PistonBerryz" no `team.pkb`).
  - `BinderData_O.dat` (lido pelo `ov052`): registros de 0x1C bytes, nome em +0x04; faltava o Kasamino.
  - `ScoutSpCode.dat` (`ov131`): id do jogador + palavra que o jogador digita. Senhas, ficam em japonês.
- **`text.py`:** lugares novos `command.STR:deslocamento` (descrição, onde o mod a tem, com o espaço até a string
  seguinte e as regras das descrições originais: 2 linhas) e as tabelas de registros (`RECORDS`:
  `arquivo:deslocamento do campo`, a string no campo dela, a largura medida em todos os campos das tabelas do
  mesmo tipo). O `apply` agora ajusta a fonte para qualquer tabela em pares (a condição antiga só pegava a
  galeria, não os overlays nem o `gamerule`; a fonte já estava ajustada desde a 1.3).
- **Traduzido:** `gamerule.json` (10), `PracticeGame.json` (60), `premiumcondition_hurri.json` (76),
  `BinderData_O.json` (1) e 4 descrições em `command.json`. Mod na versão 1.5.

**Verificação:** `text.py check`: 3.932 linhas ok. Byte a byte, o `apply` só mudou os campos pretendidos (as
descrições terminam antes da string seguinte; os nomes ficam nos 22 bytes do campo). Testes Python: 39, todos
passando (novos: sobras e strings não usadas fora da conta, lugares das tabelas de registros). `mod.py check`:
ok. "Outras tabelas" no `survey`: 1.268 → 4 (a senha); total 35,4 % e 48,4 %. Nada disso foi visto no jogo:
falta um save num jogo-treino ou numa minipartida, e não se sabe se os nomes do mapa e das condições premium
aparecem (ficaram no checklist da fase 5).

## 2026-10-25 — Nomes de time das tabelas de registros, do inglês para o português (ainda 1.5)

- Pedido: seguir com o próximo item da fase 2 (o que acrescentei na etapa anterior: os nomes em inglês do mapa
  dos jogos-treino, do fichário e das condições premium).
- **`premiumcondition.dat`** entrou nas tabelas de registros: 200 registros de 0x3C bytes (o `_hurri` tem 0x40),
  time em +0x04. Antes eu tinha medido com 0x40, e os "nomes" pareciam cortados.
- **Regra dos nomes** (glossário do `TRADUCAO.md`): conferi no diálogo (`text.py lookup`) como cada time aparece.
  O diálogo deixa quase todos com o nome próprio (Senbayama, Epsilon, Gemini Storm, Dark Emperors) e traduz uns
  poucos (Novo Japão, Cavaleiros da Rainha, O Reino, Império, Time K, Time Garshield, Anjos Caídos, Leões do
  Deserto, Mensageiros do Céu, Exército Infernal Z, Instituto Alien). Fiquei com: katakana do inglês em inglês,
  nome japonês em romaji, os da dublagem como no diálogo, e as descrições do patch em inglês em português
  (代表 = Seleção, 選抜 = Combinado, ベンチーズ = Reservas, キャッピキャピ = Patricinhas, ○メガネ○ = Quatro Olhos). Corrigi
  erros do CyberLife no caminho ("Diamon dust", "Trus Pirates", "NAtional", "Osaka Gals Kai" num registro
  que é o Osaka Gals, "Team Zoolan" no lugar do Time Garshield, "Absolute Cero" para 絶・世路死苦 = Zetsu Yoroshiku).
- **Prêmios do mapa:** nos registros de tipo 2 (byte baixo do primeiro u16), o nome vem do `item.dat` do mod pelo
  id em +0x0A, para não divergir dos itens.
- **Largura:** não achei no `ov047` (a tela do mapa, que também lê o `PracticeGameCursor.dat`) onde o nome do
  registro seria desenhado; os campos são lidos por endereços calculados. Mantive a regra do `check` (a largura
  do nome japonês mais largo: 93 px no mapa, 90 no fichário) e abreviei o que passava ("Cav. da Rainha", "Peq.
  Gigantes", "Comb. Grupo A", "Perfect Zone P.", "Chave do laborat.", "Mensag. do Céu"). O fichário tem só 18
  bytes por nome, por isso "Exérc. Infernal Z".
- Traduzido: 162 nomes no `PracticeGame.json`, 71 no `BinderData_O.json` (69 + "Reservado" nos registros
  vazios) e 200 no novo `premiumcondition.json`. O mod continua na 1.5 (ainda não lançada).

**Verificação:** `text.py check`: 4.365 linhas ok. Comparado com o mod antes desta etapa, nenhum byte mudou fora
dos campos de nome. Testes Python: 39, todos passando. `mod.py check`: ok. O `survey` não muda (ele só conta
japonês). Não visto no jogo. Ficou de fora "ＬＥＶＥＬ－ＧＯ", que o CyberLife escreveu "Level 5": sem kana nem kanji, o
`text.py` não cria lugar para ele (no checklist).

## 2026-10-25 — Tabelas do inglês para o português, e as descrições que nunca apareceram no jogo (ainda 1.5)

- Pedido: seguir com o próximo item da fase 2 ("do inglês para o português": `sp_binder.STR`, descrições das
  técnicas, `ClearCondition.dat`, `OpenCondition.dat`, `games.STR`).
- **LEVEL-GO:** as tabelas de registros aceitam também nomes só com letras de largura cheia (antes só os com
  kana/kanji); "ＬＥＶＥＬ－ＧＯ" estava "Level 5" no mapa dos jogos-treino.
- **`games.STR` não é lido pelo jogo.** São 262 objetivos de batalha (205 "Score the first goal!"), mas nenhum
  overlay nem o ARM9 tem o caminho dele. Para saber de onde vem o objetivo da batalha, repeti a sessão `bug3`
  com o mod (quadro 35300, a batalha contra os que importunavam o Toramaru) trocando, numa cópia do mod, os
  textos do `games.STR` por "GAMES ação" e os do `gamerule.dat` por "RULE ação": aparece "RULE ação", em pares e
  com acento. Isso confirma no jogo a forma escolhida para o `gamerule.dat`. As anotações de desenvolvedor do
  `games.STR` ("…2010/4/8 j_kuramoto") passaram a contar como debug (fora da largura e do levantamento).
- **Descrições das técnicas (`command.STR`):** 343 seguem a fórmula dos cadernos e saíram iguais à primeira linha
  do caderno da mesma técnica no `item.json` (3 pela mesma regra); as 27 habilidades usam a frase da descrição
  do item; 10 que citam outra técnica viraram "Chute com Fire Tornado" etc.
- **Fichário de técnicas (`sp_binder`):** o `sp_binder.dat` (2 bytes de contagem, 378 registros de 12 bytes)
  aponta cada descrição pelo deslocamento em bytes (+10) no `sp_binder.STR`, com as strings coladas. O CyberLife
  só mudou esses deslocamentos. O `ov112` copia a descrição para 0x100 bytes e a desenha com `0x020F2184`, o
  leitor de furigana da galeria, que descarta ASCII: por isso o CyberLife escreveu tudo em largura cheia. Lugar
  novo `sp_binder.dat:registro`, em pares; o `apply` refaz o `.STR` e os deslocamentos (`put_binder`). As 378
  descrições foram escritas à mão a partir do japonês.
- **O bug da 1.2:** o `ov109`/`ov110` (telas de itens e técnicas) também chamam `0x020F2184`. Para ver no jogo,
  repeti a sessão `bug4` até o quadro 44000 (no campo), salvei um savestate e abri menu → Bag → Técnicas com
  entradas roteirizadas: a descrição do Seigi no Tekken aparecia como **"éc"**, só o acento com a letra colada
  nele. A janela de descrição usa o leitor de furigana, e as descrições em texto comum (itens e táticas desde a
  1.2, técnicas desta etapa) perdiam todo o ASCII. Com as descrições em pares, a mesma tela mostra "Vento TP 37
  1 jog. Soco 2 / Técnica de goleiro", sem pixel entre as letras.
- **Correção no `text.py`:** as descrições de `item.STR`, `command.STR` e `tacticscmd.STR` (índice em 0x0E do
  `tacticscmd.dat`, registros de 0x14) vão em pares. Em pares elas ocupam o dobro, então a que não cabe no
  lugar vai para o fim do `.STR` e os registros que apontam para ela mudam (`put_description`; o jogo lê 128
  bytes do arquivo, e o CyberLife já tinha feito o `command.STR` crescer). O lugar de uma descrição passa a ser
  o deslocamento no original (o `command.json` foi convertido) e o texto atual é lido onde o `.dat` do mod
  aponta, para que um segundo `apply` não mova de novo. A largura das descrições de técnica é a da janela das
  descrições de itens (200 px). Sete descrições de itens foram encurtadas para caber em 127 bytes.
  `item.STR` 79 → 129 KiB, `command.STR` 47 → 68 KiB, `tacticscmd.STR` 5,5 → 7,3 KiB.
- **`ClearCondition.dat`/`OpenCondition.dat`** (lidos pelo `ov104`, que também usa o leitor de furigana): 256
  registros de 0x51 bytes com o texto em +1 (80 bytes). Em pares cabem ~39 letras: "Vença com time de
  Fogo!", "Vença!\nNa Ilha Hidra!", "Ilha do Lince?\nPrecisa de um item!". Corrigi uma troca do CyberLife
  ("With only men!" para 女の子のキャラで勝て！, que pede só garotas). Lugares conforme o diálogo (Colina Oculta,
  Monte Magnitude, Ilha Hidra, Estádio Ogre).

**Verificação:** `text.py check`: 5.197 linhas ok; um segundo `apply` não muda nada. Testes Python: 41, todos
passando (novos: descrição que muda para o fim uma vez só, `sp_binder` refeito). `mod.py check`: ok. No jogo:
objetivo da batalha e descrição de técnica (acima). Não vistos: descrições de itens e táticas (a bolsa da sessão
está vazia, mas é a mesma janela e o mesmo leitor), o fichário (`ov112`) e as condições das rotas (`ov104`).
O teste deixou o savestate da pasta `build/cmake-rel/game/savestates/` (slot 1) no quadro 44000 da sessão `bug4`.

## 2026-10-25 — CyberLifePlus 1.6: imagens da galeria e o aviso de saves (`tools/image.py`)

- Pedido: seguir com a próxima etapa da tradução. Pelo checklist, o menu da galeria em inglês ("ver primeiro
  se é imagem ou texto") e o aviso de saves alterados.
- **É imagem.** Nenhuma das strings ("Gallery", "Game Video", "Choose the video…") existe como texto em
  overlays, tabelas ou scripts, nem em largura cheia ou em pares. Rodando o jogo sem janela com o save do teste
  (Continuar → botão de baixo), o log mostrou o `ov028` (que carrega `pic3d/option`) e depois o `ov037`
  (`menu_MovieView`) e o `ov025` (`password` + `pic2d/menu/pwup_bg00.pac_`). O botão "Gallery" é o
  `SFDN_B02/B03` do `pic2d/menu/MMSlot.SPF_` (no original "オプション"); o aviso, `pic2d/title/atdn_03/04.pac_`.
- **Formatos** (`tools/ntr/image.py`, documentados no cabeçalho):
  - SFP: "SFP\0", tamanho em 0x10, entradas (nome, tamanho, deslocamento / 0x20) a partir de 0x20; os dados
    num segundo bloco SFP com as entradas a partir de 0x400. pic3d separa índice (`.SPL`) e dados (`.SPD`);
    pic2d junta os dois num `.SPF_` comprimido (os dados logo depois do índice, cujo tamanho é o 0x10), menos o
    `MASTutorial.SPF_`, sem compressão.
  - PAC: contagem, (deslocamento, tamanho) de cada seção, e o fim − 0x20; seções alinhadas em 0x20. 3D (`.PAC_`,
    LZ10): textura linear de 4 bpp, paleta, e parâmetros (contagem de quadros, log2 da largura e da altura − 3,
    e (x, y, w, h) de cada quadro). 2D: paleta, mapa de células 8×8 (tile, espelhos, banco de paleta) e tiles de
    4 bpp (8 bpp com paleta de 256). O mapa não guarda a largura: `guess_width` escolhe a que tem menos
    trocas de cor nas emendas (acertou em todos os menus vistos).
  - Ida e volta (ler → gravar → ler) igual em 1.700 imagens do mod. Ficam de fora as texturas de 1, 2 e 8 bpp
    (~210, quase todas dos mapas `gmapNN`) e os pacotes de imagens com nome em hash (`pic3d/common.pkb`: 16 bytes
    por entrada, hash, deslocamento, tamanho e o cabeçalho LZ10).
- **`tools/image.py`:** `export` (PNG indexado de cada imagem, só em `build/`), `check` (com `--png` para a
  prévia) e `apply`. As edições ficam em `translations/CyberLifePlus/images.json` (arquivo → entrada → lista)
  e partem da imagem retail; o arquivo do mod só troca as entradas editadas e é refeito em ordem (o tamanho dos
  `.SPD`/`.SPF_` muda). `erase` apaga o texto (cores de tinta trocadas pelo pixel não-tinta mais perto na linha,
  linhas com a cor de uma coluna limpa, ou `under`: o pixel de outra imagem com o mesmo fundo — as duas telas do
  aviso têm o mesmo fundo com os textos em lugares diferentes, e uma limpa a outra sem estragar a bola e a
  silhueta). `text` desenha com os glifos da `FONT12` do mod (CGLP de 1 bpp, 11×12), com sombra à direita e
  abaixo ou contorno, nas cores dos rótulos originais; `frames` repete nos quadros (normal, selecionado,
  apagado). O `text.py` pula o `images.json`.
- **Traduzido do japonês** (o CyberLife tinha trocado sentidos): オプション → "Extras"; アニメーション → "Vídeos";
  ケータイ連動 → "Ligação com celular"; パスワード発行 → "Gerar senha" (era "Web Identifiant"); 劇場版イナズマイレブン連動
  → "Ligação com o filme" (era "Wifi secret code"); コレクション → "Coleção"; おたのしみ → "Surpresa"; みる/もどる →
  "Ver"/"Sair" (nos botões B de 48 px "Voltar" não cabe com sombra; nos de 96 px ficou "Voltar"); けってい →
  "Confirmar"; キャラクター A/B → "Personagem A/B"; ひでんしょ → "Caderno de técnica"; はい/いいえ → Sim/Não; o painel
  da senha, "Esta é a sua senha.", "Escolha o que você quer.", "Responda à pergunta da tela de cima!" e os
  dois avisos de dados alterados. 25 imagens em 8 arquivos. Mod na 1.6.
- **Achado:** muito mais texto em imagem do que o checklist dizia, quase tudo em inglês do CyberLife: mensagens
  de erro do menu de slots, a coleção, os menus de comunicação, os letreiros da partida (Half Time, Full Time…).
  Ficaram listados no checklist do `TRADUCAO.md`.

**Verificação:** no jogo (Linux, `build/cmake-rel`, sem janela, save do teste do Windows, mod `clp-dev`):
botão "Extras", a lista, a tela dos vídeos ("Escolha o vídeo que quer ver.", Ver, Sair), o aviso ao carregar e
a tela de gerar senha (painel, "Avançar", "Gerar a senha?" com Sim/Não, "Esta é a sua senha." e OK) aparecem
em português, sem resto do texto antigo. Não vistos: a ligação com o filme e a com celular ("???" nesse save).
Nos arquivos, só as entradas editadas mudaram (comparado com o mod do HEAD), e um segundo `apply` não muda nada.
Testes Python: 49, todos passando (novos: PAC 3D/2D ida e volta, tiles espelhados reaproveitados, largura do
mapa, SFP refeito, quadros e `erase`). `text.py check`, `image.py check` e `mod.py check`: ok.

## 2026-10-27 — CyberLifePlus 1.7: mensagens do menu de slots

- Pedido: seguir com a próxima etapa da tradução. Pelo checklist, as imagens em inglês, começando pelo menu de
  slots (`pic2d/menu/MMSlot.SPF_`).
- **O que estava em inglês.** Comparando o `export` do mod com o `--retail`, o CyberLife mudou 23 das 32
  imagens do arquivo; quase todas já estavam em português (carregando, salvando, "Deseja salvar o
  progresso?", Sim/Não, Aceitar, o card do slot, o "Sair"). Em inglês ficaram cinco: `SFDN_W02` ("Loading
  failed…"), `SFDN_W08` ("Saving failed…"), `SFDN_W09` e os avisos de save corrompido `ATDN_BG00`/`ATDN_BG01`.
- **O `W09` tinha outro sentido.** O CyberLife escreveu "You can play matches via DS wireless communications
  from chapter 2 onwards", mas o original diz セーブが失敗しました。もう一度セーブを行います。 ("a gravação falhou;
  vou gravar de novo"). Traduzi do japonês: "Falha ao salvar. / Tentando salvar de novo." O `ATDN_BG00` do
  original também diz mais que o inglês: o save do slot está corrompido e é para começar pelo 「はじめから」, que
  no mod é o botão "Novo Jogo" do título (`MTSIni.SPF_:STDN_I01`). O `ATDN_BG01` avisa que os dados
  recuperados podem ser antigos.
- **As edições** (`images.json`): nas janelas vermelhas o texto original é branco (15) sem sombra, com
  furigana cinza (13) que passa um pouco da borda de cima; `erase` com `ink: [15, 13]` a partir da coluna 23
  (o ícone da mão fica à esquerda, e no `ATDN` o ícone ocupa as linhas 7–18, por isso dois retângulos) e o
  texto em três linhas de 16 px, centrado, sem sombra como no original. Mod na 1.7.

**Verificação:** `image.py check` sem problemas e a prévia de cada imagem conferida (nada do texto antigo,
nada por cima do ícone); um segundo `apply` não muda o patch; `text.py check` (5.197 linhas), `mod.py check` e
os 49 testes Python: ok. No jogo, o menu de slots continua igual (card, "Extras", "Sair"). As mensagens novas
não foram vistas: estragar as duas cópias de um slot numa cópia do save (bytes 0x200 e 0x8200; o save tem duas
metades de 32 KiB) só faz o slot aparecer vazio, e estragar só a primeira não muda nada (o jogo usa a outra).
As de falha dependem de erro na mídia. Ficou no checklist.

**Próximo:** a coleção (`pic3d/collection`: Trailer, Artworks, Musics, as abas, Play/Stop/Back). Tem texturas
de 2 bpp (`BGMDN_I01`–`I03`) que o `image.py` ainda não lê.

## 2026-10-27 — Coleção em português (ainda 1.7)

- Pedido: seguir com a próxima etapa (a coleção, `pic3d/collection`). A etapa anterior ainda não foi
  commitada, então a coleção entra na mesma versão 1.7 do mod.
- **O que o CyberLife mudou:** 6 das 20 imagens, todas de 4 bpp. As três de 2 bpp (`BGMDN_I01`–`I03`) estão
  como no retail. Traduzi do japonês, não do inglês: プロモーション映像 → "Vídeo promocional" (era "Trailer IE3
  Spark/Bomber"), イラスト → "Ilustrações", BGM → "Músicas", バージョン1/2 → "Versão 1/2" (era "Trailer 1/2"),
  キャラクター → "Personagens", マップ → "Mapas", ひっさつわざ → "Técnicas", シークレットイラスト → "Ilustração
  secreta" (era "Artwork IE GO"), さいせい/ていし → "Tocar"/"Parar", もどる → "Sair". As abas das músicas
  (マップ/しあい/イベントBGM など) viram "Mapas", "Partidas", "Eventos" (no CyberLife, "places/action/story").
- **As edições:** os botões da lista (`CLDN_B01`/`B04`, 160×32 por quadro, 3 quadros por item: escuro, normal e
  claro) têm o fundo igual em cada linha, então o `erase` copia a coluna 33. O texto é branco com sombra 7
  (11 no quadro claro). O botão vermelho (`CLDN_B06`) tem contorno 5 e sombra 7. Para fazer isso, a mesma linha
  é desenhada duas vezes: primeiro com `outline`, depois com `shadow`. O `CLDN_B00` é idêntico ao `OPDN_B00`
  da galeria e usa as mesmas edições ("Sair"). No "Parar" do botão de 48 px, `gap: 0`. Nas abas
  (`BGMDN_BG01`, três faixas, uma para cada aba selecionada), o texto da aba selecionada é branco com
  contorno 5 e o das outras é 5 com sombra 11.
- **Teste no jogo sem janela:** o jogo só fica determinístico com `NTR_RTC` fixo (sem isso, as mesmas
  entradas caíam em telas diferentes). Com `NTR_RTC="2010-07-01 12:00:00"` e o save do teste: START no quadro
  400 (créditos do CyberLife, que esperam um botão), A no 900 (pula o vídeo; START não pula), menu do título no
  ~1000, A no 1150 e menu de slots no 1200. Salvei um savestate no quadro 1500 e, a partir dele,
  `NTR_STATE_LOAD=10` com DOWN no 60 e A no 100 abre a lista do Extras. Mas a Coleção aparece como "???"
  nesse save e não abre. Devolvi o savestate que já estava na pasta.

**Verificação:** `image.py check` sem problemas e a prévia de cada imagem conferida (as primeiras deixaram
restos nas bordas das abas e uma linha apagada debaixo do "Tocar" claro; corrigido). Um segundo `apply` não
muda os patches. `text.py check`, `mod.py check` e os 49 testes: ok. No jogo, não foi vista (bloqueada).

**Próximo:** as imagens de comunicação (`MTSWfDL`, `STSWic`, `MTSWiM`…: Match, Trade, Download…).

## 2026-10-27 — Menu "Conectar" em português (ainda 1.7)

- Pedido: mais uma etapa da tradução. Das imagens de comunicação (92 mudadas pelo CyberLife em 10 arquivos),
  peguei a que o jogador vê primeiro: o menu "Conectar" do título, `pic2d/MTSWiM.SPF_` (12 imagens).
- **Traduzido do japonês**, seguindo os termos que os overlays já usam: たいせん → "Partida", すれちがい果たし状 →
  "Desafio Passing" (os overlays dizem "Mande desafios pelo Passing"; o CyberLife tinha posto "Teamshare
  mode"), トレード → "Troca", ダウンロード → "Download", Wi-Fi せってい → "Configurar Wi-Fi", シークレット →
  "Segredo", もどる → "Sair"; Wi-Fi/ワイヤレス/チームデータ ダウンロード → "Baixar via Wi-Fi", "Baixar via DS sem fio",
  "Baixar dados de time"; 2→3スーパーリンク → "Superlink 2→3", オーガプレミアムリンク → "Link Premium Ogre";
  プレミアムスカウト/アイテム → "Scout Premium"/"Item Premium" (como no `ov089`/`ov090`). As quatro janelas: "Sem 11
  jogadores no time, não dá para jogar partidas.", "Para trocar, o time da história precisa ter 12 jogadores
  ou mais." (o original diz "na parte RPG"), "Esta função ainda não está liberada. Avance mais um pouco na
  história!" e "...não dá para usar esta função.".
- **As edições:** os botões são blocos de 32 linhas, 3 por item (normal, com borda branca e claro); cada linha
  do fundo é de uma cor só, então `erase` com `from` na primeira coluna depois da bola, sem passar do ícone do
  DS à direita. Sombra 10 (`WMD_B01`/`B02_`) ou 9 (`WMDN_B02`, `SLDN`), 12 no estado claro. O "Back" (`WMD_B00`)
  tem 36 células que o `export` arruma como 288×8; com `"width": 6` vira o botão de 48×48 com dois quadros,
  igual ao "Sair" da galeria. A mesma imagem está no `MTSSrd` e recebeu as mesmas edições. Os logotipos
  (`SLDN_T00`/`T01`) ficaram como estão (ver checklist: o T00 tem um "From C3 Corp" do CyberLife).
- **No jogo** (com `NTR_RTC` fixo): START 400, A 900, DOWN 1100 + A 1150 (Conectar), A 1550 (carregar o slot),
  A 1700 (Aceitar), A 1850 (aviso de saves) → menu "Conectar" por volta do quadro 2000. Vistos em português:
  o menu, o submenu Download e, ao escolher Superlink 2→3, a janela "Esta função ainda não está liberada".
  Ainda em inglês nessas telas: as faixas de cima ("Match", "Download from Nintendo WFC", "Superlink") e a tela
  de partida (Host, Join, Organise team…), que são do `MTSWic`/`MTSWfDL`.

**Verificação:** `image.py check` sem problemas e prévias conferidas; um segundo `apply` não muda os patches;
`text.py check`, `mod.py check` e os 49 testes: ok. O savestate da pasta foi devolvido.

**Próximo:** `MTSWic` (tela de partida e a faixa "Match") e as faixas do `MTSWfDL`.

## 2026-10-27 — Pasta `logs/` e sessões rastreáveis

Os arquivos de diagnóstico saíram da raiz do jogo e foram para `logs/`. Cada abertura do jogo agora é uma sessão
com nome de data e hora, com log e gravação próprios e um histórico de todas as sessões.

- **Layout:** `logs/launcher.log` (+ `launcher.old.log`), `logs/historico.txt`, `logs/sessoes/<sessão>.log`,
  `logs/replays/<sessão>.txt/.sav`. Ficam as 20 sessões mais recentes; o histórico nunca é apagado. Nome
  repetido (reiniciar no mesmo segundo) ganha `-2`. O `.old` do jogo deixou de existir.
- **Migração:** `logs_dir` (ntr-common, `files.cpp`), chamado pelo jogo e pelo launcher, cria a pasta e, na
  primeira vez, move para lá o `boej.log`, `boej.old.log`, `boej-replay.*` e `launcher.log` que estavam na raiz.
- **Log:** o prefixo de cada linha agora tem o quadro e a hora (`  1234 14:03:12| `). A hora só é formatada de
  novo quando o segundo muda. Linhas `[sessao]` marcam: início (versão de `sdk/version.txt` ou `dev`, mods em
  uso, save), gravação do save pelo jogo (escritas a menos de 60 quadros umas das outras contam como uma
  gravação), savestates, menu de pausa aberto e retomado, telas conhecidas, e o fim.
- **Histórico:** uma linha de início no primeiro quadro (quando o save e os mods já são conhecidos) e uma de
  fim com a duração, os quadros, o motivo (fechou a janela, saiu pelo menu, reiniciou pelo menu, falha),
  saves do jogo, savestates, segundos com queda de FPS, travamentos e a última tela. Um início sem fim
  significa que o jogo foi fechado à força. Execuções sem janela ou roteirizadas não entram no histórico.
- **Telas:** tabela overlay → nome em `cpu.cpp` (`kScreens`). Por enquanto só o ov028 (Extras); sem nome
  conhecido, a "última tela" mostra os overlays ativos. Tentei mapear título, slots e campo com a sessão sem
  janela, mas com o save de teste o jogo ficou nos créditos do CyberLife em duas de três execuções (a mesma
  entrada às vezes passa, às vezes não). Fica para depois.
- **Launcher:** botão "Abrir a pasta de logs"; as mensagens de erro apontam para `logs/launcher.log`.
- Docs: README, LEIA-ME, DESENVOLVIMENTO, MODS e o comentário do `boej.ini` padrão.

**Verificação:** duas sessões com janela (`NTR_EXIT_FRAME`) geraram dois logs, dois replays e quatro linhas no
histórico; os arquivos antigos da pasta de build foram movidos. Com 22 sessões falsas sobraram 20. Um
`NTR_REPLAY` não entrou no histórico. Com `NTR_STATE_SAVE` apareceram o marco do savestate e o resumo no fim;
o save em `saves/` aparece como caminho relativo. Builds nativo e Windows (jogo e launcher) sem avisos. O
savestate da pasta de build foi devolvido. O botão do launcher não foi testado com janela.

**Próximo:** nomes para mais telas (título, slots, campo, partida) na `kScreens`.
