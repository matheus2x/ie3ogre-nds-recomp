// HD text: the game's letters drawn again at the window's resolution
// (docs/modding/TRANSLATION-STATUS.md, phase 3).
//
// The game draws every letter of FONT8, RUBI8 and FONT12 with one of four functions of its
// font class (vtable 0x0208FFEC +0x10..+0x1C: 4 bpp tiled, 4 bpp linear, 8 bpp tiled, 8 bpp
// linear), 1 bit per pixel in one color, into a buffer in RAM or straight into VRAM; FONT12T
// with the four of a subclass (vtable 0x02091290), 2 bits per pixel: value 1 in the text's
// color, 2 and 3 in the colors at +0x1C/+0x1D of the font object (the outline). Menus also
// draw with NitroSystem's NNS_G2dCharCanvasDrawChar (func_02019170), into a canvas of BG
// characters or of 1D OBJs, value v in color cl + v - 1. Those functions are hooked here: every pixel of the letter's box (the columns it draws, all the
// rows of the cell) gets a tag that says which drawn letter it is and which pixel of it. A
// buffer in RAM reaches VRAM by a copy (DMA, MIi_CpuCopy*, memcpy...), and the tags go with
// it; a clear or fill takes them away. The 2D engine reads the tag of each pixel it shows
// (gpu2d.cpp) and checks it against the letter's bitmap (ink where the bitmap has a bit,
// another color where it has none), so a tag left behind on memory the game wrote some other
// way is ignored.
//
// Tags live in pages of 4 KiB of guest bytes, made on the first tag, two per byte (the low
// and the high nibble of 4 bpp; 8 bpp uses the first). The space is main RAM, then the VRAM
// banks (Memory::vram), so a tag stays with the byte however the banks are mapped.
//
// The HD letters come from a TrueType font of a mod (hd/fonts.json, docs/modding/MODS.md),
// drawn once per letter at kSuper samples per native pixel and fitted into the letter of the
// game's font: the height of its ink and centered on it (narrowed when wider than it, so the
// layout of the text stays the game's), at a size near the one that gives the cap height of
// the game's "H" (each letter within kFitRange of it, so the strokes stay alike). A letter the
// mod's font does not have stays as the game drew it.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>

#include "runtime.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "imstb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace ntr {

