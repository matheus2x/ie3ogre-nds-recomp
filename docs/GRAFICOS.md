# Gráficos: roadmap para GPU, AA e upscalers

Checklist para dar ao jogador a opção de usar a GPU (Vulkan, Direct3D 12, Metal) no lugar do renderizador por
software e, em cima disso, anti-aliasing melhor, TAA e upscalers (FSR, DLSS). Os itens marcados já existem; o
resto é plano. Cada fase entrega algo que o jogador já usa, mesmo que as seguintes nunca saiam.

**Regras que valem para todas as fases:**
- O software continua o padrão e a referência: é determinístico, roda sem janela e decide o que é "certo".
- O jogo não percebe o backend: o `g_native` de 256×192, a captura de tela do DS e os registradores saem iguais.
- Tudo opcional, em `[video]` no `boej.ini` e no launcher. Se a API não abrir (driver antigo, WSL2 sem GPU),
  o jogo avisa no log e volta para o software.
- Release sem nada do jogo, como hoje: shaders e SDKs entram pelo `sdk/` ou pré-compilados, se a licença deixar.

## Já existe

- [x] 3D por software (`runtime/src/gpu3d.cpp`): o geometry engine monta a lista de polígonos; no
  `SWAP_BUFFERS` ela é desenhada no começo do quadro seguinte, como no hardware
- [x] Rasterizador em bandas, até 16 threads (`BandPool`, `NTR_3D_THREADS`), imagem igual com qualquer número
- [x] Z/W-buffer, os sete formatos de textura (cache em `decode_textures`), modulate/decal/toon/highlight/shadow,
  translucidez, alpha test, polygon ID, edge marking, fog e o AA do DS
- [x] Resolução interna de 1× a 10× (`resolution_3d`, `NTR_3D_SCALE`, `kMax3dScale`); o jogo lê o `g_native`
  e a janela usa o quadro grande (`gpu3d_hi_row_color/alpha`)
- [x] Composição 2D + 3D na CPU (`runtime/src/gpu2d.cpp`), camadas 2D esticadas
- [x] Apresentação pelo SDL_Renderer (`host.cpp`: `SDL_UpdateTexture` + `SDL_RenderTexture`), filtro
  `nearest`/`linear`, ImGui por `imgui_impl_sdlrenderer3`

Limites de hoje: acima de 4× o 3D pede um processador forte; cor de 5 bits por canal (RGB555); subir e compor
quadros grandes custa banda de memória.

## Fase 0 — Apresentação pela GPU (SDL_GPU)

Base para o resto. A SDL_GPU já vem com o SDL3 e cobre Vulkan, D3D12 e Metal com um só código. Desde o SDL 3.4,
o próprio SDL_Renderer roda por cima dela (`SDL_CreateGPURenderer`). Assim o desenho das telas, os avisos e o ImGui
(`imgui_impl_sdlrenderer3`) continuam iguais, e o device fica acessível (`SDL_GetGPURendererDevice`) para as
próximas fases. Isso substituiu a interface `Presenter` e o `imgui_impl_sdlgpu3`, que eram o plano inicial.

- [x] `[video] api = auto|d3d11|opengl|vulkan|d3d12` e `NTR_GPU_API`: `auto` é o renderizador padrão do SDL
  (Direct3D 11 no Windows, OpenGL no Linux); `d3d11` e `opengl` fixam o renderizador do SDL nessa API; `vulkan` e
  `d3d12` são a SDL_GPU (marcadas como BETA na interface)
- [x] Fallback: se a API escolhida não abrir, o log diz o motivo e o jogo usa o renderizador padrão
- [x] Log da API em uso (`[host] renderer: SDL_GPU (vulkan)`), que também aparece no menu de pausa
- [x] ImGui (menu de pausa, avisos de conquista) funcionando sobre a SDL_GPU em D3D12 e Vulkan
- [x] Filtro `sharp` (sharp-bilinear: ampliação inteira em nearest num render target, depois linear até o
  tamanho da tela), sem shader, em qualquer API
