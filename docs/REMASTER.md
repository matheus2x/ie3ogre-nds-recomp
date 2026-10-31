# Remaster: plan for the remastering mod

*Em português: [`REMASTER.pt-BR.md`](REMASTER.pt-BR.md).*

Checklist for an optional "remaster" mode: the game looking like a new release, not like an enlarged DS. It is
separate from [`GRAPHICS.md`](GRAPHICS.md), which covers the structure (GPU, AA, upscalers) and keeps the DS look.
Nothing here is done yet; the roadmap stays on the radar until the GPU structure is ready.

**Where it came from:** in the FXAA test (diary, 2026-10-30) it became clear that, with the 3D at high
resolution, what still looks "DS" is not the polygon edges but the low-resolution textures enlarged without
filtering (hair, clothes, floor) and the stretched 2D (HUD, portraits, technique names). AA and upscalers do not
touch that.

**Rules:**
- Optional and off by default. The DS look stays the reference and what the tests check.
- The game does not notice: the 256×192 `g_native`, the DS screen capture and the VRAM come out the same with the
  remaster on. Everything happens only in the large image that goes to the window.
- Works on the CPU (software rasterizer, `gpu3d.cpp`, and 2D composition, `gpu2d.cpp`) and on the GPU (Phase 2 of
  `GRAPHICS.md`), with the same result within tolerance. What only makes sense on the GPU is marked.
- Two parts: **runtime features** (filters, no data at all, valid for any player) and a **data pack** (redone
  textures and images, distributed as a mod in the `mods/` folder, turned on in the Mods tab).
- A release with nothing from the game, as today: the repository keeps no extracted textures nor ones redone from
  them.

## Phase 0 — Base

- [ ] A single point where a decoded texture becomes the texture used for drawing (`decode_textures` in
  `gpu3d.cpp` today; the upload of Phase 2 of `GRAPHICS.md` later), so filtering and replacement enter both paths
- [ ] Identify each texture stably: hash of the texels + palette + format + size, independent of the VRAM address
- [ ] Enlarged textures with their own size (2×, 4×…), without changing the game's texture coordinates (sample by
  the normalized coordinate)
- [ ] Cache of the enlarged textures by hash, invalidated when the texture or palette VRAM changes
- [ ] The same for 2D: identify tiles, sprites (OBJ) and bitmaps by hash, and a path for the large-image
  composition (`compose_hi_line`) to draw 2D layers at a higher resolution than native
- [ ] `[remaster]` in `boej.ini` (or `[video]`), in the launcher and in the pause menu, with a master switch

## Phase 1 — Filters without data (runtime)

They improve any texture on the fly, with no pack at all.

- [ ] `texture_filter = off|bilinear` for the 3D (the DS does not filter): bilinear sampling, respecting
  repeat/flip/clamp and the edges with alpha 0
- [ ] Pixel-art filters applied once per texture, on the CPU, kept in the cache: xBR/xBRZ, ScaleFX or MMPX
  (choose by quality on the characters and by cost; check the licenses)
- [ ] The same filter on the 2D layers of the large image: HUD, portraits, technique banners
- [ ] Text: the game's fonts (and the translation mod's) enlarged by a filter, or vector fonts drawn at the
  window's resolution (investigate where the game builds the text)
- [ ] Mipmaps for the enlarged textures, so the distant floor does not shimmer
- [ ] Optional, GPU only: anisotropic filtering

## Phase 2 — Texture packs (data)

- [ ] Dump of the textures in use (`NTR_TEXTURE_DUMP=folder`), as PNG with the hash in the name, for whoever
  redoes them
- [ ] Pack format in the mod: `textures/<hash>.png` (3D) and `images/<hash>.png` (2D), with free size (a multiple
  of the original); document it in `MODS.md` (the "High-resolution textures" row is already there)
- [ ] Replacement at draw time, by hash; textures without a replacement go through the Phase 1 filters
- [ ] Palettes: the same texture with different palettes (team uniforms) — replace by texture+palette or apply
  the palette over a redone indexed texture
- [ ] Animated textures written to VRAM every frame (technique effects): check that the hash stays stable
- [ ] A command in `tools/` to assemble and check a pack (hashes that do not exist, wrong sizes)
- [ ] Loading without stalling the game: read and decode the PNGs in the background, with a disk cache
- [ ] Third-party packs (including AI-made ones) only as separate mods, outside the release

## Phase 3 — Look

- [ ] 8 bits per channel colour in the 3D (`color_depth = full`, already in `GRAPHICS.md` Phase 2): less banding
  in the fog and the translucency
- [ ] Character outlines proportional to the resolution (`outline_scale`, `GRAPHICS.md` Phase 3)
- [ ] Widescreen in the 3D: widen the camera's field of view instead of stretching (investigate the projection
  matrix and what the game stops drawing off screen)
- [ ] Mobiclip videos (opening, cutscenes) enlarged by a filter
- [ ] Investigate: the 3D frame rate in matches and whether it can be interpolated without changing the logic
  (probably out of scope, like frame generation in `GRAPHICS.md`)

## Phase 4 — Distribution

- [ ] A "Remaster" profile in the launcher that turns the filters on and shows whether a texture pack is on
- [ ] DS × remaster comparison captures in the reference scenes (field, match, technique, menu)
- [ ] Tests: the `g_native` and the DS screen capture identical with the remaster on and off

## Open decisions

- [ ] Which pixel-art filter (quality × CPU cost with the 3D at 4× or more)
- [ ] Whether the remaster is an official mod (only the configuration files, no textures) or only a runtime
  option plus third-party packs
- [ ] Texture hash: which one (the same `hash.cpp` as the other parts?) and what goes into it
- [ ] Where high-resolution 2D fits without redoing the whole composition per large pixel