namespace {

constexpr uint32_t kPageBytes = 0x1000;
constexpr uint32_t kSpace = kMainRamSize + 0x00A4000;  // main RAM, then the VRAM banks
constexpr uint32_t kPages = kSpace / kPageBytes;
constexpr uint32_t kGlyphs = 0x10000;  // letters drawn kept at a time (tag ids 1..0xFFFF)

std::unique_ptr<uint32_t[]> g_pages[kPages];  // 2 tags per byte
TextGlyph g_glyphs[kGlyphs];
uint32_t g_next = 1;
bool g_any = false;

void prepare(const TextGlyph& g, uint32_t cglp);  // the HD letter (below)

// Byte index in the tag space of a guest address, or -1 (not RAM or VRAM).
int64_t space_of(uint32_t addr) {
    const uint8_t* p = g_write_pages[addr >> 12];
    if (!p) return -1;
    p += addr & 0xFFF;
    return text_space_of(p);
}

uint32_t* page(uint32_t space_byte, bool make) {
    std::unique_ptr<uint32_t[]>& pg = g_pages[space_byte / kPageBytes];
    if (!pg && make) {
        pg.reset(new uint32_t[kPageBytes * 2]());
        g_any = true;
    }
    return pg.get();
}

void put(uint32_t addr, int nibble, uint32_t tag) {
    int64_t s = space_of(addr);
    if (s < 0) return;
    page(uint32_t(s), true)[(s % kPageBytes) * 2 + nibble] = tag;
}

// The font objects the game made (func_02029a24 keeps them at 0x02099E8C): which file.
TextFont font_of(uint32_t font) {
    static constexpr struct {
        uint32_t offset;
        TextFont id;
    } kFonts[] = {{0x44, TextFont::Font8}, {0x90, TextFont::Rubi8}, {0xC4, TextFont::Font12}, {0x50, TextFont::Font12T}};
    for (const auto& f : kFonts) {
        if (rd32(0x02099E8C + f.offset) == font) return f.id;
    }
    return TextFont::Other;
}

enum class Layout { Tiled4, Linear4, Tiled8, Linear8 };

// (font, glyph bitmap, buffer, buffer width, color, CWDH entry, rows, x, y): what the draw
// functions take (the last five on the stack).
void tag_letter(Cpu& c, Layout layout, int bpp) {
    const uint32_t font = c.r[0], bitmap = c.r[1], buffer = c.r[2], width = c.r[3], sp = c.r[13];
    const uint8_t color = rd8(sp);
    const uint32_t cwdh = rd32(sp + 4);
    const int rows = int32_t(rd32(sp + 8)), x = int32_t(rd32(sp + 12)), y = int32_t(rd32(sp + 16));
    const int left = int8_t(rd8(cwdh)), columns = int8_t(rd8(cwdh + 2)) - left;
    const uint32_t cglp = rd32(rd32(font + 4) + 8);
    const uint32_t size = rd16(cglp + 2);
    if (columns <= 0 || rows <= 0 || columns > 0x3F || rows > 0xFF || size == 0 || bitmap < cglp + 8) return;

    uint32_t id = g_next;
    g_next = g_next + 1 < kGlyphs ? g_next + 1 : 1;
    TextGlyph& g = g_glyphs[id];
    g.font = font_of(font);
    g.bitmap = bitmap;
    g.index = uint16_t((bitmap - cglp - 8) / size);
    g.cell_w = rd8(cglp);
    g.cell_h = rd8(cglp + 1);
    g.columns = uint8_t(columns);
    g.bpp = uint8_t(bpp);
    g.depth8 = layout == Layout::Tiled8 || layout == Layout::Linear8;
    const uint8_t mask = g.depth8 ? 0xFF : 0x0F;
    g.colors[0] = 0;
    g.colors[1] = color & mask;
    g.colors[2] = rd8(font + 0x1C) & mask;
    g.colors[3] = rd8(font + 0x1D) & mask;
    g.cglp = cglp;
    prepare(g, cglp);

    for (int gy = 0; gy < rows; gy++) {
        const int py = y + gy;
        for (int gx = 0; gx < columns; gx++) {
            const int px = x + left + gx;
            if (px < 0 || py < 0) continue;
            uint32_t off;
            switch (layout) {
                case Layout::Tiled4:
                    off = (width >> 3) * 0x20 * (py >> 3) + (py & 7) * 4 + (px >> 3) * 0x20 + ((px & 7) >> 1);
                    break;
                case Layout::Linear4: off = (width * py + px) / 2; break;
                case Layout::Tiled8: off = (py >> 3) * (width >> 3) * 0x40 + (py & 7) * 8 + (px >> 3) * 0x40 + (px & 7); break;
                default: off = width * py + px; break;
            }
            put(buffer + off, g.depth8 ? 0 : (px & 1), id << 16 | uint32_t(gy) << 8 | uint32_t(gx));
        }
    }
}

// The pixels of a letter: (column, row) of its bitmap -> address and nibble in the buffer.
template <typename Where>
uint32_t tag_pixels(TextGlyph g, int columns, int rows, Where where) {
    uint32_t id = g_next;
    g_next = g_next + 1 < kGlyphs ? g_next + 1 : 1;
    g.columns = uint8_t(columns);
    g_glyphs[id] = g;
    for (int gy = 0; gy < rows; gy++) {
        for (int gx = 0; gx < columns; gx++) {
            uint32_t addr;
            int nibble;
            if (where(gx, gy, &addr, &nibble)) put(addr, nibble, id << 16 | uint32_t(gy) << 8 | uint32_t(gx));
        }
    }
    return id;
}

// NitroSystem's character index of character (x, y) of a canvas of 1D OBJs (func_020184f0):
// the canvas is cut into the largest OBJs that fit, shapes from the table at 0x0208BF20.
uint32_t obj1d_char(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t sw, uint32_t sh) {
    uint32_t base = 0;
    for (int guard = 0; guard < 64; guard++) {
        uint32_t ow = w & (~0u << sw), oh = h & (~0u << sh);
        if (y < oh) {
            if (x < ow) {
                return ow * (y & (~0u << sh)) + base + ((x & (~0u << sw)) << sh) + ((y & ~(~0u << sh)) << sw) +
                       (x & ~(~0u << sw));
            }
            base += ow * oh;
            x -= ow;
            ow = w - ow;
        } else {
            base += w * oh;
            if (x < ow) {
                y -= oh;
                oh = h - oh;
            } else {
                base += ow * (h - oh);
                x -= ow;
                y -= oh;
                ow = w - ow;
                oh = h - oh;
            }
        }
        int iw = int(ow) < 8 ? 31 - __builtin_clz(ow | 1) : 3, ih = int(oh) < 8 ? 31 - __builtin_clz(oh | 1) : 3;
        const uint32_t shapes = 0x0208BF20 + ih * 8 + iw * 2;
        sw = rd8(shapes);
        sh = rd8(shapes + 1);
        w = ow;
        h = oh;
    }
    return 0;
}

// --- the mod's fonts -----------------------------------------------------------------

constexpr int kSuper = 8;  // samples per native pixel, each way
constexpr float kFitRange = 1.35f;  // a letter's size is the font's divided or multiplied by at most this
constexpr int kFontCount = 5;

struct HdGlyph {
    int w = 0, h = 0;            // in samples: the cell of the game's font times kSuper
    std::vector<uint8_t> value;  // 0 none, 1 fill, 2/3 the outline (2 bpp fonts)
};

struct HdFont {
    std::vector<uint8_t> ttf;
    stbtt_fontinfo info{};
    bool ok = false;
    std::vector<uint32_t> codepoints;  // by glyph index of the game's font (0: none)
    uint32_t cglp = 0;                 // of the game's font, as last drawn
    bool fitted = false;
    float scale = 0;    // TrueType units -> native pixels
    std::unique_ptr<std::atomic<HdGlyph*>[]> glyphs{new std::atomic<HdGlyph*>[0x10000]()};
};
std::deque<HdFont> g_loaded;                  // every font of the mods in use (text_hd_load)
std::atomic<HdFont*> g_active[kFontCount] = {};  // the one each font of the game is drawn with (text_hd_use)
const char* const kFontNames[kFontCount] = {"", "FONT8", "RUBI8", "FONT12", "FONT12T"};

int native_value(uint32_t bitmap, int cell_w, int bpp, int x, int y) {
    uint32_t bit = uint32_t(y * cell_w + x) * bpp;
    return (rd8(bitmap + bit / 8) >> (8 - bpp - bit % 8)) & ((1 << bpp) - 1);
}

// The size that gives the cap height of the game's "H" (the first of "HAIO0" the font has).
bool fit(HdFont& f, const TextGlyph& g) {
    f.fitted = true;
    const uint32_t size = rd16(f.cglp + 2);
    for (uint32_t ch : {'H', 'A', 'I', 'O', '0'}) {
        auto it = std::find(f.codepoints.begin(), f.codepoints.end(), ch);
        if (it == f.codepoints.end()) continue;
        uint32_t bitmap = f.cglp + 8 + uint32_t(it - f.codepoints.begin()) * size;
        int top = -1, bottom = -1;
        for (int y = 0; y < g.cell_h; y++) {
            for (int x = 0; x < g.cell_w; x++) {
                if (native_value(bitmap, g.cell_w, g.bpp, x, y) == 1) {
                    if (top < 0) top = y;
                    bottom = y;
                }
            }
        }
        int x0, y0, x1, y1;
        if (top < 0 || !stbtt_GetCodepointBox(&f.info, int(ch), &x0, &y0, &x1, &y1) || y1 <= y0) continue;
        f.scale = float(bottom - top + 1) / float(y1 - std::max(y0, 0));
        return true;
    }
    f.scale = 0;
    return false;
}

HdGlyph* rasterize(HdFont& f, const TextGlyph& g, uint32_t cp) {
    auto* out = new HdGlyph;
    out->w = g.cell_w * kSuper;
    out->h = g.cell_h * kSuper;
    out->value.assign(size_t(out->w) * out->h, 0);
    // The game's ink: its columns, and the value of its outline.
    int ink_l = -1, ink_r = -1, ink_t = -1, ink_b = -1, outline[4] = {};
    for (int y = 0; y < g.cell_h; y++) {
        for (int x = 0; x < g.cell_w; x++) {
            int v = native_value(g.bitmap, g.cell_w, g.bpp, x, y);
            outline[v]++;
            if (v == 1) {
                if (ink_l < 0 || x < ink_l) ink_l = x;
                ink_r = std::max(ink_r, x);
                if (ink_t < 0) ink_t = y;
                ink_b = y;
            }
        }
    }
    if (ink_l < 0) return out;  // a blank: nothing to draw
    float sy = f.scale * kSuper;
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&f.info, int(cp), sy, sy, &x0, &y0, &x1, &y1);
    if (y1 > y0) {  // the height of the game's ink
        float k = float((ink_b - ink_t + 1) * kSuper) / float(y1 - y0);
        sy *= std::clamp(k, 1.0f / kFitRange, kFitRange);
    }
    float sx = sy;
    stbtt_GetCodepointBitmapBox(&f.info, int(cp), sx, sy, &x0, &y0, &x1, &y1);
    const int limit = (ink_r - ink_l + 2) * kSuper;
    if (x1 - x0 > limit) {
        sx *= float(limit) / float(x1 - x0);
        stbtt_GetCodepointBitmapBox(&f.info, int(cp), sx, sy, &x0, &y0, &x1, &y1);
    }
    const int bw = x1 - x0, bh = y1 - y0;
    if (bw <= 0 || bh <= 0) return out;
    std::vector<uint8_t> cover(size_t(bw) * bh);
    stbtt_MakeCodepointBitmap(&f.info, cover.data(), bw, bh, bw, sx, sy, int(cp));
    const int center = (ink_l + ink_r + 1) * kSuper / 2, middle = (ink_t + ink_b + 1) * kSuper / 2;
    const int left = center - bw / 2, top = middle - bh / 2;
    // No further than half a pixel past the game's ink on the sides, a pixel above and below
    // (an i's dot, an accent that sits higher): what the game shows around a letter stays.
    const int clip_l = ink_l * kSuper - kSuper / 2, clip_r = (ink_r + 1) * kSuper + kSuper / 2;
    const int clip_t = (ink_t - 1) * kSuper, clip_b = (ink_b + 2) * kSuper;
    for (int y = 0; y < bh; y++) {
        for (int x = 0; x < bw; x++) {
            int ox = left + x, oy = top + y;
            if (ox < std::max(0, clip_l) || oy < std::max(0, clip_t) || ox >= std::min(out->w, clip_r) ||
                oy >= std::min(out->h, clip_b) || cover[size_t(y) * bw + x] < 128) continue;
            out->value[size_t(oy) * out->w + ox] = 1;
        }
    }
    if (g.bpp == 2 && (outline[2] || outline[3])) {  // the outline: the fill grown by a native pixel
        const uint8_t v = outline[3] > outline[2] ? 3 : 2;
        std::vector<uint8_t> fill = out->value;
        for (int y = 0; y < out->h; y++) {
            for (int x = 0; x < out->w; x++) {
                if (fill[size_t(y) * out->w + x]) continue;
                bool near = false;
                for (int dy = -kSuper; dy <= kSuper && !near; dy++) {
                    for (int dx = -kSuper; dx <= kSuper && !near; dx++) {
                        int nx = x + dx, ny = y + dy;
                        near = nx >= 0 && ny >= 0 && nx < out->w && ny < out->h && dx * dx + dy * dy <= kSuper * kSuper &&
                               fill[size_t(ny) * out->w + nx];
                    }
                }
                if (near) out->value[size_t(y) * out->w + x] = v;
            }
        }
    }
    return out;
}

