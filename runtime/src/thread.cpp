// OS thread switching on host fibers.
//
// The NitroSDK switches threads with OS_SaveContext/OS_LoadContext, which rewrite sp
// and pc. Recompiled code cannot do that, so each guest thread runs on its own host
// fiber. A context that was saved remembers the fiber that saved it and the CPU state
// at that point; loading it restores that state and resumes the fiber. A context that
// was never saved (a new thread) starts a new fiber at the context's pc.
//
// OSContext layout (NitroSDK): +0 cpsr, +4 r0-r12, +0x38 sp, +0x3C lr, +0x40 pc+4,
// +0x44 sp_svc, +0x48 CP (divider) context.
#include <cstdlib>
#include <unordered_map>

#include "fiber.h"
#include "runtime.h"

namespace ntr {

namespace {

constexpr size_t kFiberStack = 1u << 20;

struct Saved {
    Fiber* fiber = nullptr;
    Cpu regs{};
};

std::unordered_map<uint32_t, Saved> g_saved;

void restore_regs(Cpu& c, const Cpu& from) {
    cpsr_write(c, cpsr_read(from), 0xFFFFFFFF);
    c = from;
}

// Entry of a fiber for a context that was set up by OS_InitContext.
void thread_entry(void* arg) {
    uint32_t ctx = uint32_t(reinterpret_cast<uintptr_t>(arg));
    Cpu& c = cpu();
    cpsr_write(c, rd32(ctx), 0xFFFFFFFF);
    for (int i = 0; i < 13; i++) c.r[i] = rd32(ctx + 4 + i * 4);
    c.r[13] = rd32(ctx + 0x38);
    c.r[14] = rd32(ctx + 0x3C);
    uint32_t pc = rd32(ctx + 0x40) - 4;
    if (rd32(ctx) & 0x20) pc |= 1;
    uint32_t exit = c.r[14];
    NTR_LOG("[thread] context %08X starts at %08X\n", ctx, pc);
    call_address(c, pc);
    // Returning from the thread function goes to the lr set up by OS_CreateThread.
    call_address(c, exit);
    unhandled(c, exit, "thread exit routine returned");
}

}  // namespace

void hle_os_load_context(Cpu& c, GuestFn);

// OS_SaveContext(OSContext* ctx): returns 0 now, 1 when the context is loaded again.
void hle_os_save_context(Cpu& c, GuestFn) {
    uint32_t ctx = c.r[0];
    if (std::getenv("NTR_LOG_THREAD")) NTR_LOG("[thread] save %08X\n", ctx);
    Saved& s = g_saved[ctx];
    s.fiber = fiber_current();
    s.regs = c;
    s.regs.r[0] = 1;
    // Keep the guest-visible copy coherent for code that inspects contexts.
    wr32(ctx, cpsr_read(c));
    for (int i = 0; i < 13; i++) wr32(ctx + 4 + i * 4, c.r[i]);
    wr32(ctx + 4, 1);
    wr32(ctx + 0x38, c.r[13]);
    wr32(ctx + 0x3C, c.r[14]);
    c.r[0] = 0;
}

// OS_LoadContext(OSContext* ctx): does not return to the caller's thread.
void hle_os_load_context(Cpu& c, GuestFn) {
    uint32_t ctx = c.r[0];
    if (std::getenv("NTR_LOG_THREAD")) NTR_LOG("[thread] load %08X\n", ctx);
    auto it = g_saved.find(ctx);
    if (it == g_saved.end()) {
        Saved& s = g_saved[ctx];
        s.fiber = fiber_create(thread_entry, reinterpret_cast<void*>(uintptr_t(ctx)), kFiberStack);
        fiber_switch(s.fiber);
        return;
    }
    Saved s = it->second;
    restore_regs(c, s.regs);
    fiber_switch(s.fiber);
}

namespace {

// Addresses the original handler takes from its literal pool (ITCM).
uint32_t irq_table() { return rd32(0x01FF8050); }         // OS_IRQTable in DTCM
uint32_t irq_thread_queue() { return rd32(0x01FF81A8); }  // threads sleeping in OS_WaitIrq
uint32_t thread_info() { return rd32(0x01FF81AC); }        // OSThreadInfo

// Port of OS_IrqHandler_ThreadSwitch: wakes OS_WaitIrq sleepers, then switches to the
// highest-priority ready thread if the scheduler asked for it.
void irq_thread_switch(Cpu& c) {
    uint32_t queue = irq_thread_queue();
    uint32_t info = thread_info();
    if (uint32_t t = rd32(queue)) {
        while (t) {
            wr32(t + 0x64, 1);  // OS_THREAD_STATE_READY
            wr32(t + 0x78, 0);
            wr32(t + 0x7C, 0);
            uint32_t next = rd32(t + 0x80);
            wr32(t + 0x80, 0);
            t = next;
        }
        wr32(queue, 0);
        wr32(queue + 4, 0);
        wr16(info, 1);
    }
    if (rd16(info) == 0) return;
    wr16(info, 0);

    uint32_t next = rd32(info + 8);
    while (next && rd16(next + 0x64) != 1) next = rd32(next + 0x68);
    uint32_t current = rd32(info + 4);
    if (std::getenv("NTR_LOG_THREAD")) NTR_LOG("[thread] irq switch %08X -> %08X\n", current, next);
    if (!next || next == current) return;
    if (uint32_t callback = rd32(info + 0xC)) {
        Cpu saved = c;
        c.r[0] = current;
        c.r[1] = next;
        call_address(c, callback);
        c = saved;
    }
    wr32(info + 4, next);

    // The interrupted thread resumes here when its context is loaded again.
    Saved& s = g_saved[current];
    s.fiber = fiber_current();
    s.regs = c;
    irq_leave_for_switch();
    c.r[0] = next;
    hle_os_load_context(c, nullptr);
    irq_resume_after_switch();
}

}  // namespace

// OS_IrqHandler: acknowledges the lowest pending IRQ, runs its handler from OS_IRQTable,
// then does what OS_IrqHandler_ThreadSwitch does.
void hle_os_irq_handler(Cpu& c, GuestFn) {
    uint8_t* io = memory().io.data();
    if (!(load32(io + 0x208) & 1)) return;
    uint32_t pending = load32(io + 0x210) & load32(io + 0x214);
    if (!pending) return;
    uint32_t bit = __builtin_ctz(pending);
    store32(io + 0x214, load32(io + 0x214) & ~(1u << bit));
    call_address(c, rd32(irq_table() + bit * 4));
    irq_thread_switch(c);
}

// OS_InitContext(ctx, func, stack): a reused OSThread starts over on a new fiber.
void hle_os_init_context(Cpu& c, GuestFn orig) {
    g_saved.erase(c.r[0]);
    orig(c);
}

}  // namespace ntr
