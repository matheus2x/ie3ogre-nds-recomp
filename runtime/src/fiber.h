// Minimal cooperative fibers: ucontext on POSIX, the Fibers API on Windows.
#pragma once

#include <cstddef>

namespace ntr {

struct Fiber;

// The calling OS thread becomes a fiber; call once before any other fiber function.
Fiber* fiber_init_main();
Fiber* fiber_create(void (*entry)(void*), void* arg, size_t stack_size);
Fiber* fiber_current();
void fiber_switch(Fiber* to);

}  // namespace ntr