// Makes the HD letter of a letter just drawn, the first time (on the CPU thread; the 2D and
// 3D engines only read them).
void prepare(const TextGlyph& g, uint32_t cglp) {
    const int fi = int(g.font);
    if (fi <= 0 || fi >= kFontCount) return;
    HdFont* active = g_active[fi].load(std::memory_order_acquire);
    if (!active) return;
    HdFont& f = *active;
    if (!f.ok || g.index >= f.codepoints.size() || !f.codepoints[g.index]) return;
    if (!stbtt_FindGlyphIndex(&f.info, int(f.codepoints[g.index]))) return;  // not in the TrueType font
    if (f.glyphs[g.index].load(std::memory_order_acquire)) return;
    f.cglp = cglp;
    if (!f.fitted && !fit(f, g)) return;
    if (f.scale <= 0) return;
    f.glyphs[g.index].store(rasterize(f, g, f.codepoints[g.index]), std::memory_order_release);
}

bool on() {
    static const bool off = std::getenv("NTR_HD_TEXT") && std::getenv("NTR_HD_TEXT")[0] == '0';
    return !off;
}

}  // namespace

int64_t text_space_of(const uint8_t* host) {
    Memory& m = memory();
    if (host >= m.main_ram.data() && host < m.main_ram.data() + m.main_ram.size()) return host - m.main_ram.data();
    if (host >= m.vram.data() && host < m.vram.data() + m.vram.size()) return kMainRamSize + (host - m.vram.data());
    return -1;
}

