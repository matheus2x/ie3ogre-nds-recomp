// ARM9 MMIO: a register file plus the registers whose reads or writes have effects.
// Display, 3D and sound are not modeled yet; their registers just keep written values.
#include <cmath>
#include <deque>

#include "pxi.h"
#include "runtime.h"

namespace ntr {

namespace {

constexpr uint32_t REG_DISPSTAT = 0x04000004;
constexpr uint32_t REG_KEYINPUT = 0x04000130;
constexpr uint32_t REG_IPCSYNC = 0x04000180;
constexpr uint32_t REG_IPCFIFOCNT = 0x04000184;
constexpr uint32_t REG_IPCFIFOSEND = 0x04000188;
constexpr uint32_t REG_IF = 0x04000214;
constexpr uint32_t REG_DIVCNT = 0x04000280;
constexpr uint32_t REG_SQRTCNT = 0x040002B0;
constexpr uint32_t REG_GXSTAT = 0x04000600;
constexpr uint32_t REG_IPCFIFORECV = 0x04100000;

uint32_t reg(uint32_t addr) { return load32(memory().io.data() + (addr - 0x04000000)); }
void set_reg(uint32_t addr, uint32_t v) { store32(memory().io.data() + (addr - 0x04000000), v); }

// Game card bus (ROMCTRL/ROMDATA). Bulk data reads are done by HLE; this answers the
// few commands the SDK still sends directly, such as reading the chip ID.
constexpr uint32_t REG_ROMCTRL = 0x040001A4;
constexpr uint32_t REG_ROMDATA = 0x04100010;
constexpr uint32_t kChipId = 0x00001FC2;

std::deque<uint32_t> g_card_data;

void card_start(uint32_t ctrl) {
    g_card_data.clear();
    uint32_t blocks = (ctrl >> 24) & 7;
    uint32_t bytes = blocks == 0 ? 0 : blocks == 7 ? 4 : 0x100u << blocks;
    uint8_t cmd[8];
    for (int i = 0; i < 8; i++) cmd[i] = memory().io[0x1A8 + i];
    const auto& data = rom().data;
    for (uint32_t i = 0; i < bytes; i += 4) {
        uint32_t word = 0;
        if (cmd[0] == 0xB8) {
            word = kChipId;
        } else if (cmd[0] == 0xB7) {
            uint32_t addr = uint32_t(cmd[1]) << 24 | cmd[2] << 16 | cmd[3] << 8 | cmd[4];
            for (int k = 0; k < 4; k++) {
                uint32_t off = addr + i + k;
                word |= uint32_t(off < data.size() ? data[off] : 0xFF) << (8 * k);
            }
        }
        g_card_data.push_back(word);
    }
}

uint32_t card_status(uint32_t ctrl) {
    ctrl &= ~0x80800000u;
    if (!g_card_data.empty()) ctrl |= 0x80800000u;  // busy, data word ready
    return ctrl;
}

// DMA: transfers run to completion as soon as they are enabled. Timed modes (VBlank,
// HBlank, GX FIFO, ...) are also run immediately until the matching devices exist.
void run_dma(int n) {
    uint32_t base = 0x040000B0 + 12 * n;
    uint32_t src = reg(base), dst = reg(base + 4), cnt = reg(base + 8);
    uint32_t count = cnt & 0x1FFFFF;
    if (count == 0) count = 0x200000;
    bool word = cnt & (1u << 26);
    uint32_t unit = word ? 4 : 2;
    auto step = [unit](uint32_t mode) -> int32_t { return mode == 0 || mode == 3 ? int32_t(unit) : mode == 1 ? -int32_t(unit) : 0; };
    int32_t dst_step = step((cnt >> 21) & 3), src_step = step((cnt >> 23) & 3);
    for (uint32_t i = 0; i < count; i++) {
        if (word) wr32(dst, rd32(src));
        else wr16(dst, rd16(src));
        src += src_step;
        dst += dst_step;
    }
    uint32_t timing = (cnt >> 27) & 7;
    if (!(cnt & (1u << 25)) || timing == 0) cnt &= ~0x80000000u;
    set_reg(base + 8, cnt);
    if (cnt & (1u << 30)) raise_irq(1u << (8 + n));
}

void update_divider() {
    uint32_t cnt = reg(REG_DIVCNT);
    int64_t num = (cnt & 3) == 0 ? int32_t(reg(0x04000290)) : int64_t(uint64_t(reg(0x04000294)) << 32 | reg(0x04000290));
    int64_t den = (cnt & 3) == 2 ? int64_t(uint64_t(reg(0x0400029C)) << 32 | reg(0x04000298)) : int32_t(reg(0x04000298));
    int64_t q, r;
    if (den == 0) {
        q = num < 0 ? 1 : -1;
        r = num;
        cnt |= 0x4000;
    } else if (num == INT64_MIN && den == -1) {
        q = num;
        r = 0;
        cnt &= ~0x4000u;
    } else {
        q = num / den;
        r = num % den;
        cnt &= ~0x4000u;
    }
    if ((cnt & 3) == 0) {  // 32/32: results are sign-extended 32-bit values
        q = int32_t(q);
        r = int32_t(r);
    }
    set_reg(REG_DIVCNT, cnt & ~0x8000u);
    set_reg(0x040002A0, uint32_t(q));
    set_reg(0x040002A4, uint32_t(uint64_t(q) >> 32));
    set_reg(0x040002A8, uint32_t(r));
    set_reg(0x040002AC, uint32_t(uint64_t(r) >> 32));
}

void update_sqrt() {
    uint32_t cnt = reg(REG_SQRTCNT);
    uint64_t v = (cnt & 1) ? (uint64_t(reg(0x040002BC)) << 32 | reg(0x040002B8)) : reg(0x040002B8);
    uint64_t root = uint64_t(std::sqrt(double(v)));
    while (root * root > v) root--;
    while ((root + 1) * (root + 1) <= v) root++;
    set_reg(REG_SQRTCNT, cnt & ~0x8000u);
    set_reg(0x040002B4, uint32_t(root));
}

}  // namespace

uint32_t io_read32(uint32_t addr) {
    if (addr == REG_IPCFIFORECV) return pxi_recv_word();
    if (addr == REG_ROMDATA) {
        if (g_card_data.empty()) return 0;
        uint32_t w = g_card_data.front();
        g_card_data.pop_front();
        return w;
    }
    if (addr - 0x04000000 >= kIoSize) {
        NTR_LOG("[io] read from unmodeled %08X\n", addr);
        return 0;
    }
    switch (addr) {
        case REG_DISPSTAT: {
            // Busy-waits on VCOUNT/DISPSTAT must see time pass.
            advance_time(64);
            uint32_t v = current_vcount();
            uint32_t stat = reg(addr) & 0xFFB8;
            stat |= (v >= 192 && v < 262) ? 1 : 0;
            stat |= in_hblank() ? 2 : 0;
            if (v == (((reg(addr) >> 8) & 0xFF) | ((reg(addr) & 0x80) << 1))) stat |= 4;
            return stat | (v << 16);
        }
        case 0x04000100: case 0x04000104: case 0x04000108: case 0x0400010C: {
            int i = (addr - 0x04000100) / 4;
            return timer_read_counter(i) | (reg(addr) & 0xFFFF0000);
        }
        case REG_KEYINPUT:
            return 0x03FF | (reg(addr) & 0xFFFF0000);
        case REG_IPCSYNC:
            return (reg(addr) & 0x6F00) | pxi_sync_from_arm7();
        case REG_IPCFIFOCNT:
            return (reg(addr) & 0x8404) | pxi_fifo_status();
        case REG_ROMCTRL:
            return card_status(reg(addr));
        case REG_GXSTAT:
            return 0x06000000;  // FIFO empty and less than half full, geometry idle
        default:
            return reg(addr);
    }
}

void io_write32(uint32_t addr, uint32_t value, uint32_t mask) {
    if (addr - 0x04000000 >= kIoSize) {
        NTR_LOG("[io] write %08X (mask %08X) to unmodeled %08X\n", value, mask, addr);
        return;
    }
    uint32_t old = reg(addr);
    switch (addr) {
        case REG_IF:
            set_reg(addr, old & ~(value & mask));  // writing 1 acknowledges
            return;
        case REG_IPCFIFOSEND:
            pxi_send_word(value);
            return;
        case REG_IPCFIFOCNT:
            if (value & mask & 0x4000) value &= ~0x4000u;  // writing 1 clears the error flag
            if (value & mask & 0x0008) pxi_clear_send_fifo();
            break;
        default:
            break;
    }
    set_reg(addr, (old & ~mask) | (value & mask));
    if (addr == REG_ROMCTRL && (value & mask & 0x80000000)) card_start(reg(addr));
    for (int n = 0; n < 4; n++) {
        if (addr == 0x040000B8u + 12 * n && (value & mask & 0x80000000) && !(old & 0x80000000)) run_dma(n);
    }
    if (addr >= 0x04000100 && addr < 0x04000110) {
        // The low half is the reload value; the high half is the control register.
        int i = (addr - 0x04000100) / 4;
        uint32_t v = reg(addr);
        timer_write(i, uint16_t(v), uint16_t(v >> 16));
    }
    if (addr == REG_IPCSYNC) pxi_sync_from_arm9((reg(addr) >> 8) & 0xF, (value & mask & 0x2000) != 0);
    if (addr == REG_DIVCNT || (addr >= 0x04000290 && addr < 0x040002A0)) update_divider();
    if (addr == REG_SQRTCNT || (addr >= 0x040002B8 && addr < 0x040002C0)) update_sqrt();
}

}  // namespace ntr
