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