uint32_t text_tag(const uint8_t* host, int nibble) {
    if (!g_any || !host) return 0;
    int64_t s = text_space_of(host);
    if (s < 0) return 0;
    const uint32_t* pg = g_pages[s / kPageBytes].get();
    return pg ? pg[(s % kPageBytes) * 2 + nibble] : 0;
}

const TextGlyph* text_glyph(uint32_t tag) {
    uint32_t id = tag >> 16;
    return id ? &g_glyphs[id] : nullptr;
}

int text_glyph_value(const TextGlyph& g, uint32_t tag) {
    uint32_t gx = tag & 0x3F, gy = (tag >> 8) & 0xFF;
    uint32_t bit = (gy * g.cell_w + gx) * g.bpp;
    return (rd8(g.bitmap + bit / 8) >> (8 - g.bpp - bit % 8)) & ((1 << g.bpp) - 1);
}

void text_tags_copy(uint32_t dst, uint32_t src, uint32_t size) {
    if (!g_any) return;
    while (size) {
        uint32_t n = std::min({size, kPageBytes - (dst & 0xFFF), kPageBytes - (src & 0xFFF)});
        int64_t d = space_of(dst), s = space_of(src);
        if (d >= 0) {
            const uint32_t* sp = s >= 0 ? page(uint32_t(s), false) : nullptr;
            uint32_t* dp = page(uint32_t(d), sp != nullptr);
            if (dp) {
                uint32_t* to = dp + (d % kPageBytes) * 2;
                if (sp) std::memmove(to, sp + (s % kPageBytes) * 2, n * 2 * sizeof(uint32_t));
                else std::memset(to, 0, n * 2 * sizeof(uint32_t));
            }
        }
        dst += n;
        src += n;
        size -= n;
    }
}

