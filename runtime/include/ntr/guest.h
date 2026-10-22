// Interface between recompiled code and the runtime.
//
// Every recompiled function has the signature `void fn(ntr::Cpu&)`. Guest registers,
// flags and memory are reached only through what this header declares, so generated
// code does not depend on runtime internals.
#pragma once

#include <cstdint>

namespace ntr {

// ARM9 state as seen by recompiled code. Flags are kept as 0/1 words so generated
// code can combine them with plain arithmetic.
struct Cpu {
    uint32_t r[16];
    uint32_t n, z, c, v, q;
    uint32_t mode;     // CPSR[4:0]
    uint32_t thumb;    // CPSR[5]
    uint32_t irq_off;  // CPSR[7] (I)
    uint32_t fiq_off;  // CPSR[6] (F)
    uint32_t spsr;
    int32_t budget;    // decremented on backward branches; poll() runs when it goes negative
};

using GuestFn = void (*)(Cpu&);

// ---------------------------------------------------------------------------
// Memory. Pages are 4 KiB; a null page goes through the slow path (MMIO, unmapped).

extern uint8_t* g_read_pages[1u << 20];
extern uint8_t* g_write_pages[1u << 20];

uint32_t slow_read32(uint32_t addr);
uint16_t slow_read16(uint32_t addr);
uint8_t slow_read8(uint32_t addr);
void slow_write32(uint32_t addr, uint32_t value);
void slow_write16(uint32_t addr, uint16_t value);
void slow_write8(uint32_t addr, uint8_t value);

inline uint32_t rd32(uint32_t addr) {
    addr &= ~3u;
    if (uint8_t* p = g_read_pages[addr >> 12]) {
        return *reinterpret_cast<uint32_t*>(p + (addr & 0xFFF));
    }
    return slow_read32(addr);
}

inline uint16_t rd16(uint32_t addr) {
    addr &= ~1u;
    if (uint8_t* p = g_read_pages[addr >> 12]) {
        return *reinterpret_cast<uint16_t*>(p + (addr & 0xFFF));
    }
    return slow_read16(addr);
}

inline uint8_t rd8(uint32_t addr) {
    if (uint8_t* p = g_read_pages[addr >> 12]) {
        return p[addr & 0xFFF];
    }
    return slow_read8(addr);
}

inline void wr32(uint32_t addr, uint32_t value) {
    addr &= ~3u;
    if (uint8_t* p = g_write_pages[addr >> 12]) {
        *reinterpret_cast<uint32_t*>(p + (addr & 0xFFF)) = value;
        return;
    }
    slow_write32(addr, value);
}

inline void wr16(uint32_t addr, uint16_t value) {
    addr &= ~1u;
    if (uint8_t* p = g_write_pages[addr >> 12]) {
        *reinterpret_cast<uint16_t*>(p + (addr & 0xFFF)) = value;
        return;
    }
    slow_write16(addr, value);
}

inline void wr8(uint32_t addr, uint8_t value) {
    if (uint8_t* p = g_write_pages[addr >> 12]) {
        p[addr & 0xFFF] = value;
        return;
    }
    slow_write8(addr, value);
}

// ARMv5 LDR from an unaligned address rotates the aligned word.
inline uint32_t ldr32(uint32_t addr) {
    uint32_t value = rd32(addr);
    uint32_t rot = (addr & 3) * 8;
    return rot ? (value >> rot) | (value << (32 - rot)) : value;
}

// ---------------------------------------------------------------------------
// ALU helpers.

inline uint32_t ror32(uint32_t v, uint32_t s) {
    s &= 31;
    return s ? (v >> s) | (v << (32 - s)) : v;
}

inline void set_nz(Cpu& c, uint32_t res) {
    c.n = res >> 31;
    c.z = res == 0;
}

inline uint32_t add_flags(Cpu& c, uint32_t a, uint32_t b, uint32_t carry_in = 0) {
    uint64_t wide = uint64_t(a) + b + carry_in;
    uint32_t res = uint32_t(wide);
    c.c = uint32_t(wide >> 32);
    c.v = ((~(a ^ b) & (a ^ res)) >> 31) & 1;
    set_nz(c, res);
    return res;
}

// a - b - !carry_in; C means "no borrow".
inline uint32_t sub_flags(Cpu& c, uint32_t a, uint32_t b, uint32_t carry_in = 1) {
    return add_flags(c, a, ~b, carry_in);
}

// Register-specified shifts (amount is Rs[7:0]). The *_c forms also update c.c.
inline uint32_t lsl_reg(uint32_t v, uint32_t s) { s &= 0xFF; return s >= 32 ? 0 : v << s; }
inline uint32_t lsr_reg(uint32_t v, uint32_t s) { s &= 0xFF; return s >= 32 ? 0 : v >> s; }
inline uint32_t asr_reg(uint32_t v, uint32_t s) {
    s &= 0xFF;
    return uint32_t(int32_t(v) >> (s >= 32 ? 31 : s));
}
inline uint32_t ror_reg(uint32_t v, uint32_t s) { return ror32(v, s & 0xFF); }

inline uint32_t lsl_reg_c(Cpu& c, uint32_t v, uint32_t s) {
    s &= 0xFF;
    if (s == 0) return v;
    if (s < 32) { c.c = (v >> (32 - s)) & 1; return v << s; }
    c.c = s == 32 ? (v & 1) : 0;
    return 0;
}
inline uint32_t lsr_reg_c(Cpu& c, uint32_t v, uint32_t s) {
    s &= 0xFF;
    if (s == 0) return v;
    if (s < 32) { c.c = (v >> (s - 1)) & 1; return v >> s; }
    c.c = s == 32 ? (v >> 31) : 0;
    return 0;
}
inline uint32_t asr_reg_c(Cpu& c, uint32_t v, uint32_t s) {
    s &= 0xFF;
    if (s == 0) return v;
    if (s < 32) { c.c = (v >> (s - 1)) & 1; return uint32_t(int32_t(v) >> s); }
    c.c = v >> 31;
    return uint32_t(int32_t(v) >> 31);
}
inline uint32_t ror_reg_c(Cpu& c, uint32_t v, uint32_t s) {
    s &= 0xFF;
    if (s == 0) return v;
    s &= 31;
    if (s == 0) { c.c = v >> 31; return v; }
    c.c = (v >> (s - 1)) & 1;
    return ror32(v, s);
}

// Saturating arithmetic (QADD/QSUB/QDADD/QDSUB); sets c.q on saturation.
inline uint32_t qadd(Cpu& c, uint32_t a, uint32_t b) {
    int64_t r = int64_t(int32_t(a)) + int32_t(b);
    if (r > INT32_MAX) { c.q = 1; return INT32_MAX; }
    if (r < INT32_MIN) { c.q = 1; return uint32_t(INT32_MIN); }
    return uint32_t(r);
}
inline uint32_t qsub(Cpu& c, uint32_t a, uint32_t b) {
    int64_t r = int64_t(int32_t(a)) - int32_t(b);
    if (r > INT32_MAX) { c.q = 1; return INT32_MAX; }
    if (r < INT32_MIN) { c.q = 1; return uint32_t(INT32_MIN); }
    return uint32_t(r);
}

inline uint32_t clz32(uint32_t v) { return v ? uint32_t(__builtin_clz(v)) : 32; }

// ---------------------------------------------------------------------------
// System. Implemented by the runtime.

uint32_t cpsr_read(const Cpu& c);
void cpsr_write(Cpu& c, uint32_t value, uint32_t field_mask);
uint32_t spsr_read(const Cpu& c);
void spsr_write(Cpu& c, uint32_t value, uint32_t field_mask);

void swi(Cpu& c, uint32_t number);
void mcr(Cpu& c, uint32_t cp, uint32_t op1, uint32_t value, uint32_t crn, uint32_t crm, uint32_t op2);
uint32_t mrc(Cpu& c, uint32_t cp, uint32_t op1, uint32_t crn, uint32_t crm, uint32_t op2);

// Calls a guest address whose target is not known statically (function pointers,
// overlay slots, interworking). Bit 0 selects Thumb.
void call_indirect(Cpu& c, uint32_t target);

// Function-entry tracing for debugging (build generated code with NTR_TRACE).
void trace_enter(uint32_t addr);
#ifdef NTR_TRACE
#define NTR_ENTER(addr) ::ntr::trace_enter(addr)
#else
#define NTR_ENTER(addr) ((void)0)
#endif

// Advances emulated time and delivers pending interrupts. Generated code calls it on
// backward branches so busy-wait loops observe hardware progress.
void poll(Cpu& c);

// Return from an exception handler (MOVS/SUBS pc, lr): restores CPSR from SPSR.
void exception_return(Cpu& c);

// Reached code the recompiler could not translate; the runtime reports and stops.
[[noreturn]] void unhandled(Cpu& c, uint32_t pc, const char* what);

}  // namespace ntr
