// Diagnostics for bug reports from players:
//
// Every run of the game is a session named after the time it started
// (YYYY-MM-DD_HH-MM-SS); its files go to logs/ next to the executable, which keeps the
// newest 20 sessions:
//
//   logs/sessoes/<session>.log    the log
//   logs/replays/<session>.txt    the input recording, with .sav (the save it started from)
//   logs/historico.txt            one line when each player session starts and ends, kept
//                                 forever: version, mods, save, how long, how it ended and
//                                 the screen it was on (a start without an end = killed)
//
// - Log file: everything NTR_LOG prints also goes to the session log (NTR_LOG_FILE picks
//   another path; empty disables it), each line prefixed with the frame number and the
//   time of day. Details that would flood the terminal (files the game opens) go only to
//   the file. Lines tagged [sessao] mark what the player did: saves, savestates, the
//   pause menu, the screens the game went through.
// - Crashes: fatal signals (and unhandled exceptions on Windows) are written to the log.
// - Watchdog: if no frame is presented for 5 s, the log gets a note and a state dump.
// - State dump (F8): display/brightness/interrupt registers and active overlays.
// - Session recording: the input of each windowed session is written to
//   logs/replays/<session>.txt in NTR_INPUT syntax, with the clock start and the save the
//   session started from (<session>.sav). Replaying both reproduces the session exactly:
//     NTR_REPLAY=logs/replays/<session>.txt NTR_SAVE=<copy of <session>.sav> boej
//   Scripted and headless runs (tests) are left out of historico.txt.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
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
std::time_t g_clock_time = -1;
char g_clock[16] = "";

constexpr size_t kSessionsKept = 20;
fs::path g_logs;        // logs/, made on first use
std::string g_session;  // the session's file names
struct Session {
    bool tracked = false;  // a player's session: goes to historico.txt
    bool begun = false, ended = false;
    uint64_t start_ms = 0;
    std::vector<std::pair<std::string, std::string>> info;  // for the start line
    uint64_t last_save_frame = 0;
    int game_saves = 0, states_saved = 0, states_loaded = 0, slow_seconds = 0, stalls = 0;
} g_s;

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

fs::path next_to_exe(const char* name) {
    std::string base = host_base_path();
    return fs_path(base.empty() ? "." : base) / name;
}

const fs::path& logs() {
    if (g_logs.empty()) g_logs = logs_dir(host_base_path());
    return g_logs;
}

// "YYYY-MM-DD_HH-MM-SS" of the first call; "-2"... when a session already has that name
// (a restart within the same second).
const std::string& session_name() {
    if (!g_session.empty()) return g_session;
    std::time_t now = std::time(nullptr);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d_%H-%M-%S", std::localtime(&now));
    std::error_code ec;
    for (int n = 1; g_session.empty(); n++) {
        std::string name = n == 1 ? text : std::string(text) + "-" + std::to_string(n);
        if (fs::exists(logs() / "sessoes" / (name + ".log"), ec) || fs::exists(logs() / "replays" / (name + ".txt"), ec)) {
            continue;
        }
        g_session = name;
    }
    return g_session;
}

// Keeps the logs and recordings of the newest kSessionsKept sessions.
void prune_sessions() {
    std::error_code ec;
    std::vector<std::string> names;
    for (const fs::directory_entry& e : fs::directory_iterator(logs() / "sessoes", ec)) {
        if (e.path().extension() == ".log") names.push_back(utf8_string(e.path().stem()));
    }
    if (names.size() <= kSessionsKept) return;
    std::sort(names.begin(), names.end());
    const std::string oldest_kept = names[names.size() - kSessionsKept];
    std::vector<fs::path> old;
    for (const char* folder : {"sessoes", "replays"}) {
        for (const fs::directory_entry& e : fs::directory_iterator(logs() / folder, ec)) {
            if (utf8_string(e.path().stem()) < oldest_kept) old.push_back(e.path());
        }
    }
    for (const fs::path& p : old) fs::remove(p, ec);
}

FILE* log_file() {
    if (g_log_tried) return g_log;
    g_log_tried = true;
    const char* env = std::getenv("NTR_LOG_FILE");
    if (env && !*env) return nullptr;
    if (env) {
        g_log = open_file(fs::path(env), "w");
        return g_log;
    }
    std::error_code ec;
    fs::create_directories(logs() / "sessoes", ec);
    g_log = open_file(logs() / "sessoes" / (session_name() + ".log"), "w");
    prune_sessions();
    return g_log;
}

// "HH:MM:SS", formatted again only when the second changes.
const char* clock_text() {
    std::time_t now = std::time(nullptr);
    if (now != g_clock_time) {
        g_clock_time = now;
        std::strftime(g_clock, sizeof g_clock, "%H:%M:%S", std::localtime(&now));
    }
    return g_clock;
}

