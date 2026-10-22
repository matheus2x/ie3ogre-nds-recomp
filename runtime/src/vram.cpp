// VRAM bank mapping (VRAMCNT_A..I at 0x04000240-0x04000249).
//
// Each enabled bank is placed in one area according to its MST/OFS fields. The CPU sees
// the BG/OBJ areas at 0x06000000-0x067FFFFF and banks in LCDC mode at 0x06800000; the
// texture and extended palette areas are only visible to the engines. When two banks
// overlap the later one wins (hardware ORs them on reads).
#include "runtime.h"

namespace ntr {

namespace {

struct Bank {
    uint32_t offset, size;  // inside Memory::vram (LCDC order)
};
constexpr Bank kBanks[9] = {
    {0x00000, 0x20000}, {0x20000, 0x20000}, {0x40000, 0x20000}, {0x60000, 0x20000},  // A-D
    {0x80000, 0x10000}, {0x90000, 0x4000},  {0x94000, 0x4000},                       // E-G
    {0x98000, 0x8000},  {0xA0000, 0x4000},                                           // H-I
};
constexpr uint32_t kVramCnt[9] = {0x240, 0x241, 0x242, 0x243, 0x244, 0x245, 0x246, 0x248, 0x249};

constexpr uint32_t kAreaSize[size_t(VramArea::Count)] = {
    0x80000, 0x20000, 0x40000, 0x20000,  // A BG, B BG, A OBJ, B OBJ
    0x80000, 0x18000,                    // texture slots 0-3, texture palette slots 0-5
    0x8000, 0x8000, 0x2000, 0x2000,      // extended palettes
};

constexpr uint32_t kGranule = 0x4000;

VramView g_views[size_t(VramArea::Count)];
bool g_lcdc[9];

VramView& view(VramArea a) { return g_views[size_t(a)]; }

void place(VramArea area, uint32_t offset, int bank, uint32_t size = 0) {
    VramView& v = view(area);
    uint8_t* base = memory().vram.data() + kBanks[bank].offset;
    if (size == 0) size = kBanks[bank].size;
    for (uint32_t k = 0; k < size; k += kGranule) {
        uint32_t g = (offset + k) / kGranule;
        if (g < 32) v.granule[g] = base + k;
    }
}

void map_bank(int bank, uint8_t cnt) {
    uint32_t mst = cnt & (bank <= 1 || bank >= 7 ? 3 : 7);
    uint32_t ofs = (cnt >> 3) & 3;
    if (mst == 0) {
        g_lcdc[bank] = true;
        return;
    }
    switch (bank) {
        case 0: case 1: case 2: case 3:  // A-D
            if (mst == 1) place(VramArea::ABg, 0x20000 * ofs, bank);
            else if (mst == 2 && bank <= 1) place(VramArea::AObj, 0x20000 * (ofs & 1), bank);
            else if (mst == 3) place(VramArea::Texture, 0x20000 * ofs, bank);
            else if (mst == 4 && bank == 2) place(VramArea::BBg, 0, bank);
            else if (mst == 4 && bank == 3) place(VramArea::BObj, 0, bank);
            // C/D with MST 2 belong to the ARM7.
            break;
        case 4:  // E
            if (mst == 1) place(VramArea::ABg, 0, bank);
            else if (mst == 2) place(VramArea::AObj, 0, bank);
            else if (mst == 3) place(VramArea::TexPalette, 0, bank);
            else if (mst == 4) place(VramArea::ABgExtPal, 0, bank, 0x8000);
            break;
        case 5: case 6: {  // F, G
            uint32_t off = 0x4000 * (ofs & 1) + 0x10000 * (ofs >> 1);
            if (mst == 1) place(VramArea::ABg, off, bank);
            else if (mst == 2) place(VramArea::AObj, off, bank);
            else if (mst == 3) place(VramArea::TexPalette, kGranule * ((ofs & 1) + (ofs >> 1) * 4), bank);
            else if (mst == 4) place(VramArea::ABgExtPal, kGranule * (ofs & 1), bank);
            else if (mst == 5) place(VramArea::AObjExtPal, 0, bank);
            break;
        }
        case 7:  // H
            if (mst == 1) place(VramArea::BBg, 0, bank);
            else if (mst == 2) place(VramArea::BBgExtPal, 0, bank);
            break;
        case 8:  // I
            if (mst == 1) place(VramArea::BBg, 0x8000, bank);
            else if (mst == 2) place(VramArea::BObj, 0, bank);
            else if (mst == 3) place(VramArea::BObjExtPal, 0, bank);
            break;
    }
}

void set_page(uint32_t addr, uint8_t* p) {
    g_read_pages[addr >> 12] = p;
    g_write_pages[addr >> 12] = p;
}

}  // namespace

const VramView& vram_view(VramArea area) { return g_views[size_t(area)]; }

uint8_t* vram_lcdc_bank(int bank) {
    return g_lcdc[bank] ? memory().vram.data() + kBanks[bank].offset : nullptr;
}

void vram_remap() {
    for (size_t a = 0; a < size_t(VramArea::Count); a++) {
        g_views[a] = VramView{};
        g_views[a].size = kAreaSize[a];
    }
    for (int b = 0; b < 9; b++) {
        g_lcdc[b] = false;
        uint8_t cnt = memory().io[kVramCnt[b]];
        if (cnt & 0x80) map_bank(b, cnt);
    }

    // CPU view: each 2 MiB window mirrors its area.
    static const VramArea kCpuAreas[4] = {VramArea::ABg, VramArea::BBg, VramArea::AObj, VramArea::BObj};
    for (uint32_t addr = 0x06000000; addr < 0x06800000; addr += 0x1000) {
        set_page(addr, view(kCpuAreas[(addr >> 21) & 3]).ptr(addr & 0x1FFFFF));
    }
    for (int b = 0; b < 9; b++) {
        for (uint32_t k = 0; k < kBanks[b].size; k += 0x1000) {
            set_page(0x06800000 + kBanks[b].offset + k, g_lcdc[b] ? memory().vram.data() + kBanks[b].offset + k : nullptr);
        }
    }
}

}  // namespace ntr
