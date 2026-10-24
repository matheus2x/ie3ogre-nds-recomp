// The game engine's cooperative tasks (main 0x0206EDC0-0x0206F0B0) on host fibers.
//
// Up to 16 tasks each run on their own guest stack. Task_RunAll resumes every live task
// once per frame; a task gives control back with Task_Yield (often through
// Task_YieldFrames) and ends with Task_Exit. The original switches by saving sp into a
// table and popping a return address from the other stack, which lands in the middle
// of Task_Yield (0x0206EE98) where recompiled code cannot be entered. Here each task is a
// fiber, and switching fibers takes the place of switching guest stacks.
//
// Guest state: 0x020B9D50 holds the current task index and 0x020B9D54 the saved sp of
// each task (0 = free slot). Task_Create builds the first frame on the new stack:
//   [resume address][r0..r11 for the task][entry]
// and the resume code pops r0-r11, then jumps to the entry (Task_Trampoline).
#include <cstdlib>

#include "fiber.h"
#include "runtime.h"

namespace ntr {

namespace {

constexpr uint32_t kCurrentTask = 0x020B9D50;
constexpr uint32_t kTaskSp = 0x020B9D54;
constexpr int kTasks = 16;
constexpr size_t kTaskStack = 1u << 20;

struct Task {
    Fiber* fiber = nullptr;
    uint32_t sp = 0;  // what we left in the sp table; anything else there is a new task
    Cpu regs{};
};
Task g_tasks[kTasks];

Fiber* g_scheduler = nullptr;
Cpu g_scheduler_regs{};
Fiber* g_finished = nullptr;  // a fiber cannot free itself; the scheduler does it

void restore(Cpu& c, const Cpu& from) {
    cpsr_write(c, cpsr_read(from), 0xFFFFFFFF);
    c = from;
}

void task_entry(void* arg) {
    int i = int(reinterpret_cast<intptr_t>(arg));
    Cpu& c = cpu();
    uint32_t sp = rd32(kTaskSp + i * 4) + 4;  // skip the resume address
    for (int r = 0; r < 12; r++, sp += 4) c.r[r] = rd32(sp);
    uint32_t entry = rd32(sp);
    c.r[2] = entry;
    c.r[13] = sp + 4;
    c.r[14] = 0;
    call_address(c, entry);
    unhandled(c, entry, "task entry returned instead of calling Task_Exit");
}

void run_task(Cpu& c, int i) {
    Task& t = g_tasks[i];
    uint32_t sp = rd32(kTaskSp + i * 4);
    if (t.fiber && sp != t.sp) {  // slot reused: the old task is gone
        fiber_destroy(t.fiber);
        t.fiber = nullptr;
    }
    g_scheduler = fiber_current();
    g_scheduler_regs = c;
    if (!t.fiber) t.fiber = fiber_create(task_entry, reinterpret_cast<void*>(intptr_t(i)), kTaskStack);
    fiber_switch(t.fiber);
    restore(c, g_scheduler_regs);
    if (g_finished) {
        fiber_destroy(g_finished);
        g_finished = nullptr;
    }
}

}  // namespace

// A thread interrupted while one of its tasks ran is resumed on the task's fiber.
bool task_uses_fiber(const Fiber* f) {
    if (f == g_scheduler) return true;
    for (const Task& t : g_tasks) {
        if (t.fiber == f) return true;
    }
    return false;
}

void task_state(StateIo& io) {
    io.section("TASK");
    for (Task& t : g_tasks) {
        int32_t fiber = fiber_index(t.fiber);
        io.value(fiber);
        io.value(t.sp);
        io.value(t.regs);
        if (io.loading()) t.fiber = fiber_at(fiber);
    }
    int32_t scheduler = fiber_index(g_scheduler), finished = fiber_index(g_finished);
    io.value(scheduler);
    io.value(g_scheduler_regs);
    io.value(finished);
    if (io.loading()) {
        g_scheduler = fiber_at(scheduler);
        g_finished = fiber_at(finished);
    }
}

void hle_task_run_all(Cpu& c, GuestFn) {
    for (int i = 0; i < kTasks; i++) {
        wr32(kCurrentTask, i);
        if (rd32(kTaskSp + i * 4) != 0) run_task(c, i);
    }
}

void hle_task_yield(Cpu& c, GuestFn) {
    int i = int(rd32(kCurrentTask));
    if (i < 0 || i >= kTasks || !g_scheduler || g_tasks[i].fiber != fiber_current()) {
        unhandled(c, c.r[14], "Task_Yield outside a task");
    }
    Task& t = g_tasks[i];
    // Like the original: 13 registers and the resume address go below sp.
    t.sp = c.r[13] - 14 * 4;
    wr32(kTaskSp + i * 4, t.sp);
    t.regs = c;
    fiber_switch(g_scheduler);
    restore(c, t.regs);
}

void hle_task_exit(Cpu& c, GuestFn) {
    int i = int(rd32(kCurrentTask));
    if (i < 0 || i >= kTasks || g_tasks[i].fiber != fiber_current()) {
        unhandled(c, c.r[14], "Task_Exit outside a task");
    }
    wr32(kTaskSp + i * 4, 0);
    g_finished = g_tasks[i].fiber;
    g_tasks[i] = Task{};
    fiber_switch(g_scheduler);
    std::abort();  // a finished task is never resumed
}

}  // namespace ntr
