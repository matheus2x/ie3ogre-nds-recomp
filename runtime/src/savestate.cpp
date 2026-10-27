// Savestates: the whole emulator state in a file, 5 slots in savestates/ next to the
// executable (savestates/boej-slot1.state ...).
//
// The guest state alone is not enough: recompiled code keeps the guest's call chain on the
// host stacks of the fibers that run the game's threads and tasks. So a savestate also
// holds those stacks (fiber.cpp), and loading puts them back at the same addresses. That
// only works with the same executable at the same address: the file records an identity
// of the build (and of where its code and stacks are, and of the mods in use) and is
// refused by any other build.
//
// Saving and loading happen on the main fiber, with every game fiber suspended: the host
// asks for it (savestate_request_*), the next poll() of the running fiber hands control
// over (savestate_poll), and the main fiber then resumes the fiber that was running when
// the state was taken. Host stack frames between a poll and the fiber switch hold no
// pointers to the heap, whose addresses change between runs.
//
// Rewind: the same payload, kept in memory every few frames while the game runs. The newest
// one is kept whole; each older one is a reverse delta (the XOR with the next newer one,
// with its runs of zero words left out), so most of a state that did not change costs
// nothing. Going back loads the newest, then rebuilds and loads each older one in turn.
//
// File: "IE3STATE", u32 format version, u32 0, u64 build identity, u64 save time (Unix),
// u64 payload size, u64 payload FNV-1a hash, then the payload (the modules' *_state).
#include <algorithm>
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <system_error>

#include <ntr/registry.h>
#include <ntr/runtime.h>

#include "fiber.h"
#include "runtime.h"

