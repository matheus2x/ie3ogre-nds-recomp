// Cooperative fibers with a hand-written context switch.
//
// A suspended fiber keeps its callee-saved registers on its own stack; the Fiber only
// remembers the stack pointer. Stacks are slots of one region reserved at a fixed address,
// so a savestate (savestate.cpp) can copy the used part of every stack and put it back at
// the same place, in this run or in a later run of the same executable (boej is linked at
// a fixed base address, without ASLR). The OS thread's own stack, the main fiber, is never
// saved: the game runs on fibers from the region.
//
// On Windows the TIB's stack bounds follow the running fiber, as SwitchToFiber does:
// exception dispatch checks every frame against them.
#include "fiber.h"

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "runtime.h"

extern "C" {
void ntr_fiber_swap(void** save_sp, void* load_sp);
void ntr_fiber_start();
void ntr_fiber_main(ntr::Fiber* f);
}

// ntr_fiber_swap(save_sp, load_sp) pushes the callee-saved registers, stores sp in
// *save_sp, then pops the other fiber's registers from load_sp and returns on its stack.
// A new fiber's stack holds the same frame with ntr_fiber_start as the return address and
// the Fiber in rbx.
#ifdef _WIN32
// Win64: rbx, rbp, rdi, rsi, r12-r15, xmm6-xmm15, MXCSR and the x87 control word.
asm(R"(
    .text
    .def ntr_fiber_swap; .scl 2; .type 32; .endef
    .globl ntr_fiber_swap
    .p2align 4
ntr_fiber_swap:
    pushq %rbp
    pushq %rbx
    pushq %rdi
    pushq %rsi
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $168, %rsp
    movdqu %xmm6, 0(%rsp)
    movdqu %xmm7, 16(%rsp)
    movdqu %xmm8, 32(%rsp)
    movdqu %xmm9, 48(%rsp)
    movdqu %xmm10, 64(%rsp)
    movdqu %xmm11, 80(%rsp)
    movdqu %xmm12, 96(%rsp)
    movdqu %xmm13, 112(%rsp)
    movdqu %xmm14, 128(%rsp)
    movdqu %xmm15, 144(%rsp)
    stmxcsr 160(%rsp)
    fnstcw 164(%rsp)
    movq %rsp, (%rcx)
    movq %rdx, %rsp
    movdqu 0(%rsp), %xmm6
    movdqu 16(%rsp), %xmm7
    movdqu 32(%rsp), %xmm8
    movdqu 48(%rsp), %xmm9
    movdqu 64(%rsp), %xmm10
    movdqu 80(%rsp), %xmm11
    movdqu 96(%rsp), %xmm12
    movdqu 112(%rsp), %xmm13
    movdqu 128(%rsp), %xmm14
    movdqu 144(%rsp), %xmm15
    ldmxcsr 160(%rsp)
    fldcw 164(%rsp)
    addq $168, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rsi
    popq %rdi
    popq %rbx
    popq %rbp
    retq

    .def ntr_fiber_start; .scl 2; .type 32; .endef
    .globl ntr_fiber_start
    .p2align 4
ntr_fiber_start:
    movq %rbx, %rcx
    subq $32, %rsp
    callq ntr_fiber_main
    ud2
)");
#else
// System V: rbx, rbp, r12-r15, MXCSR and the x87 control word.
asm(R"(
    .text
    .globl ntr_fiber_swap
    .type ntr_fiber_swap, @function
    .p2align 4
ntr_fiber_swap:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    subq $8, %rsp
    stmxcsr 0(%rsp)
    fnstcw 4(%rsp)
    movq %rsp, (%rdi)
    movq %rsi, %rsp
    ldmxcsr 0(%rsp)
    fldcw 4(%rsp)
    addq $8, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    retq
    .size ntr_fiber_swap, .-ntr_fiber_swap

    .globl ntr_fiber_start
    .type ntr_fiber_start, @function
    .p2align 4
ntr_fiber_start:
    movq %rbx, %rdi
    callq ntr_fiber_main
    ud2
    .size ntr_fiber_start, .-ntr_fiber_start
)");
#endif

namespace ntr {

struct Fiber {
    void* sp = nullptr;        // while suspended
    uint8_t* stack = nullptr;  // [stack, stack + kFiberStackSize); null for the main fiber
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
    bool used = false;
    bool committed = false;
#ifdef _WIN32
    void* tib_base = nullptr;
    void* tib_limit = nullptr;
#endif
};

namespace {

constexpr int kSlots = 128;
constexpr size_t kGuard = 64u << 10;  // no access below each stack: an overflow faults
constexpr size_t kSlotSize = kGuard + kFiberStackSize;
constexpr uintptr_t kRegionAddress = 0x200000000000ull;  // 32 TiB: free on Windows and Linux

// Where the initial frame of a new fiber keeps the Fiber (the rbx slot) and its size.
#ifdef _WIN32
constexpr size_t kFrameRbx = 168 + 6 * 8, kFrameSize = 168 + 8 * 8;
#else
constexpr size_t kFrameRbx = 8 + 4 * 8, kFrameSize = 8 + 6 * 8;
#endif

Fiber g_main;
Fiber g_slots[kSlots];
Fiber* g_current = nullptr;
uint8_t* g_region = nullptr;

void reserve_region() {
    size_t size = kSlots * kSlotSize;
#ifdef _WIN32
    void* p = VirtualAlloc(reinterpret_cast<void*>(kRegionAddress), size, MEM_RESERVE, PAGE_NOACCESS);
    if (!p) p = VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
#else
    void* p = mmap(reinterpret_cast<void*>(kRegionAddress), size, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED) p = mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) p = nullptr;
#endif
    if (!p) {
        NTR_LOG("[fiber] cannot reserve %zu MiB for the fiber stacks\n", size >> 20);
        std::abort();
    }
    g_region = static_cast<uint8_t*>(p);
    if (uintptr_t(p) != kRegionAddress) {
        NTR_LOG("[fiber] stacks at %p instead of %p: savestates only load in this session\n", p,
                reinterpret_cast<void*>(kRegionAddress));
    }
    for (int i = 0; i < kSlots; i++) g_slots[i].stack = g_region + i * kSlotSize + kGuard;
}

void commit(Fiber& f) {
    if (f.committed) return;
#ifdef _WIN32
    bool ok = VirtualAlloc(f.stack, kFiberStackSize, MEM_COMMIT, PAGE_READWRITE) != nullptr;
    f.tib_base = f.stack + kFiberStackSize;
    f.tib_limit = f.stack;
#else
    bool ok = mprotect(f.stack, kFiberStackSize, PROT_READ | PROT_WRITE) == 0;
#endif
    if (!ok) {
        NTR_LOG("[fiber] cannot commit a fiber stack\n");
        std::abort();
    }
    f.committed = true;
}

uint8_t* stack_top(const Fiber& f) { return f.stack + kFiberStackSize; }

}  // namespace

}  // namespace ntr

