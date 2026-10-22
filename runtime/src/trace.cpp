// Ring buffer of recently entered guest functions, dumped on SIGINT/SIGTERM. Only fed
// when the generated code is built with NTR_TRACE.
//
// NTR_PROFILE=first-last also counts function entries during those frames and prints
// the most frequent ones when the last frame ends.
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

#include "runtime.h"

namespace ntr {

namespace {

constexpr uint32_t kRing = 256;
uint32_t g_ring[kRing];
uint64_t g_count = 0;

bool g_profiling = false;
unsigned long long g_profile_first = 0, g_profile_last = 0;
std::unordered_map<uint32_t, uint64_t> g_profile;

void print_profile() {
    std::vector<std::pair<uint32_t, uint64_t>> rows(g_profile.begin(), g_profile.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    NTR_LOG("[profile] frames %llu-%llu, %zu functions:\n", g_profile_first, g_profile_last, rows.size());
    for (size_t i = 0; i < rows.size() && i < 1000; i++) {
        NTR_LOG("  %08X %llu\n", rows[i].first, (unsigned long long)rows[i].second);
    }
}

void dump_and_exit(int) {
    NTR_LOG("\n[trace] %llu function entries; most recent last:\n", (unsigned long long)g_count);
    uint64_t n = g_count < kRing ? g_count : kRing;
    for (uint64_t i = g_count - n; i < g_count; i++) NTR_LOG("  %08X\n", g_ring[i % kRing]);
    Cpu& c = cpu();
    for (int i = 0; i < 16; i++) NTR_LOG("  r%-2d %08X%s", i, c.r[i], i % 4 == 3 ? "\n" : "");
    std::_Exit(4);
}

}  // namespace

void trace_enter(uint32_t addr) {
    g_ring[g_count++ % kRing] = addr;
    if (g_profiling) g_profile[addr]++;
}

void trace_frame(uint64_t frame) {
    if (!g_profile_last) return;
    g_profiling = frame >= g_profile_first && frame < g_profile_last;
    if (frame == g_profile_last) print_profile();
}

void trace_install() {
    std::signal(SIGINT, dump_and_exit);
    std::signal(SIGTERM, dump_and_exit);
    if (const char* p = std::getenv("NTR_PROFILE")) std::sscanf(p, "%llu-%llu", &g_profile_first, &g_profile_last);
}

}  // namespace ntr
