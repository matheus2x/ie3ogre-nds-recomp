// 2D engines A and B, rendered one scanline at a time as the LCD reaches each line.
//
// Covers text, affine, extended (16-bit map, 256-color and direct-color bitmap) and
// large-bitmap BGs, tiled/bitmap/affine OBJs, windows 0/1/OBJ, color special effects,
// master brightness, VRAM display mode and display capture. Not modeled yet: the 3D
// mosaic and main-memory display mode. BG0 in 3D mode shows the 3D engine's output
// (gpu3d.cpp), blended with the layer below by its per-pixel alpha.
//
// With the 3D engine above native resolution, each line is still rendered and composed
// at 256 pixels (that is what display capture and the game see), and the host gets a
// larger picture of engine A: lines that show 3D are composed again for every sub-row,
// with the large 3D frame and the 2D layers stretched; its other lines are repeated.
// While a line is drawn, the top two 2D layers of each pixel are recorded (HiLine); the
// large picture is composed from them at VBlank, in parallel. A screen without 3D goes
// to the host at native size.
#include <algorithm>
#include <vector>

#include "pool.h"
#include "runtime.h"

namespace ntr {

namespace {

constexpr uint16_t kOpaque = 0x8000;
constexpr int kLayerObj = 4, kLayerBackdrop = 5;

// Color special effects registers of one line.
struct Effects {
    uint32_t effect, first, second, eva, evb, evy;
};

// A line of engine A composed again at the 3D scale: everything but BG0 is fixed per
// native pixel, so it is kept as the top two of the other layers, with their order keys
// (priority * 6 + 0 for OBJ, 1 + n for BGn; the backdrop is last).
struct HiPixel {
    uint16_t color[2];
    uint8_t layer[2];
    uint8_t key[2];
    uint8_t mask;       // window
    uint8_t obj_alpha;
};
struct HiLine {
    bool composed;  // BG0 shows the 3D frame on this line; otherwise the native line is repeated
    bool bg0_on;
    uint8_t bg0_key;
    uint16_t bright;  // MASTER_BRIGHT
    int32_t hofs;     // BG0HOFS
    Effects fx;
    HiPixel px[kScreenWidth];
};
constexpr uint8_t kKeyBackdrop = 0xFF;

struct Engine {
    uint32_t io;    // register block offset in Memory::io
    uint32_t pal;   // BG palette offset in Memory::palette (OBJ palette follows at +0x200)
    uint32_t oam;   // offset in Memory::oam
    VramArea bg, obj, bg_ext, obj_ext;
    int32_t ref_x[2], ref_y[2];  // internal affine reference points of BG2/BG3 (20.8)
    uint32_t fb[kScreenWidth * kScreenHeight];
    std::vector<uint32_t> hi;      // fb at the 3D scale, for the host
    std::vector<HiLine> hi_lines;  // one per line, while the 3D scale is above 1
    bool hi_frame = false;         // a line of this frame shows 3D (hi_lines)
};

Engine g_engines[2] = {
    {0x0000, 0x000, 0x000, VramArea::ABg, VramArea::AObj, VramArea::ABgExtPal, VramArea::AObjExtPal, {}, {}, {}, {}, {}, false},
    {0x1000, 0x400, 0x400, VramArea::BBg, VramArea::BObj, VramArea::BBgExtPal, VramArea::BObjExtPal, {}, {}, {}, {}, {}, false},
};

// Per-line layer buffers. Colors are BGR555 with kOpaque set; 0 is transparent.
struct Line {
    uint16_t bg[4][kScreenWidth];
    uint16_t obj[kScreenWidth];
    uint8_t obj_prio[kScreenWidth];
    uint8_t obj_alpha[kScreenWidth];  // 0 = normal, 1-16 = blend weight (semi-transparent/bitmap)
    bool obj_win[kScreenWidth];
    uint8_t win[kScreenWidth];         // layers and effects enabled by the windows
    uint16_t out[kScreenWidth];        // composed line, before master brightness
    uint8_t alpha3d[kScreenWidth];     // BG0 in 3D mode: 3D alpha (31 = opaque)
    bool bg0_is_3d;
};
Line g_line;

// Display capture runs from line 0 of the frame after it was enabled.
bool g_capturing = false;

uint16_t r16(const Engine& e, uint32_t off) { return memory().io[e.io + off] | memory().io[e.io + off + 1] << 8; }
uint32_t r32(const Engine& e, uint32_t off) { return load32(memory().io.data() + e.io + off); }
uint16_t pal16(uint32_t off) { return memory().palette[off] | memory().palette[off + 1] << 8; }
uint16_t oam16(uint32_t off) { return memory().oam[off] | memory().oam[off + 1] << 8; }

int32_t ref_reg(const Engine& e, uint32_t off) { return int32_t(r32(e, off) << 4) >> 4; }

void latch_ref(Engine& e, int idx, bool y) {
    uint32_t off = 0x28 + idx * 0x10 + (y ? 4 : 0);
    (y ? e.ref_y : e.ref_x)[idx] = ref_reg(e, off);
}

// --- backgrounds ------------------------------------------------------------------

enum class BgKind { None, Text, Affine, Extended, Large };

BgKind bg_kind(uint32_t mode, int bg) {
    static const BgKind T = BgKind::Text, A = BgKind::Affine, E = BgKind::Extended, L = BgKind::Large, N = BgKind::None;
    static const BgKind kTable[8][4] = {
        {T, T, T, T}, {T, T, T, A}, {T, T, A, A}, {T, T, T, E},
        {T, T, A, E}, {T, T, E, E}, {T, N, L, N}, {N, N, N, N},
    };
    return kTable[mode & 7][bg];
}

uint16_t bg_ext_color(const Engine& e, int slot, uint32_t index) {
    return vram_view(e.bg_ext).read16(slot * 0x2000 + index * 2);
}

void render_text_bg(const Engine& e, int bg, uint32_t line, uint16_t* out) {
    const VramView& vram = vram_view(e.bg);
    uint32_t dispcnt = r32(e, 0);
    uint16_t cnt = r16(e, 0x08 + 2 * bg);
    uint32_t hofs = r16(e, 0x10 + 4 * bg) & 0x1FF, vofs = r16(e, 0x12 + 4 * bg) & 0x1FF;
    uint32_t char_base = ((cnt >> 2) & 0xF) * 0x4000, screen_base = ((cnt >> 8) & 0x1F) * 0x800;
    if (e.io == 0) {
        char_base += ((dispcnt >> 24) & 7) * 0x10000;
        screen_base += ((dispcnt >> 27) & 7) * 0x10000;
    }
    uint32_t w = (cnt & 0x4000) ? 512 : 256, h = (cnt & 0x8000) ? 512 : 256;
    bool c256 = cnt & 0x80;
    bool ext = dispcnt & 0x40000000;
    int slot = (bg < 2 && (cnt & 0x2000)) ? bg + 2 : bg;
    uint32_t y = (line + vofs) & (h - 1);

    for (int x = 0; x < kScreenWidth; x++) {
        uint32_t sx = (x + hofs) & (w - 1);
        uint32_t block = (sx >> 8) + (y >> 8) * (w >> 8);
        uint16_t entry = vram.read16(screen_base + block * 0x800 + ((y & 255) >> 3) * 64 + ((sx & 255) >> 3) * 2);
        uint32_t tile = entry & 0x3FF, px = sx & 7, py = y & 7;
        if (entry & 0x400) px = 7 - px;
        if (entry & 0x800) py = 7 - py;
        uint16_t color;
        if (c256) {
            uint32_t idx = vram.read8(char_base + tile * 64 + py * 8 + px);
            if (idx == 0) continue;
            color = ext ? bg_ext_color(e, slot, (entry >> 12) * 256 + idx) : pal16(e.pal + idx * 2);
        } else {
            uint8_t b = vram.read8(char_base + tile * 32 + py * 4 + px / 2);
            uint32_t idx = (px & 1) ? b >> 4 : b & 0xF;
            if (idx == 0) continue;
            color = pal16(e.pal + ((entry >> 12) * 16 + idx) * 2);
        }
        out[x] = color | kOpaque;
    }
}

void render_affine_bg(const Engine& e, int bg, BgKind kind, uint16_t* out) {
    const VramView& vram = vram_view(e.bg);
    uint32_t dispcnt = r32(e, 0);
    uint16_t cnt = r16(e, 0x08 + 2 * bg);
    int idx = bg - 2;
    int32_t pa = int16_t(r16(e, 0x20 + idx * 0x10)), pc = int16_t(r16(e, 0x24 + idx * 0x10));
    int32_t rx = e.ref_x[idx], ry = e.ref_y[idx];
    bool wrap = cnt & 0x2000;
    uint32_t size = cnt >> 14;
    uint32_t char_base = ((cnt >> 2) & 0xF) * 0x4000, screen_base = ((cnt >> 8) & 0x1F) * 0x800;
    if (e.io == 0) {
        char_base += ((dispcnt >> 24) & 7) * 0x10000;
        screen_base += ((dispcnt >> 27) & 7) * 0x10000;
    }

    enum { Map8, Map16, Bitmap256, BitmapDirect, LargeBitmap } fmt;
    uint32_t w, h;
    if (kind == BgKind::Affine) {
        fmt = Map8;
        w = h = 128u << size;
    } else if (kind == BgKind::Large) {
        fmt = LargeBitmap;
        w = size & 1 ? 1024 : 512;
        h = size & 1 ? 512 : 1024;
    } else if (!(cnt & 0x80)) {
        fmt = Map16;
        w = h = 128u << size;
    } else {
        fmt = (cnt & 0x04) ? BitmapDirect : Bitmap256;
        static const uint32_t kW[4] = {128, 256, 512, 512}, kH[4] = {128, 256, 256, 512};
        w = kW[size];
        h = kH[size];
    }
    uint32_t bitmap_base = ((cnt >> 8) & 0x1F) * 0x4000;
    bool ext = dispcnt & 0x40000000;

    for (int x = 0; x < kScreenWidth; x++, rx += pa, ry += pc) {
        int32_t px = rx >> 8, py = ry >> 8;
        if (wrap) {
            px &= w - 1;
            py &= h - 1;
        } else if (px < 0 || py < 0 || uint32_t(px) >= w || uint32_t(py) >= h) {
            continue;
        }
        uint16_t color;
        switch (fmt) {
            case Map8: {
                uint32_t tile = vram.read8(screen_base + (py >> 3) * (w >> 3) + (px >> 3));
                uint32_t i = vram.read8(char_base + tile * 64 + (py & 7) * 8 + (px & 7));
                if (i == 0) continue;
                color = pal16(e.pal + i * 2);
                break;
            }
            case Map16: {
                uint16_t entry = vram.read16(screen_base + ((py >> 3) * (w >> 3) + (px >> 3)) * 2);
                uint32_t tx = px & 7, ty = py & 7;
                if (entry & 0x400) tx = 7 - tx;
                if (entry & 0x800) ty = 7 - ty;
                uint32_t i = vram.read8(char_base + (entry & 0x3FF) * 64 + ty * 8 + tx);
                if (i == 0) continue;
                color = ext ? bg_ext_color(e, bg, (entry >> 12) * 256 + i) : pal16(e.pal + i * 2);
                break;
            }
            case Bitmap256:
            case LargeBitmap: {
                uint32_t base = fmt == LargeBitmap ? 0 : bitmap_base;
                uint32_t i = vram.read8(base + py * w + px);
                if (i == 0) continue;
                color = pal16(e.pal + i * 2);
                break;
            }
            case BitmapDirect:
                color = vram.read16(bitmap_base + (py * w + px) * 2);
                if (!(color & 0x8000)) continue;
                break;
        }
        out[x] = color | kOpaque;
    }
}

// BG0 in 3D mode: the 3D frame, scrolled horizontally by BG0HOFS.
void render_3d_layer(const Engine& e, uint32_t line, Line& L) {
    const uint16_t* color = gpu3d_line_color(line);
    const uint8_t* alpha = gpu3d_line_alpha(line);
    int32_t hofs = int32_t(r16(e, 0x10) << 23) >> 23;
    for (int x = 0; x < kScreenWidth; x++) {
        int32_t sx = x + hofs;
        L.alpha3d[x] = 31;
        if (sx < 0 || sx >= kScreenWidth || alpha[sx] == 0) continue;
        L.bg[0][x] = color[sx] | kOpaque;
        L.alpha3d[x] = alpha[sx];
    }
}

// --- objects ------------------------------------------------------------------------

void render_objs(const Engine& e, uint32_t line, Line& L) {
    uint32_t dispcnt = r32(e, 0);
    const VramView& vram = vram_view(e.obj);
    static const uint8_t kW[3][4] = {{8, 16, 32, 64}, {16, 32, 32, 64}, {8, 8, 16, 32}};
    static const uint8_t kH[3][4] = {{8, 16, 32, 64}, {8, 8, 16, 32}, {16, 32, 32, 64}};
    uint32_t boundary_shift = 5 + ((dispcnt >> 20) & 3);
    if (e.io != 0) boundary_shift = std::min(boundary_shift, 7u);
    bool ext = dispcnt & 0x80000000;

    for (int i = 0; i < 128; i++) {
        uint32_t base = e.oam + i * 8;
        uint16_t a0 = oam16(base), a1 = oam16(base + 2), a2 = oam16(base + 4);
        bool affine = a0 & 0x100;
        if (!affine && (a0 & 0x200)) continue;  // disabled
        uint32_t mode = (a0 >> 10) & 3, shape = a0 >> 14, size = a1 >> 14;
        if (shape == 3) continue;
        int32_t w = kW[shape][size], h = kH[shape][size];
        int32_t bw = w, bh = h;
        if (affine && (a0 & 0x200)) {
            bw *= 2;
            bh *= 2;
        }
        int32_t dy = (line - (a0 & 0xFF)) & 0xFF;
        if (dy >= bh) continue;
        int32_t ox = a1 & 0x1FF;
        if (ox >= 256) ox -= 512;
        uint8_t prio = (a2 >> 10) & 3;
        uint32_t tile = a2 & 0x3FF;
        bool c256 = a0 & 0x2000;

        int32_t pa = 0x100, pb = 0, pc = 0, pd = 0x100;
        if (affine) {
            uint32_t p = e.oam + ((a1 >> 9) & 0x1F) * 32;
            pa = int16_t(oam16(p + 6));
            pb = int16_t(oam16(p + 14));
            pc = int16_t(oam16(p + 22));
            pd = int16_t(oam16(p + 30));
        }
        uint8_t alpha = 0;
        if (mode == 1) alpha = 0xFF;  // semi-transparent: weights come from BLDALPHA
        if (mode == 3) {
            alpha = uint8_t(a2 >> 12);
            if (alpha == 0) continue;
            alpha++;
        }

        for (int32_t bx = 0; bx < bw; bx++) {
            int32_t sx = ox + bx;
            if (sx < 0 || sx >= kScreenWidth) continue;
            int32_t tx, ty;
            if (affine) {
                int32_t cx = bx - bw / 2, cy = dy - bh / 2;
                tx = ((pa * cx + pb * cy) >> 8) + w / 2;
                ty = ((pc * cx + pd * cy) >> 8) + h / 2;
                if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
            } else {
                tx = (a1 & 0x1000) ? w - 1 - bx : bx;
                ty = (a1 & 0x2000) ? h - 1 - dy : dy;
            }

            uint16_t color;
            if (mode == 3) {
                uint32_t off;
                if (dispcnt & 0x40) {
                    off = tile * (128u << ((dispcnt >> 22) & 1)) + (ty * w + tx) * 2;
                } else if (dispcnt & 0x20) {
                    off = (tile & 0x1F) * 0x10 + (tile & 0x3E0) * 0x80 + ty * 512 + tx * 2;
                } else {
                    off = (tile & 0x0F) * 0x10 + (tile & 0x3F0) * 0x80 + ty * 256 + tx * 2;
                }
                color = vram.read16(off);
                if (!(color & 0x8000)) continue;
            } else {
                uint32_t idx;
                if (dispcnt & 0x10) {  // 1D mapping
                    uint32_t cell = (ty >> 3) * (w >> 3) + (tx >> 3);
                    uint32_t at = (tile << boundary_shift) + cell * (c256 ? 64 : 32);
                    idx = c256 ? vram.read8(at + (ty & 7) * 8 + (tx & 7)) : vram.read8(at + (ty & 7) * 4 + (tx & 7) / 2);
                } else if (c256) {
                    uint32_t at = (tile & ~1u) * 32 + (ty >> 3) * 1024 + (tx >> 3) * 64;
                    idx = vram.read8(at + (ty & 7) * 8 + (tx & 7));
                } else {
                    uint32_t at = tile * 32 + (ty >> 3) * 1024 + (tx >> 3) * 32;
                    idx = vram.read8(at + (ty & 7) * 4 + (tx & 7) / 2);
                }
                if (!c256) idx = (tx & 1) ? idx >> 4 : idx & 0xF;
                if (idx == 0) continue;
                if (mode == 2) {
                    L.obj_win[sx] = true;
                    continue;
                }
                if (!c256) color = pal16(e.pal + 0x200 + ((a2 >> 12) * 16 + idx) * 2);
                else if (ext) color = vram_view(e.obj_ext).read16(((a2 >> 12) * 256 + idx) * 2);
                else color = pal16(e.pal + 0x200 + idx * 2);
            }
            if ((L.obj[sx] & kOpaque) && L.obj_prio[sx] <= prio) continue;
            L.obj[sx] = color | kOpaque;
            L.obj_prio[sx] = prio;
            L.obj_alpha[sx] = alpha;
        }
    }
}

// --- windows and composition ----------------------------------------------------------

bool in_range(uint32_t v, uint16_t reg) {
    uint32_t lo = reg >> 8, hi = reg & 0xFF;
    return lo <= hi ? (v >= lo && v < hi) : (v >= lo || v < hi);
}

void compute_windows(const Engine& e, uint32_t line, Line& L) {
    uint32_t dispcnt = r32(e, 0);
    if (!(dispcnt & 0xE000)) {
        std::fill(std::begin(L.win), std::end(L.win), 0x3F);
        return;
    }
    uint16_t winin = r16(e, 0x48), winout = r16(e, 0x4A);
    std::fill(std::begin(L.win), std::end(L.win), uint8_t(winout & 0x3F));
    if ((dispcnt & 0x8000) && (dispcnt & 0x1000)) {
        for (int x = 0; x < kScreenWidth; x++) {
            if (L.obj_win[x]) L.win[x] = (winout >> 8) & 0x3F;
        }
    }
    for (int w = 1; w >= 0; w--) {
        if (!(dispcnt & (0x2000u << w)) || !in_range(line, r16(e, 0x44 + 2 * w))) continue;
        uint16_t h = r16(e, 0x40 + 2 * w);
        for (int x = 0; x < kScreenWidth; x++) {
            if (in_range(x, h)) L.win[x] = (winin >> (8 * w)) & 0x3F;
        }
    }
}

uint16_t blend(uint16_t a, uint16_t b, uint32_t eva, uint32_t evb) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (((a >> s) & 31) * eva + ((b >> s) & 31) * evb) >> 4;
        out |= std::min(c, 31u) << s;
    }
    return out;
}