void text_tags_clear(uint32_t dst, uint32_t size) {
    if (!g_any) return;
    while (size) {
        uint32_t n = std::min(size, kPageBytes - (dst & 0xFFF));
        int64_t d = space_of(dst);
        if (uint32_t* dp = d >= 0 ? page(uint32_t(d), false) : nullptr) {
            std::memset(dp + (d % kPageBytes) * 2, 0, n * 2 * sizeof(uint32_t));
        }
        dst += n;
        size -= n;
    }
}

int text_hd_value(uint32_t tag, float fx, float fy) {
    const TextGlyph* g = text_glyph(tag);
    if (!g || !on()) return -1;
    const int fi = int(g->font);
    if (fi <= 0 || fi >= kFontCount) return -1;
    const HdFont* f = g_active[fi].load(std::memory_order_acquire);
    const HdGlyph* h = f ? f->glyphs[g->index].load(std::memory_order_acquire) : nullptr;
    if (!h) return -1;
    int x = int((float(tag & 0x3F) + fx) * kSuper), y = int((float((tag >> 8) & 0xFF) + fy) * kSuper);
    if (x < 0 || y < 0 || x >= h->w || y >= h->h) return 0;
    return h->value[size_t(y) * h->w + x];
}

namespace {

int font_index(const std::string& name) {
    for (int i = 1; i < kFontCount; i++) {
        if (name == kFontNames[i]) return i;
    }
    return 0;
}

}  // namespace

