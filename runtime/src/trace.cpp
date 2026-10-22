// Ring buffer of recently entered guest functions, dumped on SIGINT/SIGTERM. Only fed
// when the generated code is built with NTR_TRACE.
#include <csignal>
#include <cstdlib>
#include <unistd.h>

#include "runtime.h"

namespace ntr {

namespace {

constexpr uint32_t kRing = 256;
uint32_t g_ring[kRing];
uint64_t g_count = 0;

void dump_and_exit(int) {
    NTR_LOG("\n[trace] %llu function entries; most recent last:\n", (unsigned long long)g_count);
    uint64_t n = g_count < kRing ? g_count : kRing;
    for (uint64_t i = g_count - n; i < g_count; i++) NTR_LOG("  %08X\n", g_ring[i % kRing]);
    Cpu& c = cpu();
    for (int i = 0; i < 16; i++) NTR_LOG("  r%-2d %08X%s", i, c.r[i], i % 4 == 3 ? "\n" : "");
    _exit(4);
}

}  // namespace

void trace_enter(uint32_t addr) { g_ring[g_count++ % kRing] = addr; }

void trace_install() {
    std::signal(SIGINT, dump_and_exit);
    std::signal(SIGTERM, dump_and_exit);
}

}  // namespace ntr
