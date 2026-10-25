// Runtime internals shared by the runtime sources (not used by generated code).
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include <ntr/common.h>
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
        if (off >= size) off %= size;  // the division is rare; this runs per texel
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
void gpu3d_frame_start();                            // renders the list of SWAP_BUFFERS and shows it
void gpu3d_irq_check();                              // GXSTAT FIFO IRQ
const uint16_t* gpu3d_line_color(uint32_t line);     // BGR555, native size
const uint8_t* gpu3d_line_alpha(uint32_t line);      // 0-31; 0 = no 3D pixel
// Internal resolution of the 3D engine (1 = native). The rows below are 256 * scale
// pixels wide; there are 192 * scale of them.
constexpr int kMax3dScale = 10;
void gpu3d_set_scale(int scale);
int gpu3d_scale();
const uint16_t* gpu3d_hi_row_color(uint32_t row);
const uint8_t* gpu3d_hi_row_alpha(uint32_t row);

// --- host frontend (SDL3) ---------------------------------------------------------

// Top and bottom physical screens, XRGB8888, each 256 * scale by 192 * scale pixels (a
// screen that shows 3D comes at the 3D resolution, the other one at native size).
void host_present(const uint32_t* top, int top_scale, const uint32_t* bottom, int bottom_scale);
bool host_fast_forward();  // fast forward is on (fewer frames reach the window)
const Config& host_config();  // boej.ini over the defaults (defaults only without a window)
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
bool overlay_active_at(uint32_t overlay_id, uint32_t addr);  // the page of addr runs this overlay
// Runs ARM code at `entry` in the interpreter (interp.cpp) until it returns to c.r[14].
void interpret_arm(Cpu& c, uint32_t entry);

// --- ROM ----------------------------------------------------------------------

// The cartridge image by ROM offset (rom.cpp); bytes outside it read as 0xFF.
void rom_read(uint32_t offset, void* dst, uint32_t size);
uint32_t rom_read32(uint32_t offset);
// The image itself, without the mods.
class RomImage;
const RomImage& rom_image();
// Reads of [offset, offset + size) come from `data` (kept alive by the caller). Ranges
// must not overlap.
void rom_override(uint32_t offset, const uint8_t* data, uint32_t size);
// The same, with the bytes made on the first read that needs them (a zipped mod file).
void rom_override_lazy(uint32_t offset, uint32_t size, std::function<const uint8_t*()> load);

// --- mods (mods.cpp) ----------------------------------------------------------

// mods_apply (ntr/runtime.h) lays the mods enabled in the settings ([mods] enabled, or
// NTR_MODS) over the cartridge image. Problems go to the log; a mod with errors is left
// out whole.
// The mods in use and their files, for the savestate identity (0 without mods).
uint64_t mods_fingerprint();

void trace_install();
void trace_frame(uint64_t frame);  // NTR_PROFILE window bookkeeping

// --- logging ------------------------------------------------------------------

// Terminal and log file (diag.cpp); log_detail goes to the file only.
void log_detail(const char* fmt, ...) NTR_PRINTF_FORMAT;
void log_disable_file();  // terminal only (boej --check-mods must not replace boej.log)

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
void log_close();  // before handing the log files to a restarted game
const std::string& rtc_start_text();  // "YYYY-MM-DD HH:MM:SS" the clock started from (arm7_rtc.cpp)
void rtc_request_start(const std::string& text);  // before boot: start the clock there

// --- savestates (savestate.cpp) ---------------------------------------------------------

struct Fiber;

// One pass over the emulator's state, writing it to a buffer or reading it back in place.
// Every module with state has a <module>_state(StateIo&) listing its fields in a fixed
// order; the same function serves both directions. After a failed read the state is
// partly overwritten: the caller puts back a copy taken before.
class StateIo {
public:
    StateIo(std::vector<uint8_t>& data, bool loading) : data_(data), loading_(loading) {}
    bool loading() const { return loading_; }
    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    void fail(const std::string& why) {
        if (error_.empty()) error_ = why;
    }

    void bytes(void* p, size_t n) {
        if (!ok()) return;
        if (!loading_) {
            const uint8_t* b = static_cast<const uint8_t*>(p);
            data_.insert(data_.end(), b, b + n);
        } else if (n > data_.size() - pos_) {
            fail("truncated");
        } else {
            std::memcpy(p, data_.data() + pos_, n);
            pos_ += n;
        }
    }
    template <class T>
    void value(T& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        bytes(&v, sizeof v);
    }
    template <class T>
    void vector(std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        uint64_t n = v.size();
        value(n);
        if (loading_) {
            if (!ok() || n > (data_.size() - pos_) / sizeof(T)) return fail("truncated");
            v.resize(size_t(n));
        }
        bytes(v.data(), size_t(n) * sizeof(T));
    }
    template <class T>
    void deque(std::deque<T>& d) {
        std::vector<T> v(d.begin(), d.end());
        vector(v);
        if (loading_) d.assign(v.begin(), v.end());
    }
    // A four-letter marker between modules: a mismatch tells which one went wrong.
    void section(const char (&tag)[5]) {
        char got[4];
        std::memcpy(got, tag, 4);
        bytes(got, 4);
        if (loading_ && ok() && std::memcmp(got, tag, 4) != 0) fail(std::string("section ") + tag + " not found");
    }

private:
    std::vector<uint8_t>& data_;
    bool loading_;
    size_t pos_ = 0;
    std::string error_;
};

void memory_state(StateIo& io);
void cpu_state(StateIo& io);
void timing_state(StateIo& io);
void irq_state(StateIo& io);
void thread_state(StateIo& io);
void task_state(StateIo& io);
void io_state(StateIo& io);
void pxi_state(StateIo& io);
void gpu2d_state(StateIo& io);
void gpu3d_state(StateIo& io);
void sound_state(StateIo& io);
void card_state(StateIo& io);
void rtc_state(StateIo& io);

// Runs the game on `game` and does, on this (the main) fiber, the savestate work the
// game's fibers hand over. Does not return.
[[noreturn]] void savestate_run(Fiber* game);
// Called by poll(): when a save or load is pending, hands control to the main fiber.
void savestate_poll();
extern bool g_savestate_pending;

// Requests from the host (keyboard shortcut, pause menu); they run at the next poll.
constexpr int kStateSlots = 5;
void savestate_request_save(int slot);  // 1..kStateSlots
void savestate_request_load(int slot);
// What a slot holds, for the menu: "" if empty, else the date and time it was saved (or
// why it cannot be loaded).
std::string savestate_describe(int slot);
// Set by a finished save or load, for the host's on-screen message; cleared by the host.
extern std::string g_savestate_message;
extern bool g_savestate_loaded;  // a load replaced the state: the host resets its pacing and sound

}  // namespace ntr
