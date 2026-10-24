// The cartridge image, read by ROM offset: either a .nds dump or the game data package
// (boej.pak) that tools/extract.py makes from one (ntr/nitro.h). Both are memory-mapped,
// so opening is instant whatever the size.
//
// Mods (mods.cpp) lay byte ranges over the image: their files at offsets past its end and
// patched copies of the FAT and the overlay table pointing at them.
#include <algorithm>
#include <cstring>
#include <functional>
#include <vector>

#include <ntr/nitro.h>

#include "runtime.h"

namespace ntr {

namespace {

struct Override {
    uint32_t offset, size;
    const uint8_t* data;
    std::function<const uint8_t*()> load;  // gives `data` on the first read when it is null
};

RomImage g_image;
std::vector<Override> g_overrides;  // sorted by offset, never overlapping

}  // namespace

bool rom_load(const std::string& path) {
    if (!g_image.open(path)) return false;
    char code[5] = {};
    rom_read(0x0C, code, 4);
    if (std::strcmp(code, "BOEJ") != 0) NTR_LOG("[rom] warning: game code is %s, not BOEJ\n", code);
    return true;
}

const RomImage& rom_image() { return g_image; }

void rom_override(uint32_t offset, const uint8_t* data, uint32_t size) {
    rom_override_lazy(offset, size, [data] { return data; });
}

void rom_override_lazy(uint32_t offset, uint32_t size, std::function<const uint8_t*()> load) {
    if (!size) return;
    auto it = std::lower_bound(g_overrides.begin(), g_overrides.end(), offset,
                               [](const Override& o, uint32_t off) { return o.offset < off; });
    g_overrides.insert(it, {offset, size, nullptr, std::move(load)});
}

void rom_read(uint32_t offset, void* dst, uint32_t size) {
    g_image.read(offset, dst, size);
    if (g_overrides.empty()) return;
    uint8_t* out = static_cast<uint8_t*>(dst);
    uint64_t end = uint64_t(offset) + size;
    // First override that ends after `offset`.
    auto it = std::upper_bound(g_overrides.begin(), g_overrides.end(), uint64_t(offset),
                               [](uint64_t off, const Override& o) { return off < uint64_t(o.offset) + o.size; });
    for (; it != g_overrides.end() && it->offset < end; ++it) {
        uint64_t lo = std::max<uint64_t>(offset, it->offset);
        uint64_t hi = std::min(end, uint64_t(it->offset) + it->size);
        if (lo >= hi) continue;
        if (!it->data) it->data = it->load();
        std::memcpy(out + (lo - offset), it->data + (lo - it->offset), hi - lo);
    }
}

uint32_t rom_read32(uint32_t offset) {
    uint8_t b[4];
    rom_read(offset, b, 4);
    return load32(b);
}

}  // namespace ntr
