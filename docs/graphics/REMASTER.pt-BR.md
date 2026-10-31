# Remaster: plano do mod remasterizador

*In English: [`REMASTER.md`](REMASTER.md).*

Checklist para um modo "remaster" opcional: o jogo com cara de versão nova, e não de DS ampliado. Fica separado do
[`GRAPHICS.pt-BR.md`](GRAPHICS.pt-BR.md), que cuida da estrutura (GPU, AA, upscalers) e mantém o visual do DS. Nada daqui foi
feito ainda; o roadmap fica no radar até a estrutura de GPU estar pronta.

**De onde veio:** no teste do FXAA (diário, 2026-10-30) ficou claro que, com o 3D em resolução alta, o que ainda
parece "DS" não são as bordas dos polígonos, e sim as texturas de baixa resolução ampliadas sem filtro (cabelo,
roupa, chão) e o 2D esticado (HUD, retratos, nomes das técnicas). AA e upscalers não mexem nisso.

**Regras:**
- Opcional e desligado por padrão. O visual do DS continua a referência e o que os testes conferem.
- O jogo não percebe: o `g_native` de 256×192, a captura de tela do DS e a VRAM saem iguais com o remaster ligado.
  Tudo acontece só na imagem grande que vai para a janela.
- Funciona na CPU (rasterizador por software, `gpu3d.cpp`, e composição 2D, `gpu2d.cpp`) e na GPU (Fase 2 do
  `GRAPHICS.pt-BR.md`), com o mesmo resultado dentro da tolerância. O que só fizer sentido na GPU fica marcado.
- Duas partes: **recursos do runtime** (filtros, sem dado nenhum, valem para qualquer jogador) e **pacote de
  dados** (texturas e imagens refeitas, distribuído como mod na pasta `mods/`, ligado pela aba Mods).
- Release sem nada do jogo, como hoje: o repositório não guarda texturas extraídas nem refeitas a partir delas.

## Fase 0 — Base

- [ ] Ponto único onde uma textura decodificada vira a textura usada no desenho (`decode_textures` em
  `gpu3d.cpp` hoje; o upload da Fase 2 do `GRAPHICS.pt-BR.md` depois), para filtro e troca entrarem nos dois caminhos
- [ ] Identificar cada textura de forma estável: hash dos texels + paleta + formato + tamanho, independente do
  endereço na VRAM
- [ ] Texturas ampliadas com tamanho próprio (2×, 4×…), sem mudar as coordenadas de textura do jogo
  (amostrar pela coordenada normalizada)
- [ ] Cache das texturas ampliadas por hash, invalidado quando a VRAM de textura ou de paleta mudar
- [ ] O mesmo para o 2D: identificar tiles, sprites (OBJ) e bitmaps por hash, e um caminho para a composição da
  imagem grande (`compose_hi_line`) desenhar camadas 2D em resolução maior do que a nativa
- [ ] `[remaster]` no `boej.ini` (ou `[video]`), no launcher e no menu de pausa, com um interruptor geral

## Fase 1 — Filtros sem dados (runtime)

Melhoram qualquer textura na hora, sem pacote nenhum.

- [ ] `texture_filter = off|bilinear` para o 3D (o DS não filtra): bilinear na amostragem, respeitando
  repeat/flip/clamp e as bordas com alpha 0
- [ ] Filtros de pixel art aplicados uma vez por textura, na CPU, guardados no cache: xBR/xBRZ, ScaleFX ou
  MMPX (escolher pela qualidade nos personagens e pelo custo; ver licenças)
- [ ] O mesmo filtro nas camadas 2D da imagem grande: HUD, retratos, letreiros das técnicas
- [ ] Texto: as fontes do jogo (e as do mod de tradução) ampliadas por filtro, ou fontes vetoriais desenhadas
  na resolução da janela (investigar onde o jogo monta o texto)
- [ ] Mipmaps para as texturas ampliadas, para o chão ao longe não tremer
- [ ] Opcional, só GPU: filtragem anisotrópica

## Fase 2 — Pacotes de texturas (dados)

- [ ] Despejo das texturas usadas (`NTR_TEXTURE_DUMP=pasta`), em PNG com o hash no nome, para quem for refazer
- [ ] Formato do pacote no mod: `textures/<hash>.png` (3D) e `images/<hash>.png` (2D), com tamanho livre
  (múltiplo do original); documentar em `MODS.pt-BR.md` (a linha "Texturas em alta resolução" já está lá)
- [ ] Troca na hora do desenho, pelo hash; texturas sem troca seguem pelos filtros da Fase 1
- [ ] Paletas: a mesma textura com paletas diferentes (uniformes dos times) — trocar por textura+paleta ou
  aplicar a paleta sobre uma textura em índices refeita
- [ ] Texturas animadas e escritas na VRAM a cada quadro (efeitos das técnicas): conferir se o hash fica estável
- [ ] `tools/` com um comando para montar e conferir um pacote (hashes que não existem, tamanhos errados)
- [ ] Carga sem travar o jogo: ler e decodificar os PNG em segundo plano, com cache em disco
- [ ] Pacotes de terceiros (inclusive feitos com IA) só como mods separados, fora da release

## Fase 3 — Visual

- [ ] Cor de 8 bits por canal no 3D (`color_depth = full`, já no `GRAPHICS.pt-BR.md` Fase 2): menos faixas no fog e na
  translucidez
- [ ] Contorno dos personagens proporcional à resolução (`outline_scale`, `GRAPHICS.pt-BR.md` Fase 3)
- [ ] Widescreen no 3D: abrir o campo de visão da câmera em vez de esticar (investigar a matriz de projeção e
  o que o jogo deixa de desenhar fora da tela)
- [ ] Vídeos Mobiclip (abertura, cenas) ampliados por filtro
- [ ] Investigar: a taxa de quadros do 3D nas partidas e se dá para interpolar sem mudar a lógica (provavelmente
  fora do escopo, como o frame generation no `GRAPHICS.pt-BR.md`)

## Fase 4 — Distribuição

- [ ] Perfil "Remaster" no launcher que liga os filtros e mostra se há um pacote de texturas ligado
- [ ] Capturas de comparação DS × remaster nas cenas de referência (campo, partida, técnica, menu)
- [ ] Testes: o `g_native` e a captura de tela do DS idênticos com o remaster ligado e desligado

## Decisões em aberto

- [ ] Qual filtro de pixel art (qualidade × custo na CPU com 3D em 4× ou mais)
- [ ] O remaster é um mod oficial (só os arquivos de configuração, sem texturas) ou só uma opção do runtime
  mais pacotes de terceiros
- [ ] Hash das texturas: qual (o mesmo `hash.cpp` das outras partes?) e o que entra nele
- [ ] Onde o 2D em resolução alta entra sem refazer a composição inteira por pixel grande