namespace ntr {

bool g_savestate_pending = false;
std::string g_savestate_message;
bool g_savestate_loaded = false;

namespace {

constexpr char kMagic[8] = {'I', 'E', '3', 'S', 'T', 'A', 'T', 'E'};
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderSize = 48;

enum class Op { None, Save, Load, RewindCapture, RewindStep };
Op g_op = Op::None;
int g_slot = 1;

Fiber* g_main = nullptr;
Fiber* g_game = nullptr;  // the game fiber to resume

struct Fnv {
    uint64_t h = 0xCBF29CE484222325ull;
    void add(const void* p, size_t n) {
        for (size_t i = 0; i < n; i++) h = (h ^ static_cast<const uint8_t*>(p)[i]) * 0x100000001B3ull;
    }
    void add(uint64_t v) { add(&v, sizeof v); }
};

// Changes with every build (the date and time this file was compiled, the addresses of
// code the stacks return into) and with the place of the fiber stacks.
uint64_t identity() {
    Fnv f;
    static const char kBuilt[] = __DATE__ " " __TIME__;
    f.add(kBuilt, sizeof kBuilt);
    f.add(kVersion);
    f.add(uintptr_t(&poll));
    f.add(uintptr_t(&savestate_poll));
    f.add(uintptr_t(&fiber_switch));
    f.add(uintptr_t(kRecompModules));
    for (uint32_t i = 0; i < kRecompModuleCount; i++) {
        const ModuleEntry& m = kRecompModules[i];
        f.add(m.count);
        if (m.count) {
            f.add(uintptr_t(m.funcs[0].fn));
            f.add(uintptr_t(m.funcs[m.count - 1].fn));
        }
    }
    f.add(fiber_region());
    f.add(sizeof(Cpu));
    f.add(mods_fingerprint());  // the game has read files that come from the mods
    return f.h;
}

std::filesystem::path slot_path(int slot) {
    std::string base = host_base_path();
    return fs_path(base.empty() ? "." : base) / "savestates" / ("boej-slot" + std::to_string(slot) + ".state");
}

void put64(uint8_t* p, uint64_t v) {
    store32(p, uint32_t(v));
    store32(p + 4, uint32_t(v >> 32));
}

uint64_t get64(const uint8_t* p) { return load32(p) | uint64_t(load32(p + 4)) << 32; }

// Every module, in a fixed order. The fibers come first: the others name them by index.
void all_state(StateIo& io) {
    fiber_state(io);
    memory_state(io);
    cpu_state(io);
    timing_state(io);
    irq_state(io);
    thread_state(io);
    task_state(io);
    io_state(io);
    pxi_state(io);
    gpu2d_state(io);
    gpu3d_state(io);
    sound_state(io);
    card_state(io);
    rtc_state(io);
    io.section("GAME");
    int32_t game = fiber_index(g_game);
    io.value(game);
    if (io.loading()) {
        g_game = fiber_at(game);
        if (!g_game || game < 0) io.fail("no fiber to resume");
    }
    io.section("END ");
}

bool write_file(const std::filesystem::path& path, const std::vector<uint8_t>& payload, std::string& error) {
    uint8_t header[kHeaderSize] = {};
    std::memcpy(header, kMagic, 8);
    store32(header + 8, kVersion);
    put64(header + 16, identity());
    put64(header + 24, uint64_t(std::time(nullptr)));
    put64(header + 32, payload.size());
    Fnv hash;
    hash.add(payload.data(), payload.size());
    put64(header + 40, hash.h);

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    FILE* f = open_file(tmp, "wb");
    if (!f) {
        error = "cannot create " + utf8_string(tmp);
        return false;
    }
    bool ok = std::fwrite(header, 1, sizeof header, f) == sizeof header &&
              std::fwrite(payload.data(), 1, payload.size(), f) == payload.size();
    ok = std::fclose(f) == 0 && ok;
    if (ok) std::filesystem::rename(tmp, path, ec);
    if (!ok || ec) {
        std::filesystem::remove(tmp, ec);
        error = "cannot write " + utf8_string(path);
        return false;
    }
    return true;
}

enum class Check { Ok, Missing, Invalid, OtherBuild };

// Reads the header (and, with `payload`, the rest) of a slot's file.
Check read_file(int slot, uint64_t& saved_at, std::vector<uint8_t>* payload) {
    FILE* f = open_file(slot_path(slot), "rb");
    if (!f) return Check::Missing;
    uint8_t header[kHeaderSize];
    Check result = Check::Ok;
    if (std::fread(header, 1, sizeof header, f) != sizeof header || std::memcmp(header, kMagic, 8) != 0) {
        result = Check::Invalid;
    } else if (load32(header + 8) != kVersion || get64(header + 16) != identity()) {
        result = Check::OtherBuild;
    }
    saved_at = get64(header + 24);
    if (result == Check::Ok && payload) {
        uint64_t size = get64(header + 32);
        payload->resize(size_t(size));
        Fnv hash;
        if (std::fread(payload->data(), 1, payload->size(), f) != payload->size()) {
            result = Check::Invalid;
        } else {
            hash.add(payload->data(), payload->size());
            if (hash.h != get64(header + 40)) result = Check::Invalid;
        }
    }
    std::fclose(f);
    return result;
}

void save(int slot) {
    std::vector<uint8_t> payload;
    payload.reserve(8u << 20);
    StateIo io(payload, false);
    all_state(io);
    std::string error;
    if (!io.ok() || !write_file(slot_path(slot), payload, error)) {
        NTR_LOG("[state] slot %d not saved: %s\n", slot, io.ok() ? error.c_str() : io.error().c_str());
        g_savestate_message = "Erro ao salvar o slot " + std::to_string(slot);
        return;
    }
    NTR_LOG("[state] slot %d saved (%zu KiB, cycle %llu)\n", slot, payload.size() >> 10,
            (unsigned long long)now_cycles());
    g_savestate_message = "Slot " + std::to_string(slot) + " salvo";
    session_savestate(true, slot);
}

// Puts `payload` in place of the current state. A failure halfway leaves a mix of both
// states: the current one goes back in.
bool restore(std::vector<uint8_t>& payload, std::string& error) {
    std::vector<uint8_t> backup;
    backup.reserve(payload.size());
    StateIo keep(backup, false);
    all_state(keep);
    StateIo io(payload, true);
    all_state(io);
    if (io.ok()) return true;
    error = io.error();
    StateIo undo(backup, true);
    all_state(undo);
    return false;
}

void load(int slot) {
    std::string name = "Slot " + std::to_string(slot);
    uint64_t saved_at = 0;
    std::vector<uint8_t> payload;
    switch (read_file(slot, saved_at, &payload)) {
        case Check::Missing:
            g_savestate_message = name + " vazio";
            return;
        case Check::Invalid:
            NTR_LOG("[state] slot %d: damaged file\n", slot);
            g_savestate_message = name + ": arquivo danificado";
            return;
        case Check::OtherBuild:
            NTR_LOG("[state] slot %d was saved by another build of the game\n", slot);
            g_savestate_message = name + ": de outra versao do jogo";
            return;
        case Check::Ok:
            break;
    }
    std::string error;
    if (!restore(payload, error)) {
        NTR_LOG("[state] slot %d cannot be loaded: %s\n", slot, error.c_str());
        g_savestate_message = name + ": arquivo danificado";
        return;
    }
    NTR_LOG("[state] slot %d loaded (cycle %llu)\n", slot, (unsigned long long)now_cycles());
    g_savestate_message = name + " carregado";
    session_savestate(false, slot);
    rewind_clear();
    g_savestate_loaded = true;
}


// --- rewind ---------------------------------------------------------------------------

constexpr size_t kRewindMaxBytes = size_t(256) << 20;  // all the deltas together

struct Rewind {
    bool enabled = false;
    size_t max_states = 0;
    std::vector<uint8_t> head;       // the newest state, whole
    bool head_loaded = false;        // a step already went back to it
    std::deque<std::vector<uint8_t>> deltas;  // older states, the oldest first
    size_t delta_bytes = 0;
    std::vector<uint8_t> scratch;
    uint64_t captures = 0, capture_ns = 0;  // for the log
} g_rewind;

uint64_t word_at(const std::vector<uint8_t>& v, size_t i) {
    uint64_t w = 0;
    if (i + 8 <= v.size()) std::memcpy(&w, v.data() + i, 8);
    else if (i < v.size()) std::memcpy(&w, v.data() + i, v.size() - i);
    return w;
}

// What turns `newer` back into `older`: older's size, then (u32 zero words, u32 literal
// words, the literal words) over the XOR of both, a missing tail counting as zeros.
std::vector<uint8_t> delta_encode(const std::vector<uint8_t>& older, const std::vector<uint8_t>& newer) {
    std::vector<uint8_t> out(8);
    put64(out.data(), older.size());
    size_t words = (older.size() + 7) / 8;
    size_t i = 0;
    while (i < words) {
        size_t zeros = 0;
        while (i < words && word_at(older, i * 8) == word_at(newer, i * 8)) i++, zeros++;
        size_t start = i;
        while (i < words && word_at(older, i * 8) != word_at(newer, i * 8)) i++;
        size_t at = out.size();
        out.resize(at + 8 + (i - start) * 8);
        store32(out.data() + at, uint32_t(zeros));
        store32(out.data() + at + 4, uint32_t(i - start));
        for (size_t k = start; k < i; k++) {
            uint64_t x = word_at(older, k * 8) ^ word_at(newer, k * 8);
            std::memcpy(out.data() + at + 8 + (k - start) * 8, &x, 8);
        }
    }
    out.shrink_to_fit();
    return out;
}

std::vector<uint8_t> delta_decode(const std::vector<uint8_t>& delta, const std::vector<uint8_t>& newer) {
    size_t size = size_t(get64(delta.data()));
    std::vector<uint8_t> older((size + 7) / 8 * 8);
    for (size_t i = 0; i < older.size(); i += 8) {
        uint64_t v = word_at(newer, i);
        std::memcpy(older.data() + i, &v, 8);
    }
    for (size_t at = 8, i = 0; at < delta.size();) {
        i += load32(delta.data() + at);
        uint32_t literal = load32(delta.data() + at + 4);
        at += 8;
        for (uint32_t k = 0; k < literal; k++, i++, at += 8) {
            uint64_t x, v;
            std::memcpy(&x, delta.data() + at, 8);
            std::memcpy(&v, older.data() + i * 8, 8);
            v ^= x;
            std::memcpy(older.data() + i * 8, &v, 8);
        }
    }
    older.resize(size);
    return older;
}

void rewind_trim() {
    while (!g_rewind.deltas.empty() &&
           (g_rewind.deltas.size() > g_rewind.max_states || g_rewind.delta_bytes > kRewindMaxBytes)) {
        g_rewind.delta_bytes -= g_rewind.deltas.front().size();
        g_rewind.deltas.pop_front();
    }
}

void rewind_capture() {
    auto start = std::chrono::steady_clock::now();
    std::vector<uint8_t>& now = g_rewind.scratch;
    now.clear();
    now.reserve(g_rewind.head.size() + (64u << 10));
    StateIo io(now, false);
    all_state(io);
    if (!io.ok()) {
        NTR_LOG("[rewind] state not captured: %s\n", io.error().c_str());
        return;
    }
    if (!g_rewind.head.empty()) {
        g_rewind.deltas.push_back(delta_encode(g_rewind.head, now));
        g_rewind.delta_bytes += g_rewind.deltas.back().size();
        rewind_trim();
    }
    g_rewind.head.swap(now);
    g_rewind.head_loaded = false;
    g_rewind.captures++;
    g_rewind.capture_ns += uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now() - start).count());
}

