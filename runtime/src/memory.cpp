// Guest address space: page tables for RAM-like regions, slow path for MMIO.
#include <cstring>

#include "runtime.h"

namespace ntr {

uint8_t* g_read_pages[1u << 20];
uint8_t* g_write_pages[1u << 20];

namespace {

Memory g_memory;

void map(uint32_t start, uint32_t end, std::vector<uint8_t>& buf) {
    for (uint32_t addr = start; addr < end; addr += 0x1000) {
        uint8_t* p = buf.data() + ((addr - start) % buf.size());
        g_read_pages[addr >> 12] = p;
        g_write_pages[addr >> 12] = p;
    }
}

// Byte-granular access to a word read/write for the slow path.
uint32_t lane_shift(uint32_t addr) { return (addr & 3) * 8; }

}  // namespace

Memory& memory() { return g_memory; }

void memory_init() {
    std::memset(g_read_pages, 0, sizeof g_read_pages);
    std::memset(g_write_pages, 0, sizeof g_write_pages);
    map(0x00000000, 0x02000000, g_memory.itcm);  // ITCM mirrored over its 32 MiB window
    map(0x02000000, 0x03000000, g_memory.main_ram);
    map(kDtcmBase, kDtcmBase + kDtcmSize, g_memory.dtcm);
    map(0x03000000, 0x04000000, g_memory.shared_wram);
    map(0x05000000, 0x06000000, g_memory.palette);
    map(0x06800000, 0x06800000 + 0x00A4000, g_memory.vram);
    map(0x07000000, 0x08000000, g_memory.oam);
}

void guest_copy_in(uint32_t dst, const void* src, uint32_t size) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (uint32_t i = 0; i < size; i++) wr8(dst + i, s[i]);
}

void guest_copy_out(void* dst, uint32_t src, uint32_t size) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (uint32_t i = 0; i < size; i++) d[i] = rd8(src + i);
}

// --- slow path ------------------------------------------------------------------

namespace {

bool is_io(uint32_t addr) { return (addr >> 24) == 0x04; }

uint32_t unmapped_read(uint32_t addr) {
    if (addr >= 0x08000000 && addr < 0x0A000000) return 0xFFFFFFFF;  // empty slot-2
    NTR_LOG("[mem] read from unmapped %08X\n", addr);
    return 0;
}

void unmapped_write(uint32_t addr, uint32_t value) {
    if (addr >= 0x08000000 && addr < 0x0A000000) return;
    NTR_LOG("[mem] write %08X to unmapped %08X\n", value, addr);
}

}  // namespace

uint32_t slow_read32(uint32_t addr) {
    return is_io(addr) ? io_read32(addr) : unmapped_read(addr);
}

uint16_t slow_read16(uint32_t addr) {
    uint32_t word = is_io(addr) ? io_read32(addr & ~3u) : unmapped_read(addr & ~3u);
    return uint16_t(word >> lane_shift(addr & ~1u));
}

uint8_t slow_read8(uint32_t addr) {
    uint32_t word = is_io(addr) ? io_read32(addr & ~3u) : unmapped_read(addr & ~3u);
    return uint8_t(word >> lane_shift(addr));
}

void slow_write32(uint32_t addr, uint32_t value) {
    if (is_io(addr)) io_write32(addr, value, 0xFFFFFFFF);
    else unmapped_write(addr, value);
}

void slow_write16(uint32_t addr, uint16_t value) {
    uint32_t shift = lane_shift(addr & ~1u);
    if (is_io(addr)) io_write32(addr & ~3u, uint32_t(value) << shift, 0xFFFFu << shift);
    else unmapped_write(addr, value);
}

void slow_write8(uint32_t addr, uint8_t value) {
    uint32_t shift = lane_shift(addr);
    if (is_io(addr)) io_write32(addr & ~3u, uint32_t(value) << shift, 0xFFu << shift);
    else unmapped_write(addr, value);
}

}  // namespace ntr
