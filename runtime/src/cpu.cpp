// CPU state outside the register file: modes and banked registers, CP15, BIOS calls
// and dispatch of calls whose target is only known at run time.
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include <ntr/registry.h>

#include "runtime.h"

namespace ntr {

namespace {

Cpu g_cpu;

// Banked r13/r14/SPSR per mode. USR and SYS share the user bank.
struct Bank {
    uint32_t r13 = 0, r14 = 0, spsr = 0;
};
Bank g_banks[32];

uint32_t bank_index(uint32_t mode) { return mode == 0x10 ? 0x1F : mode; }

uint32_t g_cp15[16][16][8];
// Overlay id + 1 per 32-byte block of main RAM (0 = none). Overlays are 32-byte aligned
// and one can start in the same 4 KiB page where another ends (ov126 / ov132).
constexpr uint32_t kOverlayBlockShift = 5;
uint16_t g_active_overlay[0x400000 >> kOverlayBlockShift];
uint32_t overlay_block(uint32_t addr) { return (addr & 0x3FFFFF) >> kOverlayBlockShift; }

}  // namespace

Cpu& cpu() { return g_cpu; }

void cpu_reset(uint32_t mode) {
    std::memset(&g_cpu, 0, sizeof g_cpu);
    g_cpu.mode = mode;
    g_cpu.irq_off = 1;
    g_cpu.fiq_off = 1;
}

uint32_t cpsr_read(const Cpu& c) {
    return c.n << 31 | c.z << 30 | c.c << 29 | c.v << 28 | c.q << 27 | c.irq_off << 7 | c.fiq_off << 6 |
           c.thumb << 5 | c.mode;
}

void cpsr_write(Cpu& c, uint32_t value, uint32_t mask) {
    uint32_t v = (cpsr_read(c) & ~mask) | (value & mask);
    c.n = v >> 31;
    c.z = (v >> 30) & 1;
    c.c = (v >> 29) & 1;
    c.v = (v >> 28) & 1;
    c.q = (v >> 27) & 1;
    c.irq_off = (v >> 7) & 1;
    c.fiq_off = (v >> 6) & 1;
    uint32_t mode = v & 0x1F;
    if (mode != c.mode) {
        Bank& old = g_banks[bank_index(c.mode)];
        old.r13 = c.r[13];
        old.r14 = c.r[14];
        old.spsr = c.spsr;
        Bank& next = g_banks[bank_index(mode)];
        c.r[13] = next.r13;
        c.r[14] = next.r14;
        c.spsr = next.spsr;
        c.mode = mode;
    }
}

uint32_t spsr_read(const Cpu& c) { return c.spsr; }

void spsr_write(Cpu& c, uint32_t value, uint32_t mask) { c.spsr = (c.spsr & ~mask) | (value & mask); }

void exception_return(Cpu& c) { cpsr_write(c, c.spsr, 0xFFFFFFFF); }

void mcr(Cpu& c, uint32_t cp, uint32_t op1, uint32_t value, uint32_t crn, uint32_t crm, uint32_t op2) {
    (void)c;
    if (cp != 15) {
        NTR_LOG("[cpu] mcr to coprocessor %u ignored\n", cp);
        return;
    }
    // Cache maintenance (c7) and wait-for-interrupt have no host equivalent.
    if (crn == 7 && crm == 0 && op2 == 4) return;
    g_cp15[crn][crm][op2 & 7] = value;
    (void)op1;
}

uint32_t mrc(Cpu& c, uint32_t cp, uint32_t op1, uint32_t crn, uint32_t crm, uint32_t op2) {
    (void)c;
    (void)op1;
    if (cp != 15) return 0;
    if (crn == 0 && crm == 0) {
        switch (op2) {
            case 0: return 0x41059461;  // ARM946E-S
            case 1: return 0x0F0D2112;  // cache type
            case 2: return 0x00140180;  // TCM sizes
        }
    }
    return g_cp15[crn][crm][op2 & 7];
}

[[noreturn]] void unhandled(Cpu& c, uint32_t pc, const char* what) {
    NTR_LOG("\n[cpu] unhandled at %08X: %s\n", pc, what);
    for (int i = 0; i < 16; i++) NTR_LOG("  r%-2d %08X%s", i, c.r[i], i % 4 == 3 ? "\n" : "");
    NTR_LOG("  cpsr %08X\n", cpsr_read(c));
    std::exit(3);
}

// --- dispatch ---------------------------------------------------------------------

namespace {

const FuncEntry* find_in(const ModuleEntry& m, uint32_t addr) {
    auto end = m.funcs + m.count;
    auto it = std::lower_bound(m.funcs, end, addr, [](const FuncEntry& f, uint32_t a) { return f.addr < a; });
    return it != end && it->addr == addr ? it : nullptr;
}

int overlay_id(const ModuleEntry& m) {
    return std::strncmp(m.name, "ov", 2) == 0 ? std::atoi(m.name + 2) : -1;
}

}  // namespace

void overlay_set_active(uint32_t id, bool active) {
    for (uint32_t i = 0; i < kRecompModuleCount; i++) {
        const ModuleEntry& m = kRecompModules[i];
        if (overlay_id(m) != int(id)) continue;
        for (uint32_t a = m.base & ~0x1Fu; a < m.base + m.size; a += 1u << kOverlayBlockShift) {
            g_active_overlay[overlay_block(a)] = uint16_t(active ? id + 1 : 0);
        }
        NTR_LOG("[ovl] overlay %u %s\n", id, active ? "active" : "inactive");
        return;
    }
    NTR_LOG("[ovl] overlay %u was not recompiled\n", id);
}

std::string overlay_active_list() {
    std::set<uint32_t> ids;
    for (uint32_t v : g_active_overlay) {
        if (v) ids.insert(v - 1);
    }
    std::string out;
    for (uint32_t id : ids) out += (out.empty() ? "" : " ") + std::to_string(id);
    return out.empty() ? "none" : out;
}

void call_address(Cpu& c, uint32_t target) {
    uint32_t addr = target & ~1u;
    for (uint32_t i = 0; i < kRecompModuleCount; i++) {
        const ModuleEntry& m = kRecompModules[i];
        if (addr < m.base || addr - m.base >= m.size) continue;
        int id = overlay_id(m);
        if (id >= 0 && g_active_overlay[overlay_block(addr)] != uint32_t(id) + 1) continue;
        if (const FuncEntry* f = find_in(m, addr)) {
            f->fn(c);
            return;
        }
        NTR_LOG("[cpu] %08X is inside %s but not a function entry\n", addr, m.name);
        break;
    }
    unhandled(c, target, "indirect call to unknown address");
}

void call_indirect(Cpu& c, uint32_t target) { call_address(c, target); }

// --- BIOS (SWI) -----------------------------------------------------------------

namespace {

void bios_lz77(uint32_t src, uint32_t dst) {
    uint32_t header = rd32(src);
    uint32_t size = header >> 8;
    src += 4;
    uint32_t out = 0;
    while (out < size) {
        uint8_t flags = rd8(src++);
        for (int i = 0; i < 8 && out < size; i++, flags <<= 1) {
            if (flags & 0x80) {
                uint32_t b0 = rd8(src++), b1 = rd8(src++);
                uint32_t len = (b0 >> 4) + 3, disp = ((b0 & 0xF) << 8 | b1) + 1;
                for (uint32_t k = 0; k < len && out < size; k++, out++) wr8(dst + out, rd8(dst + out - disp));
            } else {
                wr8(dst + out++, rd8(src++));
            }
        }
    }
}

void bios_rl(uint32_t src, uint32_t dst) {
    uint32_t size = rd32(src) >> 8;
    src += 4;
    uint32_t out = 0;
    while (out < size) {
        uint8_t flag = rd8(src++);
        if (flag & 0x80) {
            uint32_t len = (flag & 0x7F) + 3;
            uint8_t v = rd8(src++);
            for (uint32_t k = 0; k < len && out < size; k++) wr8(dst + out++, v);
        } else {
            for (uint32_t k = 0; k < uint32_t(flag & 0x7F) + 1 && out < size; k++) wr8(dst + out++, rd8(src++));
        }
    }
}

// CRC-16 with polynomial 0xA001, LSB first (the BIOS uses an unrolled table form).
uint16_t bios_crc16(uint16_t crc, uint32_t addr, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        crc ^= rd8(addr + i);
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ ((crc & 1) ? 0xA001 : 0);
    }
    return crc;
}

}  // namespace

