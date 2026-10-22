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