int text_hd_load(const std::string& font, std::vector<uint8_t> ttf, std::vector<uint32_t> codepoints) {
    if (!font_index(font)) return -1;
    HdFont& f = g_loaded.emplace_back();
    f.ttf = std::move(ttf);
    f.codepoints = std::move(codepoints);
    int offset = stbtt_GetFontOffsetForIndex(f.ttf.data(), 0);
    f.ok = offset >= 0 && stbtt_InitFont(&f.info, f.ttf.data(), offset);
    if (!f.ok) {
        g_loaded.pop_back();
        return -1;
    }
    return int(g_loaded.size() - 1);
}

void text_hd_use(const std::string& font, int handle) {
    const int fi = font_index(font);
    if (!fi) return;
    HdFont* f = handle >= 0 && size_t(handle) < g_loaded.size() ? &g_loaded[size_t(handle)] : nullptr;
    if (g_active[fi].exchange(f, std::memory_order_acq_rel) == f || !f) return;
    // The letters already drawn (on the screen now) get their HD version in the new font.
    for (uint32_t id = 1; id < kGlyphs; id++) {
        const TextGlyph& g = g_glyphs[id];
        if (int(g.font) == fi && g.cglp) prepare(g, g.cglp);
    }
}

bool text_hd_debug() {
    static const bool debug = std::getenv("NTR_HD_TEXT_DEBUG");
    return debug;
}

// --- hooks (config/boej/hle.tsv) -----------------------------------------------------

