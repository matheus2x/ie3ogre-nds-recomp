// Emulated time: ARM9 bus cycles (33.514 MHz), the LCD scanline counter and the four
// hardware timers. Generated code only advances time through poll() and HLE waits, so
// these values are approximate but monotonic. Line starts drive the 2D engines
// (gpu2d.cpp), and the start of VBlank presents the frame and runs the ARM7 per-frame work.
#include "pxi.h"
#include "runtime.h"

namespace ntr {

namespace {

constexpr uint32_t kCyclesPerLine = 2130;  // 355 dots * 6 cycles
constexpr uint32_t kLines = 263;
constexpr uint32_t kHDrawCycles = 1536;    // 256 dots * 6 cycles
constexpr int32_t kPollBudget = 64;        // backward branches between polls
constexpr uint32_t kCyclesPerPoll = 512;

constexpr uint32_t IRQ_VBLANK = 1u << 0, IRQ_HBLANK = 1u << 1, IRQ_VCOUNT = 1u << 2, IRQ_TIMER0 = 1u << 3;

uint64_t g_cycles = 0;

struct Timer {
    uint16_t reload = 0;
    uint16_t ctrl = 0;
    uint64_t start = 0;     // cycle when the counter last held `base`
    uint32_t base = 0;
};
Timer g_timers[4];

uint32_t io16(uint32_t addr) {
    uint32_t off = addr - 0x04000000;
    return memory().io[off] | memory().io[off + 1] << 8;
}

uint32_t prescale(uint16_t ctrl) {
    static const uint32_t kShift[4] = {0, 6, 8, 10};
    return kShift[ctrl & 3];
}

// Ticks of timer i since `start`, ignoring count-up (cascade) timers.
uint64_t timer_ticks(const Timer& t, uint64_t now) { return (now - t.start) >> prescale(t.ctrl); }

void advance_timers(uint64_t now) {
    for (int i = 0; i < 4; i++) {
        Timer& t = g_timers[i];
        if (!(t.ctrl & 0x80) || (t.ctrl & 0x04)) continue;
        uint64_t total = t.base + timer_ticks(t, now);
        if (total < 0x10000) continue;
        uint32_t period = 0x10000 - t.reload;
        uint64_t over = total - 0x10000;
        t.base = t.reload + uint32_t(over % period);
        t.start = now - ((now - t.start) & ((1u << prescale(t.ctrl)) - 1));
        if (t.ctrl & 0x40) raise_irq(IRQ_TIMER0 << i);
    }
}

}  // namespace

uint64_t now_cycles() { return g_cycles; }

uint32_t current_vcount() { return uint32_t((g_cycles / kCyclesPerLine) % kLines); }

bool in_hblank() { return g_cycles % kCyclesPerLine >= kHDrawCycles; }

void raise_irq(uint32_t bits) {
    uint8_t* p = memory().io.data() + 0x214;
    store32(p, load32(p) | bits);
}

void advance_time(uint64_t cycles) {
    uint64_t start = g_cycles;
    uint64_t end = g_cycles + cycles;
    uint32_t dispstat = io16(0x04000004);
    uint32_t vmatch = ((dispstat >> 8) & 0xFF) | ((dispstat & 0x80) << 1);
    // Edges crossed in (start, end]: line starts (VBlank, VCount match) and HBlank starts.
    for (uint64_t line = start / kCyclesPerLine; line * kCyclesPerLine <= end; line++) {
        uint64_t line_start = line * kCyclesPerLine;
        uint32_t v = uint32_t(line % kLines);
        if (line_start > start) {
            if (v == 0) gpu3d_frame_start();  // the 3D frame drawn as the hardware does, for this frame
            if (v < 192) gpu_render_line(v);
            gpu3d_irq_check();
            if (v == 192) {
                gpu_vblank();
                arm7_vblank();
                sound_vblank();
                if (dispstat & 0x08) raise_irq(IRQ_VBLANK);
            }
            if (v == vmatch && (dispstat & 0x20)) raise_irq(IRQ_VCOUNT);
        }
        uint64_t hblank = line_start + kHDrawCycles;
        if (hblank > start && hblank <= end && (dispstat & 0x10)) raise_irq(IRQ_HBLANK);
    }
    g_cycles = end;
    advance_timers(end);
    sound_advance();
}

uint64_t cycles_to_next_line() { return kCyclesPerLine - g_cycles % kCyclesPerLine; }

uint16_t timer_read_counter(int i) {
    advance_timers(g_cycles);
    const Timer& t = g_timers[i];
    if (!(t.ctrl & 0x80) || (t.ctrl & 0x04)) return uint16_t(t.base);
    return uint16_t(t.base + timer_ticks(t, g_cycles));
}

void timer_write(int i, uint16_t reload, uint16_t ctrl) {
    Timer& t = g_timers[i];
    advance_timers(g_cycles);
    bool was_running = t.ctrl & 0x80;
    if (was_running) t.base = timer_read_counter(i);
    t.reload = reload;
    if (!was_running && (ctrl & 0x80)) t.base = reload;  // starting reloads the counter
    t.ctrl = ctrl;
    t.start = g_cycles;
}

void timing_state(StateIo& io) {
    io.section("TIME");
    io.value(g_cycles);
    io.value(g_timers);
}

void poll(Cpu& c) {
    c.budget = kPollBudget;
    advance_time(kCyclesPerPoll);
    check_interrupts(c);
    if (g_savestate_pending) savestate_poll();
}

}  // namespace ntr