void rewind_step() {
    if (g_rewind.head_loaded && !g_rewind.deltas.empty()) {  // none left: the oldest again
        g_rewind.head = delta_decode(g_rewind.deltas.back(), g_rewind.head);
        g_rewind.delta_bytes -= g_rewind.deltas.back().size();
        g_rewind.deltas.pop_back();
    }
    std::string error;
    if (!restore(g_rewind.head, error)) {
        NTR_LOG("[rewind] state cannot be loaded: %s\n", error.c_str());
        rewind_clear();
        return;
    }
    g_rewind.head_loaded = true;
    g_savestate_loaded = true;
}

}  // namespace

void rewind_configure(bool enabled, int seconds, int interval) {
    g_rewind.enabled = enabled;
    g_rewind.max_states = size_t(std::max(1, seconds * 60 / std::max(1, interval)));
    if (!enabled) rewind_clear();
    rewind_trim();
}

void rewind_clear() {
    g_rewind.head = {};
    g_rewind.head_loaded = false;
    g_rewind.deltas = {};
    g_rewind.delta_bytes = 0;
    g_rewind.scratch = {};
}

bool rewind_enabled() { return g_rewind.enabled; }

void rewind_request_capture() {
    if (!g_rewind.enabled || g_op != Op::None) return;
    g_op = Op::RewindCapture;
    g_savestate_pending = true;
}

