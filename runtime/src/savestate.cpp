// Savestates: the whole emulator state in a file, 5 slots in savestates/ next to the
// executable (savestates/boej-slot1.state ...).
//
// The guest state alone is not enough: recompiled code keeps the guest's call chain on the
// host stacks of the fibers that run the game's threads and tasks. So a savestate also
// holds those stacks (fiber.cpp), and loading puts them back at the same addresses. That
// only works with the same executable at the same address: the file records an identity
// of the build (and of where its code and stacks are) and is refused by any other build.
//
// Saving and loading happen on the main fiber, with every game fiber suspended: the host
// asks for it (savestate_request_*), the next poll() of the running fiber hands control
// over (savestate_poll), and the main fiber then resumes the fiber that was running when
// the state was taken. Host stack frames between a poll and the fiber switch hold no
// pointers to the heap, whose addresses change between runs.
//
// File: "IE3STATE", u32 format version, u32 0, u64 build identity, u64 save time (Unix),
// u64 payload size, u64 payload FNV-1a hash, then the payload (the modules' *_state).
#include <ctime>
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

enum class Op { None, Save, Load };
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
    // A failure halfway leaves a mix of both states: the current one goes back in.
    std::vector<uint8_t> backup;
    StateIo keep(backup, false);
    all_state(keep);
    StateIo io(payload, true);
    all_state(io);
    if (!io.ok()) {
        NTR_LOG("[state] slot %d cannot be loaded: %s\n", slot, io.error().c_str());
        StateIo undo(backup, true);
        all_state(undo);
        g_savestate_message = name + ": arquivo danificado";
        return;
    }
    NTR_LOG("[state] slot %d loaded (cycle %llu)\n", slot, (unsigned long long)now_cycles());
    g_savestate_message = name + " carregado";
    g_savestate_loaded = true;
}

}  // namespace

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
        case Check::OtherBuild: return "De outra versão do jogo";
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
    }
}

}  // namespace ntr