uint16_t brighten(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31;
        out |= (c + (((31 - c) * evy) >> 4)) << s;
    }
    return out;
}

uint16_t darken(uint16_t a, uint32_t evy) {
    uint16_t out = 0;
    for (int s = 0; s < 15; s += 5) {
        uint32_t c = (a >> s) & 31;
        out |= (c - ((c * evy) >> 4)) << s;
    }
    return out;
}

Effects effects(const Engine& e) {
    uint16_t bldcnt = r16(e, 0x50), bldalpha = r16(e, 0x52);
    return {uint32_t(bldcnt >> 6) & 3,
            bldcnt & 0x3Fu,
            uint32_t(bldcnt >> 8) & 0x3F,
            std::min(bldalpha & 0x1Fu, 16u),
            std::min((bldalpha >> 8) & 0x1Fu, 16u),
            std::min(r16(e, 0x54) & 0x1Fu, 16u)};
}

// The top two layers at native pixel x, without BG0 when skip_bg0 is set.
void top_layers(const Line& L, const bool* bg_on, const uint8_t* bg_prio, uint16_t backdrop, int x, bool skip_bg0,
                uint16_t* color, int* layer, uint8_t* key) {
    uint8_t mask = L.win[x];
    color[0] = color[1] = backdrop;
    layer[0] = layer[1] = kLayerBackdrop;
    key[0] = key[1] = kKeyBackdrop;
    int found = 0;
    for (int p = 0; p < 4 && found < 2; p++) {
        if ((L.obj[x] & kOpaque) && L.obj_prio[x] == p && (mask & 0x10)) {
            color[found] = L.obj[x] & 0x7FFF;
            key[found] = uint8_t(p * 6);
            layer[found++] = kLayerObj;
        }
        for (int bg = skip_bg0 ? 1 : 0; bg < 4 && found < 2; bg++) {
            uint16_t c = L.bg[bg][x];
            if (bg_on[bg] && bg_prio[bg] == p && (c & kOpaque) && (mask & (1u << bg))) {
                color[found] = c & 0x7FFF;
                key[found] = uint8_t(p * 6 + 1 + bg);
                layer[found++] = bg;
            }
        }
    }
}