void swi(Cpu& c, uint32_t number) {
    switch (number) {
        case 0x03:  // WaitByLoop
            return;
        case 0x09: {  // Div
            int32_t num = int32_t(c.r[0]), den = int32_t(c.r[1]);
            if (den == 0) unhandled(c, 0, "SWI Div by zero");
            int32_t q = (num == INT32_MIN && den == -1) ? num : num / den;
            int32_t r = (num == INT32_MIN && den == -1) ? 0 : num % den;
            c.r[0] = uint32_t(q);
            c.r[1] = uint32_t(r);
            c.r[3] = uint32_t(q < 0 ? -q : q);
            return;
        }
        case 0x0B: {  // CpuSet
            uint32_t src = c.r[0], dst = c.r[1], ctl = c.r[2], count = ctl & 0x1FFFFF;
            bool fill = ctl & (1u << 24), word = ctl & (1u << 26);
            for (uint32_t i = 0; i < count; i++) {
                if (word) wr32(dst + i * 4, rd32(fill ? src : src + i * 4));
                else wr16(dst + i * 2, rd16(fill ? src : src + i * 2));
            }
            return;
        }
        case 0x0C: {  // CpuFastSet
            uint32_t src = c.r[0], dst = c.r[1], ctl = c.r[2];
            uint32_t count = ((ctl & 0x1FFFFF) + 7) & ~7u;  // whole 8-word blocks
            bool fill = ctl & (1u << 24);
            for (uint32_t i = 0; i < count; i++) wr32(dst + i * 4, rd32(fill ? src : src + i * 4));
            return;
        }
        case 0x0D: {  // Sqrt
            uint32_t v = c.r[0], root = 0;
            while (uint64_t(root + 1) * (root + 1) <= v) root++;
            c.r[0] = root;
            return;
        }
        case 0x0E:  // GetCRC16
            c.r[0] = bios_crc16(uint16_t(c.r[0]), c.r[1], c.r[2]);
            return;
        case 0x0F:  // IsDebugger
            c.r[0] = 0;
            return;
        case 0x11:  // LZ77UnCompWram
        case 0x12:  // LZ77UnCompVram
            bios_lz77(c.r[0], c.r[1]);
            return;
        case 0x14:  // RLUnCompWram
        case 0x15:  // RLUnCompVram
            bios_rl(c.r[0], c.r[1]);
            return;
        default:
            NTR_LOG("[bios] swi %02X not implemented\n", number);
            unhandled(c, 0, "unimplemented SWI");
    }
}

}  // namespace ntr
