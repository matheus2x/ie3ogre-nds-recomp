// Interrupt delivery. The BIOS IRQ vector saves registers and calls the handler whose
// address the game stores at DTCM+0x3FFC (OS_IrqHandler); we do the same as a host call.
#include <cstdlib>

#include "runtime.h"

namespace ntr {

namespace {

uint32_t io32(uint32_t off) { return load32(memory().io.data() + off); }

constexpr uint32_t kIrqHandlerSlot = kDtcmBase + 0x3FFC;
bool g_in_irq = false;

}  // namespace

bool irq_pending(const Cpu& c) {
    return !c.irq_off && (io32(0x208) & 1) && (io32(0x210) & io32(0x214));
}

void check_interrupts(Cpu& c) {
    if (g_in_irq || !irq_pending(c)) return;
    g_in_irq = true;
    if (std::getenv("NTR_LOG_IRQ")) {
        uint32_t pending = io32(0x210) & io32(0x214);
        uint32_t bit = 31 - __builtin_clz(pending & -pending);
        NTR_LOG("[irq] IE %08X IF %08X -> bit %u, table entry %08X\n", io32(0x210), io32(0x214), bit,
                rd32(kDtcmBase + bit * 4));
    }
    Cpu saved = c;
    uint32_t cpsr = cpsr_read(c);
    cpsr_write(c, (cpsr & ~0x3Fu) | 0x80 | 0x12, 0xFFFFFFFF);  // IRQ mode, IRQs off, ARM state
    c.spsr = cpsr;
    // The BIOS pushes r0-r3, r12 and lr on the IRQ stack before calling the handler.
    c.r[13] -= 24;
    c.r[0] = 0x04000000;
    c.r[14] = 0xFFFF0290;  // inside the BIOS IRQ vector; the handler returns there
    call_address(c, rd32(kIrqHandlerSlot));
    c.r[13] += 24;
    // Back to the interrupted mode; every register comes from the snapshot.
    cpsr_write(c, cpsr, 0xFFFFFFFF);
    for (int i = 0; i < 16; i++) c.r[i] = saved.r[i];
    c.n = saved.n;
    c.z = saved.z;
    c.c = saved.c;
    c.v = saved.v;
    c.q = saved.q;
    g_in_irq = false;
}

// A thread switch inside an IRQ ends the IRQ for the thread being left; the new thread
// must be able to take interrupts. The old one re-enters the handler when resumed.
void irq_leave_for_switch() { g_in_irq = false; }
void irq_resume_after_switch() { g_in_irq = true; }

void wait_for_interrupt(Cpu& c) {
    // Jump ahead scanline by scanline until a requested interrupt is pending.
    for (int guard = 0; guard < 263 * 600; guard++) {
        if (io32(0x210) & io32(0x214)) break;
        advance_time(cycles_to_next_line());
    }
    check_interrupts(c);
}

}  // namespace ntr