// NNS_G2dCharCanvasDrawChar(canvas, font, x, y, cl, code).
void hle_nns_draw_char(Cpu& c, GuestFn orig) {
    Cpu q = c;
    const uint32_t canvas = c.r[0], font = c.r[1], sp = c.r[13];
    const int x = int32_t(c.r[2]), y = int32_t(c.r[3]);
    const uint32_t cl = rd32(sp), code = rd16(sp + 4);
    orig(c);
    if (!on()) return;
    q.r[0] = font;
    q.r[1] = code;
    call_address(q, 0x020182AC);  // Font_GlyphIndex
    uint32_t glyph = q.r[0] == 0xFFFF ? rd16(rd32(font) + 2) : q.r[0];
    q.r[0] = font;
    q.r[1] = glyph;
    call_address(q, 0x020182F4);  // Font_GlyphWidths
    const uint32_t cwdh = q.r[0], cglp = rd32(rd32(font) + 8);
    if (rd8(cglp + 7) != 0) return;  // rotated
    const uint32_t vtable = rd32(canvas + 0x14);
    const bool obj = vtable == 0x0208BF14;
    if (!obj && vtable != 0x0208BF08) return;
    const uint32_t chars = rd32(canvas), area_w = rd32(canvas + 4), area_h = rd32(canvas + 8), param = rd32(canvas + 0x10);
    const uint32_t bpp = rd8(canvas + 0xC);
    if (bpp != 4 && bpp != 8) return;
    TextGlyph g{};
    g.font = font_of(font - 4);
    g.cell_w = rd8(cglp);
    g.cell_h = rd8(cglp + 1);
    g.bpp = rd8(cglp + 6);
    g.depth8 = bpp == 8;
    g.index = uint16_t(glyph);
    g.bitmap = cglp + 8 + glyph * rd16(cglp + 2);
    for (int v = 1; v < 4; v++) g.colors[v] = uint8_t((cl + v - 1) & (bpp == 8 ? 0xFF : 0x0F));
    g.cglp = cglp;
    prepare(g, cglp);
    const int x0 = x + int8_t(rd8(cwdh)), width = rd8(cwdh + 1), rows = g.cell_h;
    if (width <= 0 || width > 0x3F || (g.bpp != 1 && g.bpp != 2)) return;
    tag_pixels(g, width, rows, [&](int gx, int gy, uint32_t* addr, int* nibble) {
        const int px = x0 + gx, py = y + gy;
        if (px < 0 || py < 0 || uint32_t(px) >= area_w * 8 || uint32_t(py) >= area_h * 8) return false;
        uint32_t cx = uint32_t(px) >> 3, cy = uint32_t(py) >> 3;
        uint32_t ch = obj ? obj1d_char(cx, cy, area_w, area_h, param & 0xFF, (param >> 8) & 0xFF) : param * cy + cx;
        *addr = chars + ch * bpp * 8 + (py & 7) * bpp + (px & 7) * bpp / 8;
        *nibble = bpp == 4 ? (px & 1) : 0;
        return true;
    });
}

#define NTR_FONT_BLIT(name, layout, bpp)          \
    void hle_##name(Cpu& c, GuestFn orig) {        \
        Cpu args = c;                              \
        orig(c);                                   \
        if (on()) tag_letter(args, layout, bpp);   \
    }
NTR_FONT_BLIT(font_blit_tiled4, Layout::Tiled4, 1)
NTR_FONT_BLIT(font_blit_linear4, Layout::Linear4, 1)
NTR_FONT_BLIT(font_blit_tiled8, Layout::Tiled8, 1)
NTR_FONT_BLIT(font_blit_linear8, Layout::Linear8, 1)
NTR_FONT_BLIT(font2_blit_tiled4, Layout::Tiled4, 2)
NTR_FONT_BLIT(font2_blit_linear4, Layout::Linear4, 2)
NTR_FONT_BLIT(font2_blit_tiled8, Layout::Tiled8, 2)
NTR_FONT_BLIT(font2_blit_linear8, Layout::Linear8, 2)
#undef NTR_FONT_BLIT

// MIi_CpuCopy16/32/Fast, MI_CpuCopy8: (src, dst, size).
void hle_tags_copy_sdk(Cpu& c, GuestFn orig) {
    const uint32_t src = c.r[0], dst = c.r[1], size = c.r[2];
    orig(c);
    text_tags_copy(dst, src, size);
}
// memcpy: (dst, src, size).
void hle_tags_copy_c(Cpu& c, GuestFn orig) {
    const uint32_t dst = c.r[0], src = c.r[1], size = c.r[2];
    orig(c);
    text_tags_copy(dst, src, size);
}
// MIi_CpuClear16/32/Fast: (value, dst, size).
void hle_tags_clear_sdk(Cpu& c, GuestFn orig) {
    const uint32_t dst = c.r[1], size = c.r[2];
    orig(c);
    text_tags_clear(dst, size);
}
// MI_CpuFill8, memset: (dst, value, size).
void hle_tags_clear_c(Cpu& c, GuestFn orig) {
    const uint32_t dst = c.r[0], size = c.r[2];
    orig(c);
    text_tags_clear(dst, size);
}

}  // namespace ntr
