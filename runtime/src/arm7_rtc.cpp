// Real-time clock service (PXI tag 5), high level.
//
// The ARM9 sends a command in data bits 8-14 (RTCi_SendPxiCommand). For reads the ARM7
// leaves the raw clock registers in shared memory and replies with command << 8 | result
// (0 = success); RTCi_CallbackFromPxi decodes them:
//   0x02FFFDE8  date: year (BCD, 2000-based) bits 0-7, month bits 8-12, day bits 16-21,
//               day of week bits 24-26 (the SDK recomputes it from the date)
//   0x02FFFDEC  time: hour bits 0-5, minute bits 8-14, second bits 16-22 (24-hour)
// Commands: 0x10 date+time, 0x11 date, 0x12 time; the rest (status, alarms, writes) are
// acknowledged without effect.
//
// The clock starts at the host's local time at boot (or at NTR_RTC="YYYY-MM-DD HH:MM:SS",
// or at the start written in a session recording) and advances with emulated time, so a
// run depends only on its start and its input and can be replayed exactly.
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "pxi.h"
#include "runtime.h"

namespace ntr {

namespace {

constexpr uint32_t kRtcDate = 0x02FFFDE8;
constexpr uint32_t kRtcTime = 0x02FFFDEC;
constexpr uint64_t kCyclesPerSecond = 33513982;

std::tm g_start{};
std::string g_start_text;
std::string g_requested;  // from a session recording

uint32_t bcd(int v) { return uint32_t((v / 10) << 4 | v % 10); }

bool parse_time(const std::string& text, std::tm& out) {
    std::tm t{};
    if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min,
                    &t.tm_sec) != 6) {
        return false;
    }
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    t.tm_isdst = -1;
    std::mktime(&t);
    out = t;
    return true;
}

std::tm current_time() {
    std::tm t = g_start;
    t.tm_sec += int(now_cycles() / kCyclesPerSecond);
    std::mktime(&t);  // normalizes the overflowing seconds
    return t;
}

void write_clock(bool date, bool time) {
    std::tm t = current_time();
    if (date) {
        int year = t.tm_year + 1900 - 2000;
        year = year < 0 ? 0 : year > 99 ? 99 : year;
        wr32(kRtcDate, bcd(year) | bcd(t.tm_mon + 1) << 8 | bcd(t.tm_mday) << 16 | uint32_t(t.tm_wday) << 24);
    }
    if (time) wr32(kRtcTime, bcd(t.tm_hour) | bcd(t.tm_min) << 8 | bcd(t.tm_sec) << 16);
}

}  // namespace

void rtc_request_start(const std::string& text) { g_requested = text; }

const std::string& rtc_start_text() { return g_start_text; }

// The clock's start: with the emulated time it gives the date and time the game sees.
void rtc_state(StateIo& io) {
    io.section("RTC ");
    std::vector<char> text(g_start_text.begin(), g_start_text.end());
    io.vector(text);
    if (io.loading() && io.ok()) {
        std::string t(text.begin(), text.end());
        if (!parse_time(t, g_start)) return io.fail("bad clock start");
        g_start_text = t;
    }
}

void arm7_rtc_init() {
    const char* fixed = std::getenv("NTR_RTC");
    std::string wanted = fixed ? fixed : g_requested;
    if (wanted.empty() || !parse_time(wanted, g_start)) {
        if (!wanted.empty()) NTR_LOG("[rtc] \"%s\" is not \"YYYY-MM-DD HH:MM:SS\"; using the host clock\n", wanted.c_str());
        std::time_t now = std::time(nullptr);
        g_start = *std::localtime(&now);
    }
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &g_start);
    g_start_text = text;
    NTR_LOG("[rtc] clock starts at %s\n", text);
    replay_start(g_start_text);
    write_clock(true, true);  // the firmware leaves a reading there at boot
    pxi_register_service(5, [](uint32_t data, bool) {
        uint32_t command = (data >> 8) & 0x7F;
        switch (command) {
            case 0x10: write_clock(true, true); break;
            case 0x11: write_clock(true, false); break;
            case 0x12: write_clock(false, true); break;
            default: NTR_LOG("[rtc] command %02X acknowledged without effect\n", command); break;
        }
        pxi_reply(5, command << 8);
    });
}

}  // namespace ntr