// Special effects on the top two layers of a pixel.
uint16_t finish_pixel(const Effects& fx, const uint16_t* color, const int* layer, uint8_t mask, bool bg0_is_3d,
                      uint32_t a3d, uint8_t obj_alpha) {
    uint16_t out = color[0];
    bool second_ok = fx.second & (1u << layer[1]);
    if (layer[0] == 0 && bg0_is_3d && a3d < 31 && second_ok) {
        // Translucent 3D pixels blend with the layer below using their own alpha.
        uint32_t a = a3d;
        out = 0;
        for (int s = 0; s < 15; s += 5) {
            uint32_t c = (((color[0] >> s) & 31) * (a + 1) + ((color[1] >> s) & 31) * (31 - a)) >> 5;
            out |= uint16_t(c << s);
        }
    } else if (layer[0] == kLayerObj && obj_alpha && second_ok) {
        uint32_t a = obj_alpha == 0xFF ? fx.eva : obj_alpha;
        uint32_t b = obj_alpha == 0xFF ? fx.evb : 16 - a;
        out = blend(color[0], color[1], a, b);
    } else if ((mask & 0x20) && (fx.first & (1u << layer[0]))) {
        if (fx.effect == 1 && second_ok) out = blend(color[0], color[1], fx.eva, fx.evb);
        else if (fx.effect == 2) out = brighten(color[0], fx.evy);
        else if (fx.effect == 3) out = darken(color[0], fx.evy);
    }
    return out;
}

