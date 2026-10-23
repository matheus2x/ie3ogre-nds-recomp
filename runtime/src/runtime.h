// Runtime internals shared by the runtime sources (not used by generated code).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <ntr/guest.h>

// printf-style checking; MinGW's default "printf" archetype is the old MSVCRT one.
#ifndef NTR_PRINTF_FORMAT
#ifdef __MINGW32__
#define NTR_PRINTF_FORMAT __attribute__((format(gnu_printf, 1, 2)))
#else
#define NTR_PRINTF_FORMAT __attribute__((format(printf, 1, 2)))
#endif
#endif

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

void gpu3d_init();
void gpu3d_write(uint32_t addr, uint32_t value);     // GX FIFO and command ports
void gpu3d_write_gxstat(uint32_t value);
uint32_t gpu3d_read(uint32_t addr, uint32_t stored);  // GXSTAT, RAM_COUNT, test/matrix results
void gpu3d_vblank();                                 // shows the frame rendered at SWAP_BUFFERS
void gpu3d_irq_check();                              // GXSTAT FIFO IRQ
const uint16_t* gpu3d_line_color(uint32_t line);     // BGR555, native size
const uint8_t* gpu3d_line_alpha(uint32_t line);      // 0-31; 0 = no 3D pixel
// Internal resolution of the 3D engine (1 = native). The rows below are 256 * scale
// pixels wide; there are 192 * scale of them.
constexpr int kMax3dScale = 4;
void gpu3d_set_scale(int scale);
int gpu3d_scale();
const uint16_t* gpu3d_hi_row_color(uint32_t row);
const uint8_t* gpu3d_hi_row_alpha(uint32_t row);

// --- host frontend (SDL3) ---------------------------------------------------------

// Top and bottom physical screens, 256x192 XRGB8888 each.
// The two screens as the window shows them: 256 * scale by 192 * scale pixels each.
void host_present(const uint32_t* top, const uint32_t* bottom, int scale);
bool host_fast_forward();  // fast forward is on (fewer frames reach the window)
uint16_t host_keyinput();  // REG_KEYINPUT, active low
uint16_t host_extkeys();   // ARM7 EXTKEYIN (X, Y, pen, hinge), active low
bool host_touch(int& x, int& y);  // bottom screen pixel, true while pressed
void host_audio_push(const int16_t* frames, size_t count);  // stereo, 32768 Hz

// --- sound (arm7_sound.cpp) ------------------------------------------------------------

constexpr int kSoundRate = 32768;
void sound_advance();  // mixes up to the current emulated time, delivers SND alarms
void sound_vblank();   // hands the frame's samples to the host

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
void overlay_set_active(uint32_t overlay_id, bool active);  // per 4 KiB page; the last loaded wins
// Runs ARM code at `entry` in the interpreter (interp.cpp) until it returns to c.r[14].
void interpret_arm(Cpu& c, uint32_t entry);

// --- ROM ----------------------------------------------------------------------

// The cartridge image by ROM offset (rom.cpp); bytes outside it read as 0xFF.
void rom_read(uint32_t offset, void* dst, uint32_t size);
uint32_t rom_read32(uint32_t offset);

void trace_install();
void trace_frame(uint64_t frame);  // NTR_PROFILE window bookkeeping

// --- logging ------------------------------------------------------------------

// Terminal and log file (diag.cpp); log_detail goes to the file only.
void log_printf(const char* fmt, ...) NTR_PRINTF_FORMAT;
void log_detail(const char* fmt, ...) NTR_PRINTF_FORMAT;
#define NTR_LOG(...) ::ntr::log_printf(__VA_ARGS__)

// --- diagnostics (diag.cpp) ----------------------------------------------------------

void diag_init(bool windowed);  // crash handlers; the stall watchdog when windowed
void diag_frame(uint64_t frame);  // every presented frame
void debug_dump(const char* reason);  // display/IRQ registers, overlays and threads to the log
std::string overlay_active_list();
// Session recording (boej-replay.txt + boej-replay.sav next to the executable).
void replay_enable();  // by the host, before boot; the rest are no-ops otherwise
void replay_start(const std::string& rtc_start);
void replay_initial_save(const std::vector<uint8_t>& data, bool existed);
void replay_record(uint64_t frame, uint16_t keys, bool touch, int x, int y);
void replay_finish(uint64_t frame);
const std::string& rtc_start_text();  // "YYYY-MM-DD HH:MM:SS" the clock started from (arm7_rtc.cpp)
void rtc_request_start(const std::string& text);  // before boot: start the clock there

}  // namespace ntr