extern "C" void ntr_fiber_main(ntr::Fiber* f) {
    f->entry(f->arg);
    std::abort();  // fiber entries must never return
}

namespace ntr {

Fiber* fiber_init_main() {
    if (!g_region) reserve_region();
    g_current = &g_main;
    return &g_main;
}

Fiber* fiber_create(void (*entry)(void*), void* arg, size_t stack_size) {
    if (stack_size > kFiberStackSize) {
        NTR_LOG("[fiber] stack of %zu bytes requested; the limit is %zu\n", stack_size, kFiberStackSize);
        std::abort();
    }
    for (Fiber& f : g_slots) {
        if (f.used) continue;
        commit(f);
        f.used = true;
        f.entry = entry;
        f.arg = arg;
        // The frame ntr_fiber_swap pops, returning to ntr_fiber_start with the stack 16-byte
        // aligned. The registers start at zero, with the default MXCSR and x87 control word.
        uint8_t* ret = stack_top(f) - 8;
        uint8_t* sp = ret - kFrameSize;
        std::memset(sp, 0, kFrameSize);
        uint32_t mxcsr = 0x1F80;
        uint16_t fpucw = 0x037F;
#ifdef _WIN32
        std::memcpy(sp + 160, &mxcsr, 4);
        std::memcpy(sp + 164, &fpucw, 2);
#else
        std::memcpy(sp, &mxcsr, 4);
        std::memcpy(sp + 4, &fpucw, 2);
#endif
        Fiber* self = &f;
        std::memcpy(sp + kFrameRbx, &self, 8);
        void (*start)() = ntr_fiber_start;
        std::memcpy(ret, &start, 8);
        f.sp = sp;
        return &f;
    }
    NTR_LOG("[fiber] all %d fibers are in use\n", kSlots);
    std::abort();
}

void fiber_switch(Fiber* to) {
    Fiber* from = g_current;
    if (from == to) return;
    g_current = to;
#ifdef _WIN32
    NT_TIB* tib = reinterpret_cast<NT_TIB*>(NtCurrentTeb());
    from->tib_base = tib->StackBase;
    from->tib_limit = tib->StackLimit;
    tib->StackBase = to->tib_base;
    tib->StackLimit = to->tib_limit;
#endif
    ntr_fiber_swap(&from->sp, to->sp);
}

void fiber_destroy(Fiber* f) {
    if (f && f != &g_main) f->used = false;  // the stack stays committed for the next fiber
}

Fiber* fiber_current() { return g_current; }

int fiber_index(const Fiber* f) {
    if (!f) return -1;
    if (f == &g_main) return -2;
    return int(f - g_slots);
}

Fiber* fiber_at(int index) {
    if (index == -2) return &g_main;
    if (index < 0 || index >= kSlots || !g_slots[index].used) return nullptr;
    return &g_slots[index];
}

uintptr_t fiber_region() { return uintptr_t(g_region); }

// Used fibers: entry, argument, stack pointer and the stack from there to the top. The
// switching fiber is the main one (savestate.cpp), so every fiber here is suspended.
void fiber_state(StateIo& io) {
    io.section("FIBR");
    for (Fiber& f : g_slots) {
        uint8_t used = f.used;
        io.value(used);
        if (io.loading()) f.used = false;
        if (!used) continue;
        uint64_t entry = uintptr_t(f.entry), arg = uintptr_t(f.arg), sp = uintptr_t(f.sp);
        io.value(entry);
        io.value(arg);
        io.value(sp);
        if (io.loading()) {
            commit(f);
            if (sp < uintptr_t(f.stack) || sp >= uintptr_t(stack_top(f))) {
                io.fail("fiber stack pointer outside its stack");
                return;
            }
            f.used = true;
            f.entry = reinterpret_cast<void (*)(void*)>(uintptr_t(entry));
            f.arg = reinterpret_cast<void*>(uintptr_t(arg));
            f.sp = reinterpret_cast<void*>(uintptr_t(sp));
        }
        io.bytes(f.sp, size_t(stack_top(f) - static_cast<uint8_t*>(f.sp)));
    }
}

}  // namespace ntr