void bg_priorities(const Engine& e, uint8_t* bg_prio) {
    for (int bg = 0; bg < 4; bg++) bg_prio[bg] = r16(e, 0x08 + 2 * bg) & 3;
}

void compose(const Engine& e, Line& L, const bool* bg_on, uint16_t* out_line) {
    Effects fx = effects(e);
    uint16_t backdrop = pal16(e.pal) & 0x7FFF;
    uint8_t bg_prio[4];
    bg_priorities(e, bg_prio);
    for (int x = 0; x < kScreenWidth; x++) {
        uint16_t color[2];
        int layer[2];
        uint8_t key[2];
        top_layers(L, bg_on, bg_prio, backdrop, x, false, color, layer, key);
        out_line[x] = finish_pixel(fx, color, layer, L.win[x], L.bg0_is_3d, L.alpha3d[x], L.obj_alpha[x]);
    }
}

// --- display capture ------------------------------------------------------------------

void capture_line(uint32_t line, const uint16_t* source_a) {
    Memory& m = memory();
    uint32_t cnt = load32(m.io.data() + 0x64);
    static const uint32_t kW[4] = {128, 256, 256, 256}, kH[4] = {128, 64, 128, 192};
    uint32_t size = (cnt >> 20) & 3;
    if (line >= kH[size]) return;
    uint8_t* dst = vram_lcdc_bank((cnt >> 16) & 3);
    if (!dst) return;
    uint32_t width = kW[size];
    uint32_t src_mode = (cnt >> 29) & 3;
    uint32_t eva = std::min(cnt & 0x1Fu, 16u), evb = std::min((cnt >> 8) & 0x1Fu, 16u);
    const uint8_t* vram_b = vram_lcdc_bank((load32(m.io.data()) >> 18) & 3);
    uint32_t read_off = ((cnt >> 26) & 3) * 0x8000 + line * 512;
    uint32_t write_off = ((cnt >> 18) & 3) * 0x8000 + line * width * 2;
    bool b_is_fifo = cnt & (1u << 25);

    for (uint32_t x = 0; x < width; x++) {
        uint16_t a = source_a[x];
        uint16_t b = 0;
        if (!b_is_fifo && vram_b) {
            uint32_t o = (read_off + x * 2) & 0x1FFFF;
            b = vram_b[o] | vram_b[o + 1] << 8;
        }
        uint16_t out;
        if (src_mode == 0) {
            out = a;
        } else if (src_mode == 1) {
            out = b;
        } else {
            uint32_t aa = (a & kOpaque) ? eva : 0, ab = (b & kOpaque) ? evb : 0;
            out = blend(a, b, aa, ab) | ((aa | ab) ? kOpaque : 0);
        }
        uint32_t o = (write_off + x * 2) & 0x1FFFF;
        dst[o] = uint8_t(out);
        dst[o + 1] = uint8_t(out >> 8);
    }
    if (line + 1 == kH[size]) store32(m.io.data() + 0x64, cnt & ~0x80000000u);
}

