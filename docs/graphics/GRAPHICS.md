# Graphics: roadmap for GPU, AA and upscalers

*Em português: [`GRAPHICS.pt-BR.md`](GRAPHICS.pt-BR.md).*

Checklist for giving the player the option to use the GPU (Vulkan, Direct3D 12, Metal) instead of the software
renderer and, on top of that, better anti-aliasing, TAA and upscalers (FSR, DLSS). The checked items already
exist; the rest is a plan. Each phase delivers something the player already uses, even if the following ones never
ship.

**Rules for every phase:**
- Software stays the default and the reference: it is deterministic, runs without a window and decides what is
  "right".
- The game does not notice the backend: the 256×192 `g_native`, the DS screen capture and the registers come out
  the same.
- Everything optional, in `[video]` in `boej.ini` and in the launcher. If the API does not open (old driver, WSL2
  without a GPU), the game says so in the log and falls back to software.
- A release with nothing from the game, as today: shaders and SDKs come through `sdk/` or precompiled, if the
  license allows it.

## Already there

- [x] Software 3D (`runtime/src/gpu3d.cpp`): the geometry engine builds the polygon list; on `SWAP_BUFFERS` it is
  drawn at the start of the next frame, as on the hardware
- [x] Banded rasterizer, up to 16 threads (`BandPool`, `NTR_3D_THREADS`), the same image with any number
- [x] Z/W-buffer, the seven texture formats (cache in `decode_textures`), modulate/decal/toon/highlight/shadow,
  translucency, alpha test, polygon ID, edge marking, fog and the DS's AA
- [x] Internal resolution from 1× to 10× (`resolution_3d`, `NTR_3D_SCALE`, `kMax3dScale`); the game reads
  `g_native` and the window uses the large frame (`gpu3d_hi_row_color/alpha`)
- [x] 2D + 3D composition on the CPU (`runtime/src/gpu2d.cpp`), 2D layers stretched
- [x] Presentation through SDL_Renderer (`host.cpp`: `SDL_UpdateTexture` + `SDL_RenderTexture`), `nearest`/`linear`
  filter, ImGui through `imgui_impl_sdlrenderer3`

Today's limits: above 4× the 3D asks for a strong processor; 5-bit colour per channel (RGB555); uploading and
composing large frames costs memory bandwidth.

## Phase 0 — Presentation on the GPU (SDL_GPU)

The base for the rest. SDL_GPU comes with SDL3 and covers Vulkan, D3D12 and Metal with one code path. Since SDL
3.4, SDL_Renderer itself runs on top of it (`SDL_CreateGPURenderer`). So drawing the screens, the notices and ImGui
(`imgui_impl_sdlrenderer3`) stay the same, and the device is reachable (`SDL_GetGPURendererDevice`) for the next
phases. This replaced the `Presenter` interface and `imgui_impl_sdlgpu3`, which were the initial plan.

- [x] `[video] api = auto|d3d11|opengl|vulkan|d3d12` and `NTR_GPU_API`: `auto` is SDL's default renderer
  (Direct3D 11 on Windows, OpenGL on Linux); `d3d11` and `opengl` pin SDL's renderer to that API; `vulkan` and
  `d3d12` are SDL_GPU (marked BETA in the interface)
- [x] Fallback: if the chosen API does not open, the log says why and the game uses the default renderer
- [x] Log of the API in use (`[host] renderer: SDL_GPU (vulkan)`), also shown in the pause menu
- [x] ImGui (pause menu, achievement notices) working over SDL_GPU on D3D12 and Vulkan
- [x] `sharp` filter (sharp-bilinear: integer nearest upscaling into a render target, then linear up to the screen
  size), without a shader, on any API
- [x] `lcd` filter: `sharp` with the DS screen grid multiplied on top (one cell per DS pixel)
- [x] Option in the launcher and the pause menu (Video → Graphics API, Upscaling filter); the API applies when the
  game opens
- [x] **Done when:** window captures come out the same with the default renderer and with SDL_GPU. On Windows,
  with `nearest` and with `lcd`, D3D11, SDL_GPU/D3D12 and SDL_GPU/Vulkan gave pixel-identical images. WSL2 has no
  Vulkan driver and the fallback to OpenGL worked
