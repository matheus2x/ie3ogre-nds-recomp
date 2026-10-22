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

// --- VRAM banks -----------------------------------------------------------------
// Memory::vram holds banks A-I back to back in LCDC order. A view is one of the areas
// the 2D/3D engines read from, split in 16 KiB granules; unmapped granules are null.

enum class VramArea { ABg, BBg, AObj, BObj, Texture, TexPalette, ABgExtPal, BBgExtPal, AObjExtPal, BObjExtPal, Count };

struct VramView {
    uint8_t* granule[32] = {};
    uint32_t size = 0;  // bytes the area spans (accesses wrap at this size)

    uint8_t* ptr(uint32_t off) const {
        off %= size;
        uint8_t* g = granule[off >> 14];
        return g ? g + (off & 0x3FFF) : nullptr;
    }
    uint8_t read8(uint32_t off) const {
        const uint8_t* p = ptr(off);
        return p ? *p : 0;
    }
    uint16_t read16(uint32_t off) const {
        const uint8_t* p = ptr(off & ~1u);
        return p ? uint16_t(p[0] | p[1] << 8) : 0;
    }
};

const VramView& vram_view(VramArea area);
void vram_remap();  // after a VRAMCNT write: rebuilds the views and the CPU pages
uint8_t* vram_lcdc_bank(int bank);  // bank 0-3 (A-D) for display mode 2, null unless in LCDC mode

// --- video ----------------------------------------------------------------------

constexpr int kScreenWidth = 256;
constexpr int kScreenHeight = 192;

void gpu_init();
void gpu_render_line(uint32_t line);  // called as the LCD starts drawing a visible line
void gpu_vblank();                    // called as VBlank starts: presents the frame
void gpu_io_written(uint32_t addr);   // affine reference point writes reload the latch

// --- host frontend (SDL3) ---------------------------------------------------------

// Top and bottom physical screens, 256x192 XRGB8888 each.
void host_present(const uint32_t* top, const uint32_t* bottom);
uint16_t host_keyinput();  // REG_KEYINPUT, active low
uint16_t host_extkeys();   // ARM7 EXTKEYIN (X, Y, pen, hinge), active low
bool host_touch(int& x, int& y);  // bottom screen pixel, true while pressed

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

// The cartridge image by ROM offset (rom.cpp); bytes outside it read as 0xFF.
void rom_read(uint32_t offset, void* dst, uint32_t size);
uint32_t rom_read32(uint32_t offset);

void trace_install();
void trace_frame(uint64_t frame);  // NTR_PROFILE window bookkeeping

// --- logging ------------------------------------------------------------------

#define NTR_LOG(...) std::fprintf(stderr, __VA_ARGS__)

}  // namespace ntr