// --- output ---------------------------------------------------------------------------

uint32_t to_rgb(uint16_t c) {
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = r << 3 | r >> 2;
    g = g << 3 | g >> 2;
    b = b << 3 | b >> 2;
    return 0xFF000000u | r << 16 | g << 8 | b;
}

// Master brightness, then 24-bit color.
void output_pixels(uint16_t bright, const uint16_t* colors, int count, uint32_t* dst) {
    uint32_t factor = std::min(bright & 0x1Fu, 16u), mode = bright >> 14;
    for (int x = 0; x < count; x++) {
        uint16_t c = colors[x] & 0x7FFF;
        if (mode == 1) c = brighten(c, factor);
        else if (mode == 2) c = darken(c, factor);
        dst[x] = to_rgb(c);
    }
}

void output_line(Engine& e, uint32_t line, const uint16_t* colors) {
    output_pixels(r16(e, 0x6C), colors, kScreenWidth, e.fb + line * kScreenWidth);
}

// What the large picture needs from this line (see HiLine).
void record_hi(Engine& e, uint32_t line, const Line& L, const bool* bg_on, bool composed) {
    if (e.hi_lines.size() != kScreenHeight) e.hi_lines.resize(kScreenHeight);
    HiLine& h = e.hi_lines[line];
    h.composed = composed && L.bg0_is_3d && bg_on[0];
    if (!h.composed) return;
    e.hi_frame = true;
    uint8_t bg_prio[4];
    bg_priorities(e, bg_prio);
    uint16_t backdrop = pal16(e.pal) & 0x7FFF;
    h.bg0_on = bg_on[0];
    h.bg0_key = uint8_t(bg_prio[0] * 6 + 1);
    h.bright = r16(e, 0x6C);
    h.hofs = int32_t(r16(e, 0x10) << 23) >> 23;
    h.fx = effects(e);
    for (int x = 0; x < kScreenWidth; x++) {
        HiPixel& p = h.px[x];
        int layer[2];
        top_layers(L, bg_on, bg_prio, backdrop, x, true, p.color, layer, p.key);
        p.layer[0] = uint8_t(layer[0]);
        p.layer[1] = uint8_t(layer[1]);
        p.mask = L.win[x];
        p.obj_alpha = L.obj_alpha[x];
    }
}

