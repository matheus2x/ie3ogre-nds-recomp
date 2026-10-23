// Diagnostics for bug reports from players:
//
// - Log file: everything NTR_LOG prints also goes to boej.log next to the executable
//   (NTR_LOG_FILE picks another path; empty disables it), each line prefixed with the
//   frame number. Details that would flood the terminal (files the game opens) go only
//   to the file. The previous run's log is kept as boej.old.log.
// - Crashes: fatal signals (and unhandled exceptions on Windows) are written to the log.
// - Watchdog: if no frame is presented for 5 s, the log gets a note and a state dump.
// - State dump (F8): display/brightness/interrupt registers and active overlays.
// - Session recording: the input of each windowed session is written to
//   boej-replay.txt in NTR_INPUT syntax, with the clock start and the save the session
//   started from (boej-replay.sav). Replaying both reproduces the session exactly:
//     NTR_REPLAY=boej-replay.txt NTR_SAVE=<copy of boej-replay.sav> boej
//   The previous run's recording is kept as boej-replay.old.*.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include <ntr/runtime.h>

#include "runtime.h"

namespace ntr {

namespace {

namespace fs = std::filesystem;

std::recursive_mutex g_log_mutex;
FILE* g_log = nullptr;
bool g_log_tried = false;
bool g_line_start = true;
std::atomic<uint64_t> g_frame{0};

bool g_replay_wanted = false;
FILE* g_replay = nullptr;
struct InputState {
    uint16_t keys = 0;
    bool touch = false;
    int x = 0, y = 0;
    bool operator==(const InputState&) const = default;
};
InputState g_input;
uint64_t g_input_since = 0;

std::atomic<uint64_t> g_last_present_ms{0};

uint64_t wall_ms() {
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
}

// Moves `path` to its .old name so the previous run survives one restart.
void keep_previous(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return;
    fs::path old = path;
    old.replace_extension(".old" + path.extension().string());
    fs::remove(old, ec);
    fs::rename(path, old, ec);
}

fs::path next_to_exe(const char* name) {
    std::string base = host_base_path();
    return fs::path(base.empty() ? "." : base) / name;
}

FILE* log_file() {
    if (g_log_tried) return g_log;
    g_log_tried = true;
    const char* env = std::getenv("NTR_LOG_FILE");
    if (env && !*env) return nullptr;
    fs::path path = env ? fs::path(env) : next_to_exe("boej.log");
    keep_previous(path);
    g_log = std::fopen(path.string().c_str(), "w");
    return g_log;
}

void write_file(const char* text) {
    FILE* f = log_file();
    if (!f) return;
    for (const char* p = text; *p; p++) {
        if (g_line_start) std::fprintf(f, "%7llu| ", (unsigned long long)g_frame.load());
        std::fputc(*p, f);
        g_line_start = *p == '\n';
    }
    std::fflush(f);
}

void vlog(bool to_terminal, const char* fmt, va_list ap) {
    char buf[2048];
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    std::lock_guard lock(g_log_mutex);
    if (to_terminal) std::fputs(buf, stderr);
    write_file(buf);
}

const char* const kKeyNames[12] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L", "X", "Y"};

void replay_flush_range(uint64_t end_frame) {
    if (!g_replay || end_frame <= g_input_since) return;
    if (g_input.keys == 0 && !g_input.touch) return;
    std::fprintf(g_replay, "%llu-%llu:", (unsigned long long)g_input_since, (unsigned long long)(end_frame - 1));
    bool first = true;
    for (int i = 0; i < 12; i++) {
        if (!(g_input.keys & (1u << i))) continue;
        std::fprintf(g_replay, "%s%s", first ? "" : "+", kKeyNames[i]);
        first = false;
    }
    if (g_input.touch) std::fprintf(g_replay, "%sTOUCH@%d,%d", first ? "" : "+", g_input.x, g_input.y);
    std::fputc('\n', g_replay);
    std::fflush(g_replay);
}

void on_fatal(const char* what) {
    // Best effort: the process may be in any state here.
    if (g_log_mutex.try_lock()) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "\n[fatal] %s at frame %llu; the game stopped\n", what,
                      (unsigned long long)g_frame.load());
        std::fputs(buf, stderr);
        write_file(buf);
        debug_dump("fatal");
        replay_flush_range(g_frame.load() + 1);  // keep the input up to the crash
    }
}

void on_signal(int sig) {
    const char* name = sig == SIGSEGV ? "invalid memory access (SIGSEGV)"
                       : sig == SIGILL ? "illegal instruction (SIGILL)"
                       : sig == SIGFPE ? "arithmetic error (SIGFPE)"
                                       : "abort (SIGABRT)";
    on_fatal(name);
    std::_Exit(128 + sig);
}

#ifdef _WIN32
LONG WINAPI on_exception(EXCEPTION_POINTERS* info) {
    char what[96];
    std::snprintf(what, sizeof what, "exception %08lX at host address %p",
                  info->ExceptionRecord->ExceptionCode, info->ExceptionRecord->ExceptionAddress);
    on_fatal(what);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void watchdog() {
    bool reported = false;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        uint64_t last = g_last_present_ms.load();
        if (last == 0) continue;
        bool stalled = wall_ms() - last > 5000;
        if (stalled && !reported) {
            NTR_LOG("[watchdog] no frame for 5 s after frame %llu: the game may be stuck\n",
                    (unsigned long long)g_frame.load());
            debug_dump("watchdog");
        } else if (!stalled && reported) {
            NTR_LOG("[watchdog] frames are coming again\n");
        }
        reported = stalled;
    }
}

}  // namespace