- [ ] Metal: only if there is ever a macOS build
- [ ] Decide whether `auto` should prefer SDL_GPU, after more players test `vulkan` and `d3d12`

## Phase 1 — Post-processing over the software image

Full-screen passes over the screens, without touching the rasterizer. They need neither depth nor motion vectors.

- [x] 3D mask in the frame's alpha byte (`gpu2d.cpp`: 0xFF where the 3D is the top layer, 0 elsewhere), so the
  passes do not blur the HUD and the 2D text. In this game part of the HUD is 3D (the field backpack) and is in
  the mask
- [x] FXAA (`runtime/shaders/fxaa.frag.hlsl`), one pass per screen at the frame size, before the upscaling filter.
  Vulkan and D3D12 give the same image (0.001% of the pixels differ, from rounding)
- [ ] Decide whether FXAA stays: the game's textures are pixel art, and it takes the texel steps for aliasing and
  softens the characters. Even with subpixel 0.25 and threshold 0.166 the loss shows
- [ ] SMAA 1x
- [x] FSR 1 (EASU + RCAS, `runtime/shaders/easu.frag.hlsl` and `rcas.frag.hlsl`, from AMD's FidelityFX FSR 1, MIT):
  each place where a screen shows is upscaled to the pixels it takes in the window, instead of the filter. Only the
  3D of the mask goes through EASU and RCAS; the 2D goes through sharp bilinear. Places smaller than the frame (the
  small screens of the wide layout with the 3D above 2×) keep the filter. Vulkan and D3D12 give the same image
- [ ] Decide what to do with FSR 1 on the textures: like FXAA, it was made for natural images; on pixel art it
  rounds the blocks (an oil-painting look on faces). It improves the geometry edges
- [ ] Sharpening (CAS) alone
- [x] Own shaders through SDL_Renderer's `SDL_GPURenderState` over SDL_GPU, in HLSL, kept precompiled in
  `runtime/src/shaders_embedded.h` (`tools/shaders.py`), so the player's PC needs no shader compiler. SPIR-V by
  glslc, DXIL by dxc (`~/tools/dxc-*/bin/dxc`). The fragment shader inputs are `TEXCOORD0` (colour) and
  `TEXCOORD1` (uv), as in SDL's vertex shader
- [x] `[video] post_aa = off|fxaa`, in the launcher and the pause menu (Video → Anti-aliasing). With an API other
  than `vulkan`/`d3d12`, the log says the pass stayed off
- [x] `[video] upscaler = off|fsr1`, in the launcher and the pause menu (Video → 3D upscaling); FXAA may come
  first (it keeps the mask in the alpha)
- [ ] `post_aa = smaa`

## Phase 2 — 3D rasterizer on the GPU

The geometry engine stays on the CPU; only rasterization goes to the GPU.

- [ ] `[video] renderer = software|gpu` and `NTR_RENDERER`
- [ ] Polygons and vertices of `SWAP_BUFFERS` sent to the GPU every frame
- [ ] Z and W depth with the `depth_value` formula, with the "equal" test and the DS's tolerance
- [ ] Textures decoded to RGBA8 on upload, reusing the `decode_textures` cache; invalidate when the texture or
  palette VRAM changes
- [ ] Repeat/flip/clamp in the shader
- [ ] Modulate, decal, toon and highlight in the fragment shader (toon table in a uniform)
- [ ] Translucency in the game's submission order (`SWAP_BUFFERS` sort mode), optional depth update
- [ ] Alpha test
- [ ] Polygon ID and flags (opaque, edge, AA) in an integer attachment (R8UI), like the `kEdge`/`kAntialias` planes
- [ ] Shadow polygons through the stencil (the mask with ID 0 marks; the shadow draws where marked and the ID
  differs)
- [ ] Edge marking, fog and the DS's AA as full-screen passes, with the same logic as the software
- [ ] Wireframe (alpha 0) and clear colour/depth
- [ ] Native 256×192 3D back to the CPU by asynchronous readback (one frame late, as on the hardware); the large
  image never comes back
- [ ] 2D + 3D composition on the GPU (2D layers as a native texture, with per-pixel priority and blending)
- [ ] `NTR_GPU_COMPARE=1`: render with both backends and log the difference per frame
- [ ] Reference scenes by savestate (field, match, menus, techniques with effects) within tolerance at 1×
- [ ] Savestate, rewind and fast forward checked
- [ ] DS screen capture (transitions) checked scene by scene with the readback delay
- [ ] Scale above 10× without CPU cost
- [ ] Optional: `color_depth = ds|full` (8 bits per channel, less banding in the fog and the translucency)
- [ ] Optional: `texture_filter = off|bilinear|anisotropic` (the DS does not filter); texture filters, texture
  packs and the rest of the "remaster" look are in [`REMASTER.md`](REMASTER.md)
- [ ] An open path for the mods' high-resolution textures (already in the `README.md` roadmap; plan in
  [`REMASTER.md`](REMASTER.md))