// The rows of one line in the large picture: the 3D sub-rows put between the recorded
// layers, or the native line repeated.
void compose_hi_line(Engine& e, uint32_t line, int scale) {
    int width = kScreenWidth * scale;
    uint32_t* rows = e.hi.data() + size_t(line) * scale * width;
    const HiLine& h = e.hi_lines[line];
    if (!h.composed) {
        const uint32_t* src = e.fb + line * kScreenWidth;
        for (int r = 0; r < scale; r++) {
            uint32_t* dst = rows + size_t(r) * width;
            for (int x = 0; x < width; x++) dst[x] = src[x / scale];
        }
        return;
    }
    int32_t hofs = h.hofs * scale;
    uint16_t out[kScreenWidth * kMax3dScale];
    for (int r = 0; r < scale; r++) {
        const uint16_t* color3d = gpu3d_hi_row_color(line * scale + r);
        const uint8_t* alpha3d = gpu3d_hi_row_alpha(line * scale + r);
        for (int xo = 0; xo < width; xo++) {
            const HiPixel& p = h.px[xo / scale];
            uint16_t color[2] = {p.color[0], p.color[1]};
            int layer[2] = {p.layer[0], p.layer[1]};
            int32_t sx = xo + hofs;
            bool shown = sx >= 0 && sx < width && alpha3d[sx] != 0;
            uint32_t a3d = shown ? alpha3d[sx] : 31;
            if (shown && h.bg0_on && (p.mask & 1)) {
                uint16_t c = color3d[sx] & 0x7FFF;
                if (h.bg0_key < p.key[0]) {
                    color[1] = color[0];
                    layer[1] = layer[0];
                    color[0] = c;
                    layer[0] = 0;
                } else if (h.bg0_key < p.key[1]) {
                    color[1] = c;
                    layer[1] = 0;
                }
            }
            out[xo] = finish_pixel(h.fx, color, layer, p.mask, true, a3d, p.obj_alpha);
        }
        output_pixels(h.bright, out, width, rows + size_t(r) * width);
    }
}

