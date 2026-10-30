// Discord Rich Presence: "Playing Inazuma Eleven 3" on the player's profile, with the chapter
// and the team's level from the save and the time since the game opened. Nothing goes over
// the network: it talks to the Discord app on this PC through its local IPC (a named pipe
// \\.\pipe\discord-ipc-N on Windows, a socket discord-ipc-N in the runtime directory
// elsewhere), the same protocol the official SDKs use, so no Discord library ships with the
// port. Without Discord open, it tries again now and then and the game never waits for it.
//
// A message is a little-endian header (opcode, length) and that many bytes of JSON:
// 0 HANDSHAKE {"v":1,"client_id":...} answered by a READY DISPATCH, 1 FRAME (commands such as
// SET_ACTIVITY and their answers), 2 CLOSE, 3 PING (answered by 4 PONG). Discord takes at most
// 5 SET_ACTIVITY in 20 s, so changes closer than kSendGapNs are sent together, the last one
// winning. Closing the connection (or the game exiting) clears the presence.
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <thread>

#include <ntr/json.h>
#include <ntr/savedata.h>

#include "runtime.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace ntr {

namespace {

// The "IE3 Team Ogre Attacks! Recomp" application in the Discord Developer Portal: its name is
// what the profile shows after "Playing" and its icon is the presence's picture (no
// large_image is sent, so Discord shows the icon). NTR_DISCORD_APP_ID picks another one (tests).
constexpr const char* kAppId = "1565817315625533561";

constexpr int64_t kRetryNs = 20'000'000'000;    // Discord closed: try again after this
constexpr int64_t kSendGapNs = 4'000'000'000;   // between two SET_ACTIVITY
constexpr int64_t kReadyWaitNs = 3'000'000'000;  // for the answer to the handshake
constexpr uint32_t kMaxMessage = 64 * 1024;

enum Op : uint32_t { kHandshake = 0, kFrame = 1, kClose = 2, kPing = 3, kPong = 4 };

int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// --- the connection, per platform -------------------------------------------------------

class Pipe {
public:
    ~Pipe() { close(); }

#ifdef _WIN32
    bool open() {
        for (int i = 0; i < 10; i++) {
            wchar_t name[32];
            swprintf(name, 32, L"\\\\.\\pipe\\discord-ipc-%d", i);
            HANDLE h = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                h_ = h;
                return true;
            }
        }
        return false;
    }
    bool is_open() const { return h_ != INVALID_HANDLE_VALUE; }
    void close() {
        if (is_open()) CloseHandle(h_);
        h_ = INVALID_HANDLE_VALUE;
    }
    bool write(const void* p, size_t n) {
        DWORD done = 0;
        return WriteFile(h_, p, DWORD(n), &done, nullptr) && done == n;
    }
    bool read(void* p, size_t n) {
        auto* out = static_cast<uint8_t*>(p);
        while (n) {
            DWORD done = 0;
            if (!ReadFile(h_, out, DWORD(n), &done, nullptr) || !done) return false;
            out += done;
            n -= done;
        }
        return true;
    }
    // A message waiting (false also when the pipe broke: the next read tells).
    bool readable(bool& broken) {
        DWORD avail = 0;
        broken = !PeekNamedPipe(h_, nullptr, 0, nullptr, &avail, nullptr);
        return broken || avail > 0;
    }

private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
#else
    bool open() {
        // The runtime directory, also where the Flatpak and Snap Discord put theirs.
        std::string base;
        for (const char* var : {"XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP"}) {
            if (const char* v = std::getenv(var); v && *v) {
                base = v;
                break;
            }
        }
        if (base.empty()) base = "/tmp";
        for (const char* sub : {"", "app/com.discordapp.Discord/", "snap.discord/", ".flatpak/com.discordapp.Discord/xdg-run/"}) {
            for (int i = 0; i < 10; i++) {
                std::string path = base + "/" + sub + "discord-ipc-" + std::to_string(i);
                sockaddr_un addr{};
                addr.sun_family = AF_UNIX;
                if (path.size() >= sizeof addr.sun_path) continue;
                std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
                int fd = socket(AF_UNIX, SOCK_STREAM, 0);
                if (fd < 0) return false;
                if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0) {
                    fd_ = fd;
                    return true;
                }
                ::close(fd);
            }
        }
        return false;
    }
    bool is_open() const { return fd_ >= 0; }
    void close() {
        if (is_open()) ::close(fd_);
        fd_ = -1;
    }
    bool write(const void* p, size_t n) {
        auto* in = static_cast<const uint8_t*>(p);
        while (n) {
            ssize_t done = send(fd_, in, n, MSG_NOSIGNAL);
            if (done <= 0) return false;
            in += done;
            n -= size_t(done);
        }
        return true;
    }
    bool read(void* p, size_t n) {
        auto* out = static_cast<uint8_t*>(p);
        while (n) {
            ssize_t done = recv(fd_, out, n, 0);
            if (done <= 0) return false;
            out += done;
            n -= size_t(done);
        }
        return true;
    }
    bool readable(bool& broken) {
        pollfd pfd{fd_, POLLIN, 0};
        int r = poll(&pfd, 1, 0);
        broken = r < 0 || (pfd.revents & (POLLERR | POLLNVAL));
        return broken || (r > 0 && (pfd.revents & (POLLIN | POLLHUP)));
    }