void write_file(const char* text) {
    FILE* f = log_file();
    if (!f) return;
    for (const char* p = text; *p; p++) {
        if (g_line_start) std::fprintf(f, "%7llu %s| ", (unsigned long long)g_frame.load(), clock_text());
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

// A line in logs/historico.txt: when, the session, then `text`.
void history_line(const std::string& text) {
    FILE* f = open_file(logs() / "historico.txt", "a");
    if (!f) return;
    std::time_t now = std::time(nullptr);
    char when[32];
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    std::fprintf(f, "%s  %s  %s\n", when, session_name().c_str(), text.c_str());
    std::fclose(f);
}

// sdk/version.txt, which releases have next to the executable.
std::string game_version() {
    std::ifstream in(next_to_exe("sdk") / "version.txt");
    std::string v;
    std::getline(in, v);
    while (!v.empty() && std::isspace(uint8_t(v.back()))) v.pop_back();
    return v.empty() ? "dev" : "v" + v;
}

void session_begin() {
    std::lock_guard lock(g_log_mutex);
    if (g_s.begun) return;
    g_s.begun = true;
    std::string text = "início  " + game_version();
    for (const auto& [key, value] : g_s.info) text += "  " + key + ": " + value;
    NTR_LOG("[sessao] %s\n", text.c_str());
    history_line(text);
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
        session_end((std::string("falha: ") + what).c_str());
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
            g_s.stalls++;
        } else if (!stalled && reported) {
            NTR_LOG("[watchdog] frames are coming again\n");
        }
        reported = stalled;
    }
}

}  // namespace

void log_disable_file() { g_log_tried = true; }

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
    if (g_s.tracked && !g_s.begun) session_begin();  // the save and the mods are known by now
}

void session_track() {
    g_s.tracked = true;
    g_s.start_ms = wall_ms();
    session_info("mods", "nenhum");  // until mods_apply says otherwise
}

void session_info(const std::string& key, const std::string& value) {
    std::lock_guard lock(g_log_mutex);
    for (auto& [k, v] : g_s.info) {
        if (k == key) {
            v = value;
            return;
        }
    }
    g_s.info.emplace_back(key, value);
}

void session_event(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    NTR_LOG("[sessao] %s\n", buf);
}

void session_game_saved() {
    // One save is many writes over a few frames.
    uint64_t frame = g_frame.load();
    bool same_save = g_s.game_saves && frame - g_s.last_save_frame < 60;
    g_s.last_save_frame = frame;
    if (same_save) return;
    g_s.game_saves++;
    session_event("o jogo gravou o save (%d nesta sessão)", g_s.game_saves);
}

void session_savestate(bool saved, int slot) {
    if (saved) {
        g_s.states_saved++;
        session_event("savestate salvo no slot %d", slot);
    } else {
        g_s.states_loaded++;
        session_event("savestate do slot %d carregado (a gravação da entrada termina aqui)", slot);
    }
}

void session_slow_second() { g_s.slow_seconds++; }

void session_end(const char* reason) {
    std::lock_guard lock(g_log_mutex);
    if (!g_s.tracked || g_s.ended) return;
    if (!g_s.begun) session_begin();
    g_s.ended = true;
    uint64_t s = (wall_ms() - g_s.start_ms) / 1000;
    char text[1024];
    int n = std::snprintf(text, sizeof text, "fim  %lluh%02llum%02llus  %llu quadros  motivo: %s  saves do jogo: %d",
                          (unsigned long long)(s / 3600), (unsigned long long)(s / 60 % 60),
                          (unsigned long long)(s % 60), (unsigned long long)g_frame.load(), reason, g_s.game_saves);
    auto add = [&](const char* fmt, auto... args) {
        if (n >= 0 && size_t(n) < sizeof text) n += std::snprintf(text + n, sizeof text - n, fmt, args...);
    };
    if (g_s.states_saved || g_s.states_loaded) add("  savestates: %d salvos, %d carregados", g_s.states_saved, g_s.states_loaded);
    if (g_s.slow_seconds) add("  segundos com queda de FPS: %d", g_s.slow_seconds);
    if (g_s.stalls) add("  travamentos: %d", g_s.stalls);
    add("  última tela: %s", screen_now().c_str());
    NTR_LOG("[sessao] %s\n", text);
    history_line(text);
}

void replay_enable() { g_replay_wanted = true; }

void replay_start(const std::string& rtc_start) {
    if (!g_replay_wanted) return;
    std::error_code ec;
    fs::create_directories(logs() / "replays", ec);
    std::string name = "logs/replays/" + session_name();
    g_replay = open_file(logs() / "replays" / (session_name() + ".txt"), "w");
    if (!g_replay) return;
    std::fprintf(g_replay,
                 "# Session recording (input per frame, NTR_INPUT syntax).\n"
                 "# Replay: NTR_REPLAY=%s.txt NTR_SAVE=<copy of %s.sav> boej\n"
                 "# rtc %s\n",
                 name.c_str(), name.c_str(), rtc_start.c_str());
    std::fflush(g_replay);
}

void replay_initial_save(const std::vector<uint8_t>& data, bool existed) {
    if (!g_replay) return;
    std::fprintf(g_replay, "# save %s\n", existed ? (session_name() + ".sav").c_str() : "none (new game)");
    std::fflush(g_replay);
    if (!existed) return;
    std::ofstream out(logs() / "replays" / (session_name() + ".sav"), std::ios::binary);
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

void log_close() {
    std::lock_guard lock(g_log_mutex);
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
    g_log_tried = true;  // later lines go to the terminal only
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