void log_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(true, fmt, ap);
    va_end(ap);
}

void log_detail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(false, fmt, ap);
    va_end(ap);
}

void diag_init(bool windowed) {
    std::signal(SIGSEGV, on_signal);
    std::signal(SIGILL, on_signal);
    std::signal(SIGFPE, on_signal);
    std::signal(SIGABRT, on_signal);
#ifdef _WIN32
    SetUnhandledExceptionFilter(on_exception);
#endif
    if (windowed) std::thread(watchdog).detach();
}

void diag_frame(uint64_t frame) {
    g_frame = frame;
    g_last_present_ms = wall_ms();
}

void replay_enable() { g_replay_wanted = true; }

void replay_start(const std::string& rtc_start) {
    if (!g_replay_wanted) return;
    fs::path txt = next_to_exe("boej-replay.txt"), sav = next_to_exe("boej-replay.sav");
    keep_previous(txt);
    keep_previous(sav);
    g_replay = std::fopen(txt.string().c_str(), "w");
    if (!g_replay) return;
    std::fprintf(g_replay,
                 "# Session recording (input per frame, NTR_INPUT syntax).\n"
                 "# Replay: NTR_REPLAY=boej-replay.txt NTR_SAVE=<copy of boej-replay.sav> boej\n"
                 "# rtc %s\n",
                 rtc_start.c_str());
    std::fflush(g_replay);
}

void replay_initial_save(const std::vector<uint8_t>& data, bool existed) {
    if (!g_replay) return;
    std::fprintf(g_replay, "# save %s\n", existed ? "boej-replay.sav" : "none (new game)");
    std::fflush(g_replay);
    if (!existed) return;
    std::ofstream out(next_to_exe("boej-replay.sav"), std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
}

void replay_record(uint64_t frame, uint16_t keys, bool touch, int x, int y) {
    if (!g_replay) return;
    InputState now{keys, touch, touch ? x : 0, touch ? y : 0};
    if (now == g_input) return;
    replay_flush_range(frame);
    g_input = now;
    g_input_since = frame;
}

void replay_finish(uint64_t frame) {
    if (!g_replay) return;
    replay_flush_range(frame + 1);
    std::fclose(g_replay);
    g_replay = nullptr;
}

void debug_dump(const char* reason) {
    const uint8_t* io = memory().io.data();
    auto r16 = [&](uint32_t off) { return uint32_t(io[off] | io[off + 1] << 8); };
    auto r32 = [&](uint32_t off) { return load32(io + off); };
    NTR_LOG("[dump] %s, frame %llu, cycle %llu\n", reason, (unsigned long long)g_frame.load(),
            (unsigned long long)now_cycles());
    NTR_LOG("[dump] DISPCNT A %08X B %08X  MASTER_BRIGHT A %04X B %04X  POWCNT1 %04X\n", r32(0x000),
            r32(0x1000), r16(0x06C), r16(0x106C), r16(0x304));
    NTR_LOG("[dump] BLDCNT A %04X B %04X  BLDALPHA A %04X B %04X  BLDY A %02X B %02X\n", r16(0x050), r16(0x1050),
            r16(0x052), r16(0x1052), io[0x054], io[0x1054]);
    NTR_LOG("[dump] VRAMCNT A-I %02X %02X %02X %02X %02X %02X %02X %02X %02X  DISP3DCNT %04X  IME %u IE %08X IF %08X\n",
            io[0x240], io[0x241], io[0x242], io[0x243], io[0x244], io[0x245], io[0x246], io[0x248], io[0x249],
            r16(0x060), io[0x208] & 1, r32(0x210), r32(0x214));
    NTR_LOG("[dump] BG0-3CNT A %04X %04X %04X %04X B %04X %04X %04X %04X\n", r16(0x008), r16(0x00A), r16(0x00C),
            r16(0x00E), r16(0x1008), r16(0x100A), r16(0x100C), r16(0x100E));
    NTR_LOG("[dump] active overlays: %s\n", overlay_active_list().c_str());
    // OSThreadInfo (pointer at 0x01FF81AC): +4 current thread, +8 list linked by +0x68;
    // thread state at +0x64 (0 waiting, 1 ready, 2 terminated).
    std::string threads;
    if (uint32_t info = rd32(0x01FF81AC)) {
        NTR_LOG("[dump] game task %u, current thread %08X\n", rd32(0x020B9D50), rd32(info + 4));
        int n = 0;
        for (uint32_t t = rd32(info + 8); t && n < 32; t = rd32(t + 0x68), n++) {
            char item[32];
            std::snprintf(item, sizeof item, " %08X:%s", t, rd16(t + 0x64) == 1 ? "ready" : rd16(t + 0x64) == 0 ? "waiting" : "ended");
            threads += item;
        }
        NTR_LOG("[dump] threads:%s\n", threads.c_str());
    }
}

}  // namespace ntr