void render_engine_line(Engine& e, int id, uint32_t line) {
    Line& L = g_line;
    uint32_t dispcnt = r32(e, 0);
    uint32_t powcnt = load32(memory().io.data() + 0x304);
    uint32_t display = (dispcnt >> 16) & (id == 0 ? 3 : 1);
    bool powered = (powcnt & 1) && (powcnt & (id == 0 ? 0x2 : 0x200));

    bool bg_on[4] = {};
    bool composed = false;
    if (!powered) {
        std::fill(std::begin(L.out), std::end(L.out), 0);
        output_line(e, line, L.out);
        if (gpu3d_scale() > 1) record_hi(e, line, L, bg_on, false);
        return;
    }
    if (display == 0 || (dispcnt & 0x80)) {  // display off / forced blank: white
        std::fill(std::begin(L.out), std::end(L.out), 0x7FFF);
    } else if (display == 2) {
        const uint8_t* bank = vram_lcdc_bank((dispcnt >> 18) & 3);
        for (int x = 0; x < kScreenWidth; x++) {
            L.out[x] = bank ? bank[(line * kScreenWidth + x) * 2] | bank[(line * kScreenWidth + x) * 2 + 1] << 8 : 0;
        }
    } else if (display == 3) {
        static bool warned = false;
        if (!warned) NTR_LOG("[gpu] main memory display mode is not supported\n");
        warned = true;
        std::fill(std::begin(L.out), std::end(L.out), 0);
    } else {
        composed = true;
        L.bg0_is_3d = id == 0 && (dispcnt & 8);
        for (int bg = 0; bg < 4; bg++) {
            std::fill(std::begin(L.bg[bg]), std::end(L.bg[bg]), 0);
            BgKind kind = bg_kind(dispcnt, bg);
            if (!(dispcnt & (0x100u << bg)) || kind == BgKind::None) continue;
            bg_on[bg] = true;
            if (bg == 0 && id == 0 && (dispcnt & 8)) {
                render_3d_layer(e, line, L);
                continue;
            }
            if (kind == BgKind::Text) render_text_bg(e, bg, line, L.bg[bg]);
            else render_affine_bg(e, bg, kind, L.bg[bg]);
        }
        std::fill(std::begin(L.obj), std::end(L.obj), 0);
        std::fill(std::begin(L.obj_alpha), std::end(L.obj_alpha), 0);
        std::fill(std::begin(L.obj_win), std::end(L.obj_win), false);
        if (dispcnt & 0x1000) render_objs(e, line, L);
        compute_windows(e, line, L);
        compose(e, L, bg_on, L.out);
    }

    if (id == 0 && g_capturing) {
        uint16_t source_a[kScreenWidth];
        bool graphics = !(load32(memory().io.data() + 0x64) & (1u << 24));
        const uint16_t* c3d = gpu3d_line_color(line);
        const uint8_t* a3d = gpu3d_line_alpha(line);
        for (int x = 0; x < kScreenWidth; x++) {
            source_a[x] = graphics ? (L.out[x] | kOpaque) : (c3d[x] | (a3d[x] ? kOpaque : 0));
        }
        capture_line(line, source_a);
    }
    output_line(e, line, L.out);
    if (gpu3d_scale() > 1) record_hi(e, line, L, bg_on, composed);

    // BG2/BG3 reference points advance by (dmx, dmy) every line.
    for (int idx = 0; idx < 2; idx++) {
        e.ref_x[idx] += int16_t(r16(e, 0x22 + idx * 0x10));
        e.ref_y[idx] += int16_t(r16(e, 0x26 + idx * 0x10));
    }
}

}  // namespace