private:
    int fd_ = -1;
#endif
};

// --- the presence --------------------------------------------------------------------------

struct Wanted {
    bool enabled = false;
    bool have_save = false;
    SaveSummary save;
    int paused = 0;  // nested: the pause menu over a pause
};

// On the heap and never freed: the thread is detached and outlives static destructors.
struct State {
    std::mutex mutex;
    std::condition_variable wake;
    Wanted wanted;
    uint64_t version = 0;  // bumped by each change of `wanted`
    std::string app_id;
    int64_t started = 0;   // unix seconds, the "elapsed" of the presence
};
State* g_state = nullptr;

class Client {
public:
    explicit Client(State& s) : s_(s) {}

    void run() {
        uint64_t sent = ~0ull;
        bool was_enabled = false;
        int64_t next_try = 0, last_send = -kSendGapNs;
        for (;;) {
            Wanted wanted;
            uint64_t version;
            {
                std::unique_lock lock(s_.mutex);
                s_.wake.wait_for(lock, std::chrono::seconds(1));
                wanted = s_.wanted;
                version = s_.version;
            }
            if (!wanted.enabled) {
                if (pipe_.is_open()) {
                    NTR_LOG("[discord] turned off\n");
                    pipe_.close();
                }
                was_enabled = false;
                continue;
            }
            if (!was_enabled) next_try = 0;  // turned on: connect now
            was_enabled = true;
            int64_t now = now_ns();
            if (!pipe_.is_open()) {
                if (refused_ || now < next_try) continue;
                next_try = now + kRetryNs;
                if (!connect()) continue;
                sent = ~0ull;
            }
            if (!drain()) {
                disconnect("Discord closed the connection");
                continue;
            }
            if (version != sent && now - last_send >= kSendGapNs) {
                if (!send_frame(kFrame, activity(wanted))) {
                    disconnect("cannot write to Discord");
                    continue;
                }
                sent = version;
                last_send = now;
            }
        }
    }

private:
    bool send_frame(uint32_t op, const Json& body) {
        std::string text = json_write(body);
        uint8_t header[8];
        for (int i = 0; i < 4; i++) {
            header[i] = uint8_t(op >> (8 * i));
            header[4 + i] = uint8_t(text.size() >> (8 * i));
        }
        return pipe_.write(header, 8) && pipe_.write(text.data(), text.size());
    }

    // One whole message; false when the connection broke or it makes no sense.
    bool read_frame(uint32_t& op, Json& body) {
        uint8_t header[8];
        if (!pipe_.read(header, 8)) return false;
        op = header[0] | header[1] << 8 | header[2] << 16 | uint32_t(header[3]) << 24;
        uint32_t size = header[4] | header[5] << 8 | header[6] << 16 | uint32_t(header[7]) << 24;
        if (size > kMaxMessage) return false;
        std::string text(size, '\0');
        if (!pipe_.read(text.data(), size)) return false;
        std::string error;
        if (!json_parse(text, body, error)) body = Json();
        return true;
    }

