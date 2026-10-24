// Minimal cooperative fibers with their own context switch (x86-64, Windows and System V).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ntr {

class StateIo;
struct Fiber;

// The calling OS thread becomes a fiber; call once before any other fiber function.
Fiber* fiber_init_main();
// Stacks are at most 1 MiB (kFiberStackSize).
Fiber* fiber_create(void (*entry)(void*), void* arg, size_t stack_size);
Fiber* fiber_current();
void fiber_switch(Fiber* to);
void fiber_destroy(Fiber* f);  // f must not be the running fiber

constexpr size_t kFiberStackSize = 1u << 20;

// Savestates (savestate.cpp): fibers by index, and the stacks of every fiber but the
// main one. Loading puts each stack back at its address, so it must be the same build.
int fiber_index(const Fiber* f);  // -1 for null, -2 for the main fiber
Fiber* fiber_at(int index);       // the reverse; null unless the index names a fiber in use
uintptr_t fiber_region();         // where the stacks live (part of a savestate's identity)
void fiber_state(StateIo& io);

}  // namespace ntr