void gpu_init() {
    for (Engine& e : g_engines) {
        for (int idx = 0; idx < 2; idx++) {
            latch_ref(e, idx, false);
            latch_ref(e, idx, true);
        }
    }
}

void gpu_render_line(uint32_t line) {
    if (line == 0) g_capturing = load32(memory().io.data() + 0x64) & 0x80000000;
    for (int id = 0; id < 2; id++) render_engine_line(g_engines[id], id, line);
}

void gpu_vblank() {
    g_capturing = false;
    // The large picture reads the 3D frame these lines showed, before the next one is swapped in.
    int scale = gpu3d_scale();
    for (Engine& e : g_engines) {
        if (!e.hi_frame || scale == 1) {
            e.hi_frame = false;
            continue;
        }
        size_t size = size_t(kScreenWidth) * kScreenHeight * scale * scale;
        if (e.hi.size() != size) e.hi.assign(size, 0);
        BandPool& pool = band_pool();
        pool.run([&](int band) {
            for (int line = band; line < kScreenHeight; line += pool.bands()) compose_hi_line(e, line, scale);
        });
    }
    gpu3d_vblank();
    for (Engine& e : g_engines) {
        for (int idx = 0; idx < 2; idx++) {
            latch_ref(e, idx, false);
            latch_ref(e, idx, true);
        }
    }
    // POWCNT1 bit 15: engine A drives the top screen when set.
    bool a_on_top = load32(memory().io.data() + 0x304) & 0x8000;
    const Engine& a = g_engines[0];
    const Engine& b = g_engines[1];
    const Engine& top = a_on_top ? a : b;
    const Engine& bottom = a_on_top ? b : a;
    auto picture = [&](const Engine& e) { return e.hi_frame ? e.hi.data() : e.fb; };
    auto picture_scale = [&](const Engine& e) { return e.hi_frame ? scale : 1; };
    host_present(picture(top), picture_scale(top), picture(bottom), picture_scale(bottom));
    for (Engine& e : g_engines) e.hi_frame = false;
}

void gpu_io_written(uint32_t addr) {
    uint32_t off = addr & 0xFFF;
    if (off < 0x28 || off >= 0x40 || (off & 0xF) < 8) return;
    latch_ref(g_engines[(addr >> 12) & 1], (off - 0x28) / 0x10, off & 4);
}

}  // namespace ntr