- [x] Filtro `lcd`: `sharp` com a grade da tela do DS multiplicada por cima (uma célula por pixel do DS)
- [x] Opção no launcher e no menu de pausa (Vídeo → API gráfica, Filtro de ampliação); a API vale ao abrir o jogo
- [x] **Pronto quando:** as capturas da janela saem iguais com o renderizador padrão e com a SDL_GPU. No Windows,
  com `nearest` e com `lcd`, D3D11, SDL_GPU/D3D12 e SDL_GPU/Vulkan deram imagens idênticas pixel a pixel. No
  WSL2 não há driver Vulkan e o fallback para OpenGL funcionou
- [ ] Metal: só se um dia houver build para macOS
- [ ] Decidir se `auto` passa a preferir a SDL_GPU, depois de mais jogadores testarem `vulkan` e `d3d12`

## Fase 1 — Pós-processamento sobre a imagem do software

Passes em tela cheia sobre a tela de cima, sem mexer no rasterizador. Não precisam de profundidade nem de
vetores de movimento.

- [ ] Máscara do 3D (`gpu3d_hi_row_alpha`) enviada junto com o quadro, para os passes não borrarem o HUD e o
  texto do 2D
- [ ] FXAA
- [ ] SMAA 1x
- [ ] FSR 1 (EASU + RCAS): 3D em 2× ou 3× ampliado até a janela, em vez de pagar 6× ou 8× na CPU
- [ ] Nitidez (CAS) sozinha
- [ ] Shaders próprios pelo `SDL_GPURenderState` do SDL_Renderer sobre a SDL_GPU, em HLSL, compilados para
  SPIR-V e DXIL (glslc existe aqui; o dxc ainda falta) e guardados pré-compilados, para o PC do jogador não
  precisar de compilador de shader
- [ ] `[video] post_aa = off|fxaa|smaa` e `upscaler = off|fsr1`

## Fase 2 — Rasterizador 3D na GPU

O geometry engine fica na CPU; só a rasterização vai para a GPU.

- [ ] `[video] renderer = software|gpu` e `NTR_RENDERER`
- [ ] Polígonos e vértices do `SWAP_BUFFERS` enviados à GPU a cada quadro
- [ ] Profundidade Z e W na fórmula de `depth_value`, com o teste "igual" e a tolerância do DS
- [ ] Texturas decodificadas para RGBA8 no upload, reaproveitando o cache de `decode_textures`; invalidar
  quando a VRAM de textura ou de paleta mudar
- [ ] Repeat/flip/clamp no shader
- [ ] Modulate, decal, toon e highlight no fragment shader (tabela de toon num uniform)
- [ ] Translucidez na ordem de envio do jogo (modo de ordenação do `SWAP_BUFFERS`), atualização opcional da
  profundidade
- [ ] Alpha test
- [ ] Polygon ID e flags (opaco, borda, AA) num anexo inteiro (R8UI), como os planos `kEdge`/`kAntialias`
- [ ] Shadow polygons pelo stencil (a máscara com ID 0 marca; a sombra desenha onde está marcado e o ID é outro)
- [ ] Edge marking, fog e AA do DS como passes em tela cheia, com a mesma lógica do software
- [ ] Wireframe (alpha 0) e clear color/depth
- [ ] 3D nativo de 256×192 de volta para a CPU por leitura assíncrona (um quadro de atraso, como no hardware);
  a imagem grande nunca volta
- [ ] Composição 2D + 3D na GPU (camadas 2D como textura nativa, com prioridade e blend por pixel)
- [ ] `NTR_GPU_COMPARE=1`: renderizar com os dois backends e logar a diferença por quadro
- [ ] Cenas de referência por savestate (campo, partida, menus, técnicas com efeitos) dentro da tolerância
  em 1×