    // What came from Discord since the last time: the answers to SET_ACTIVITY (errors are
    // logged), pings and a close.
    bool drain() {
        for (;;) {
            bool broken = false;
            if (!pipe_.readable(broken)) return true;
            uint32_t op;
            Json body;
            if (broken || !read_frame(op, body)) return false;
            if (op == kClose) {
                NTR_LOG("[discord] closed by Discord: %s\n", body.get("message").as_string().c_str());
                return false;
            }
            if (op == kPing && !send_frame(kPong, body)) return false;
            if (op == kFrame && body.get("evt").as_string() == "ERROR") {
                NTR_LOG("[discord] %s: %s\n", body.get("cmd").as_string().c_str(),
                        body.get("data").get("message").as_string().c_str());
            }
        }
    }

    bool connect() {
        if (!pipe_.open()) return false;  // Discord not open: quiet, it is tried again later
        Json hello = Json::object();
        hello.set("v", 1);
        hello.set("client_id", s_.app_id);
        if (!send_frame(kHandshake, hello)) {
            pipe_.close();
            return false;
        }
        for (int64_t end = now_ns() + kReadyWaitNs; now_ns() < end;) {
            bool broken = false;
            if (!pipe_.readable(broken)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            uint32_t op;
            Json body;
            if (broken || !read_frame(op, body)) break;
            if (op == kFrame && body.get("evt").as_string() == "READY") {
                const Json& user = body.get("data").get("user");
                NTR_LOG("[discord] connected (%s)\n", user.get("username").as_string().c_str());
                return true;
            }
            if (op == kClose) {
                // A bad application id: asking again will not help.
                NTR_LOG("[discord] handshake refused, off until the game restarts: %s\n",
                        body.get("message").as_string().c_str());
                refused_ = true;
                break;
            }
        }
        pipe_.close();
        return false;
    }

    void disconnect(const char* why) {
        NTR_LOG("[discord] %s; trying again later\n", why);
        pipe_.close();
    }

    Json activity(const Wanted& w) {
        char details[128], state[128] = "";
        const SaveSummary& save = w.save;
        if (!w.have_save || !save.valid || save.empty || save.unknown) {
            std::snprintf(details, sizeof details, "%s", L("New game", "Jogo novo"));
        } else {
            std::snprintf(details, sizeof details, L("Chapter %d", "Capítulo %d"), save.chapter);
            std::snprintf(state, sizeof state, L("Team level %d · %d players", "Time nível %d · %d jogadores"),
                          save.level, save.members);
        }
        if (w.paused) std::snprintf(state, sizeof state, "%s", L("Paused", "Pausado"));

        Json act = Json::object();
        act.set("details", details);
        if (*state) act.set("state", state);
        Json timestamps = Json::object();
        timestamps.set("start", double(s_.started));
        act.set("timestamps", timestamps);

        Json args = Json::object();
#ifdef _WIN32
        args.set("pid", double(GetCurrentProcessId()));
#else
        args.set("pid", double(getpid()));
#endif
        args.set("activity", act);
        Json cmd = Json::object();
        cmd.set("cmd", "SET_ACTIVITY");
        cmd.set("args", args);
        cmd.set("nonce", std::to_string(++nonce_));
        return cmd;
    }

    State& s_;
    Pipe pipe_;
    uint64_t nonce_ = 0;
    bool refused_ = false;
};

template <typename F>
void change(F&& f) {
    if (!g_state) return;
    {
        std::lock_guard lock(g_state->mutex);
        f(g_state->wanted);
        g_state->version++;
    }
    g_state->wake.notify_one();
}

}  // namespace

void discord_init(bool enabled) {
    const char* env = std::getenv("NTR_DISCORD_APP_ID");
    std::string app_id = env && *env ? env : kAppId;
    if (app_id.empty()) {
        NTR_LOG("[discord] off: no application id in this build\n");
        return;
    }
    g_state = new State;
    g_state->app_id = app_id;
    g_state->started = int64_t(std::time(nullptr));
    g_state->wanted.enabled = enabled;
    std::thread([s = g_state] { Client(*s).run(); }).detach();
}

void discord_enable(bool enabled) {
    if (g_state && g_state->wanted.enabled != enabled) change([&](Wanted& w) { w.enabled = enabled; });
}

void discord_save_changed(const SaveSummary& save) {
    change([&](Wanted& w) {
        w.have_save = true;
        w.save = save;
    });
}

void discord_paused(bool paused) {
    change([&](Wanted& w) { w.paused += paused ? 1 : -1; });
}

}  // namespace ntr
