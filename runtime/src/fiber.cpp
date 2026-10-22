#include "fiber.h"

#include <cstdint>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <ucontext.h>
#endif

namespace ntr {

namespace {
thread_local Fiber* g_current = nullptr;
}

#ifdef _WIN32

struct Fiber {
    void* handle = nullptr;
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
};

namespace {
void CALLBACK fiber_trampoline(void* param) {
    Fiber* f = static_cast<Fiber*>(param);
    f->entry(f->arg);
    std::abort();  // fiber entries must never return
}
}  // namespace

Fiber* fiber_init_main() {
    auto* f = new Fiber;
    f->handle = ConvertThreadToFiber(nullptr);
    g_current = f;
    return f;
}

Fiber* fiber_create(void (*entry)(void*), void* arg, size_t stack_size) {
    auto* f = new Fiber;
    f->entry = entry;
    f->arg = arg;
    f->handle = CreateFiber(stack_size, fiber_trampoline, f);
    return f;
}

void fiber_switch(Fiber* to) {
    g_current = to;
    SwitchToFiber(to->handle);
}

#else

struct Fiber {
    ucontext_t ctx;
    void* stack = nullptr;
    void (*entry)(void*) = nullptr;
    void* arg = nullptr;
};

namespace {
void fiber_trampoline(unsigned lo, unsigned hi) {
    Fiber* f = reinterpret_cast<Fiber*>(uintptr_t(lo) | uintptr_t(hi) << 32);
    f->entry(f->arg);
    std::abort();  // fiber entries must never return
}
}  // namespace

Fiber* fiber_init_main() {
    auto* f = new Fiber;
    getcontext(&f->ctx);
    g_current = f;
    return f;
}

Fiber* fiber_create(void (*entry)(void*), void* arg, size_t stack_size) {
    auto* f = new Fiber;
    f->entry = entry;
    f->arg = arg;
    f->stack = std::malloc(stack_size);
    getcontext(&f->ctx);
    f->ctx.uc_stack.ss_sp = f->stack;
    f->ctx.uc_stack.ss_size = stack_size;
    f->ctx.uc_link = nullptr;
    uintptr_t p = reinterpret_cast<uintptr_t>(f);
    makecontext(&f->ctx, reinterpret_cast<void (*)()>(fiber_trampoline), 2, unsigned(p), unsigned(p >> 32));
    return f;
}

void fiber_switch(Fiber* to) {
    Fiber* from = g_current;
    if (from == to) return;
    g_current = to;
    swapcontext(&from->ctx, &to->ctx);
}

#endif

Fiber* fiber_current() { return g_current; }

}  // namespace ntr