## Phase 3 — Real anti-aliasing

- [ ] MSAA 2×/4×/8× in the GPU rasterizer (`[video] msaa`)
- [ ] Edge marking and fog over the resolved samples (or per sample, if the difference shows)
- [ ] SSAA: render above the window and reduce with a good filter, without the 10× ceiling
- [ ] `outline_scale`: thicken the characters' black outline (edge marking) with the scale, to keep the DS look
  at high resolution
- [ ] The DS's AA can be turned off when there is MSAA

## Phase 4 — TAA and temporal upscalers

All of them need jitter, depth and motion vectors. The hard part is the vectors: the DS sends a new polygon list
every frame, with nothing saying which is which.

- [ ] Subpixel jitter (Halton) in the projection, only in the player's image (`g_native` gets no jitter)
- [ ] Linear depth per pixel (comes from Phase 2)
- [ ] Camera motion vectors: reprojection by the previous frame's projection matrix (solves the scenery)
- [ ] Per-polygon vectors: match polygons between frames by texture, palette, matrix and order in the command
  block, keeping the previous positions (solves the characters)
- [ ] If matching is not enough: a stable ID per model (player, ball) from the game's draw calls
- [ ] Reactive mask for the 2D, the HUD, animated textures, technique particles and the Mobiclip videos
- [ ] Own TAA (no SDK), to validate jitter, vectors and masks
- [ ] FSR 2/3 (AMD FidelityFX SDK, MIT, Vulkan and D3D12, any GPU)
- [ ] DLSS and DLAA (NVIDIA, RTX only), through Streamline or the DLSS SDK
- [ ] XeSS (Intel), optional
- [ ] Native mode (FSR/DLAA with input equal to output) as AA only
- [ ] `[video] upscaler = off|fsr1|taa|fsr|dlss|xess`, `upscaler_quality = native|quality|balanced|performance` and
  `NTR_UPSCALER`
- [ ] Out of scope: frame generation. The logic runs locked at 60 Hz and extra frames would only add latency.

## Open decisions

- [ ] SDL_GPU and the SDKs: FSR 2/3 and DLSS ask for the native device (`VkDevice`, `ID3D12Device`). Phase 0
  already has the `SDL_GPUDevice` (`SDL_GetGPURendererDevice`), but not the native objects underneath. See what
  SDL_GPU exposes by then; if not enough, an own Vulkan backend for Phase 4
- [ ] DLSS license: redistributing NVIDIA's DLL next to the executable, with the release that compiles the game on
  the player's PC
- [ ] Optional SDKs at build time (`NTR_WITH_FSR`, `NTR_WITH_DLSS`); the launcher only shows what was built;
  licenses in `launcher/TERCEIROS.txt` and `licenses/`
- [ ] Two renderers to maintain: a single description of the post passes (edge marking, fog, AA), with the same
  test on both
- [ ] Tests without a window: the automatic ones stay on software; the GPU comparison runs with
  `SDL_VIDEO_DRIVER=offscreen` or on a PC with a GPU
- [ ] WSL2: the GPU depends on the dzn/D3D12 driver; the fallback to software must be reliable
