// Runtime internals shared by the runtime sources (not used by generated code).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <ntr/guest.h>

namespace ntr {

// --- memory -----------------------------------------------------------------

constexpr uint32_t kMainRamSize = 4u << 20;
constexpr uint32_t kItcmSize = 32u << 10;
constexpr uint32_t kDtcmSize = 16u << 10;
constexpr uint32_t kDtcmBase = 0x027E0000;
constexpr uint32_t kIoSize = 0x2000;

struct Memory {
    std::vector<uint8_t> main_ram = std::vector<uint8_t>(kMainRamSize);
    std::vector<uint8_t> itcm = std::vector<uint8_t>(kItcmSize);
    std::vector<uint8_t> dtcm = std::vector<uint8_t>(kDtcmSize);
    std::vector<uint8_t> shared_wram = std::vector<uint8_t>(32u << 10);
    std::vector<uint8_t> palette = std::vector<uint8_t>(2u << 10);
    std::vector<uint8_t> vram = std::vector<uint8_t>(0x00A4000);  // LCDC view of banks A-I
    std::vector<uint8_t> oam = std::vector<uint8_t>(2u << 10);
    std::vector<uint8_t> io = std::vector<uint8_t>(kIoSize);
};

Memory& memory();
void memory_init();

// Raw little-endian accessors into host buffers.
inline uint32_t load32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
inline void store32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24); }

// Guest block copies used by HLE code.
void guest_copy_in(uint32_t dst, const void* src, uint32_t size);
void guest_copy_out(void* dst, uint32_t src, uint32_t size);

// --- MMIO -------------------------------------------------------------------

uint32_t io_read32(uint32_t addr);
void io_write32(uint32_t addr, uint32_t value, uint32_t mask);

// --- time / interrupts -------------------------------------------------------

uint64_t now_cycles();
void advance_time(uint64_t cycles);
uint64_t cycles_to_next_line();
uint32_t current_vcount();
bool in_hblank();
void raise_irq(uint32_t bits);
uint16_t timer_read_counter(int i);
void timer_write(int i, uint16_t reload, uint16_t ctrl);
bool irq_pending(const Cpu& c);       // IME, IE & IF and CPSR.I allow an IRQ now
void check_interrupts(Cpu& c);        // runs the IRQ handler if one is pending
void wait_for_interrupt(Cpu& c);      // halts until IE & IF != 0, then services it
void irq_leave_for_switch();
void irq_resume_after_switch();

// --- cpu / dispatch -----------------------------------------------------------

Cpu& cpu();
void cpu_reset(uint32_t mode);
void call_address(Cpu& c, uint32_t target);  // lookup + call, used by call_indirect
void overlay_set_active(uint32_t overlay_id, bool active);

// --- ROM ----------------------------------------------------------------------

struct Rom {
    std::vector<uint8_t> data;
};
Rom& rom();
bool rom_load(const std::string& path);

void trace_install();

// --- logging ------------------------------------------------------------------

#define NTR_LOG(...) std::fprintf(stderr, __VA_ARGS__)

}  // namespace ntr