- [ ] Savestate, rewind e avanço rápido conferidos
- [ ] Captura de tela do DS (transições) conferida cena a cena com o atraso da leitura
- [ ] Escala acima de 10× sem custo de CPU
- [ ] Opcional: `color_depth = ds|full` (8 bits por canal, menos banding no fog e na translucidez)
- [ ] Opcional: `texture_filter = off|bilinear|anisotropic` (o DS não filtra)
- [ ] Caminho aberto para as texturas em alta resolução dos mods (já no roadmap do `README.md`)

## Fase 3 — Anti-aliasing de verdade

- [ ] MSAA 2×/4×/8× no rasterizador de GPU (`[video] msaa`)
- [ ] Edge marking e fog sobre as amostras resolvidas (ou por amostra, se a diferença aparecer)
- [ ] SSAA: renderizar acima da janela e reduzir com um filtro bom, sem o teto de 10×
- [ ] `outline_scale`: engrossar o contorno preto dos personagens (edge marking) conforme a escala, para manter
  o visual do DS em resolução alta
- [ ] AA do DS desligável quando houver MSAA

## Fase 4 — TAA e upscalers temporais

Todos precisam de jitter, profundidade e vetores de movimento. O difícil são os vetores: o DS manda uma lista
nova de polígonos a cada quadro, sem nada que diga qual é qual.

- [ ] Jitter subpixel (Halton) na projeção, só na imagem do jogador (o `g_native` não recebe jitter)
- [ ] Profundidade linear por pixel (vem da Fase 2)
- [ ] Vetores de movimento da câmera: reprojeção pela matriz de projeção do quadro anterior (resolve o cenário)
- [ ] Vetores por polígono: casar polígonos entre quadros por textura, paleta, matriz e ordem no bloco de
  comandos, guardando as posições anteriores (resolve os personagens)
- [ ] Se o casamento não bastar: ID estável por modelo (jogador, bola) a partir das chamadas de desenho do jogo
- [ ] Máscara reativa para o 2D, o HUD, texturas animadas, partículas das técnicas e os vídeos Mobiclip
- [ ] TAA próprio (sem SDK), para validar jitter, vetores e máscaras
- [ ] FSR 2/3 (AMD FidelityFX SDK, MIT, Vulkan e D3D12, qualquer GPU)
- [ ] DLSS e DLAA (NVIDIA, só RTX), via Streamline ou o DLSS SDK
- [ ] XeSS (Intel), opcional
- [ ] Modo nativo (FSR/DLAA com entrada igual à saída) só como AA
- [ ] `[video] upscaler = off|fsr1|taa|fsr|dlss|xess`, `upscaler_quality = native|quality|balanced|performance`
  e `NTR_UPSCALER`
- [ ] Fora do escopo: frame generation. A lógica roda presa em 60 Hz e os quadros extras só aumentariam o
  atraso.

## Decisões em aberto

- [ ] SDL_GPU e os SDKs: FSR 2/3 e DLSS pedem o device nativo (`VkDevice`, `ID3D12Device`). A Fase 0 já tem o
  `SDL_GPUDevice` (`SDL_GetGPURendererDevice`), mas não os objetos nativos por baixo. Ver o que a SDL_GPU expõe
  na época; se não bastar, um backend Vulkan próprio para a Fase 4
- [ ] Licença do DLSS: redistribuir a DLL da NVIDIA ao lado do executável, com a release que compila o jogo no
  PC do jogador
- [ ] SDKs opcionais na compilação (`NTR_WITH_FSR`, `NTR_WITH_DLSS`); o launcher só mostra o que foi compilado;
  licenças em `launcher/TERCEIROS.txt` e `licenses/`
- [ ] Dois renderizadores para manter: uma descrição só dos passes de pós (edge marking, fog, AA), com o mesmo
  teste nos dois
- [ ] Testes sem janela: os automáticos seguem no software; a comparação com a GPU roda com
  `SDL_VIDEO_DRIVER=offscreen` ou num PC com GPU
- [ ] WSL2: a GPU depende do driver dzn/D3D12; o fallback para software precisa ser confiável