bool rewind_request_step() {
    if (!g_rewind.enabled || g_rewind.head.empty()) return false;
    if (g_op == Op::None || g_op == Op::RewindCapture) {
        g_op = Op::RewindStep;
        g_savestate_pending = true;
    }
    return !g_rewind.head_loaded || !g_rewind.deltas.empty();
}

void rewind_log() {
    if (!g_rewind.captures) return;
    NTR_LOG("[rewind] %zu states back (%zu KiB of deltas, the newest %zu KiB), %.2f ms per capture\n",
            g_rewind.deltas.size() + 1, g_rewind.delta_bytes >> 10, g_rewind.head.size() >> 10,
            g_rewind.capture_ns / 1e6 / double(g_rewind.captures));
}

void savestate_request_save(int slot) {
    g_op = Op::Save;
    g_slot = slot;
    g_savestate_pending = true;
}

void savestate_request_load(int slot) {
    g_op = Op::Load;
    g_slot = slot;
    g_savestate_pending = true;
}

std::string savestate_describe(int slot) {
    uint64_t saved_at = 0;
    switch (read_file(slot, saved_at, nullptr)) {
        case Check::Missing: return "";
        case Check::Invalid: return "Arquivo danificado";
        case Check::OtherBuild: return "De outra versão do jogo ou com outros mods";
        case Check::Ok: break;
    }
    std::time_t t = std::time_t(saved_at);
    char text[64];
    std::strftime(text, sizeof text, "%d/%m/%Y %H:%M:%S", std::localtime(&t));
    return text;
}

void savestate_poll() {
    g_game = fiber_current();
    fiber_switch(g_main);
}

void savestate_run(Fiber* game) {
    g_main = fiber_current();
    g_game = game;
    for (;;) {
        fiber_switch(g_game);
        g_savestate_pending = false;
        Op op = g_op;
        g_op = Op::None;
        if (op == Op::Save) save(g_slot);
        if (op == Op::Load) load(g_slot);
        if (op == Op::RewindCapture) rewind_capture();
        if (op == Op::RewindStep) rewind_step();
    }
}

}  // namespace ntr
