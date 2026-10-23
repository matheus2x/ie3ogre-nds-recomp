#include "pxi.h"

#include <cstdlib>
#include <deque>
#include <map>

#include "runtime.h"

namespace ntr {

namespace {

// Shared PXI state the SDK keeps in main RAM: per-processor bitmaps of tags with a
// registered receive callback (ARM9 at +0x388, ARM7 at +0x38C).
constexpr uint32_t kPxiReadyArm7 = 0x02FFFC00 + 0x38C;

constexpr const char* kTagNames[32] = {
    "EX", "USER0", "USER1", "SYSTEM", "NVRAM", "RTC", "TOUCHPANEL", "SOUND", "PM", "MIC", "WMW", "FS",
    "OS", "CTRDG", "CARD", "WVR", "CTRDG_Ex", "CTRDG_PHI",
};

uint32_t g_sync_arm7 = 0;
std::deque<uint32_t> g_recv;  // ARM7 -> ARM9
std::map<uint32_t, PxiService> g_services;
uint32_t g_ready_tags = 0;

uint32_t fifocnt() { return load32(memory().io.data() + 0x184); }

void update_recv_irq() {
    if (!g_recv.empty() && (fifocnt() & 0x0400)) raise_irq(1u << 18);
}

}  // namespace

uint32_t pxi_sync_from_arm7() { return g_sync_arm7; }

// The SDK handshake: each side mirrors what it reads; the ARM7 answers with value+1.
void pxi_sync_from_arm9(uint32_t value, bool irq) {
    g_sync_arm7 = (value + 1) & 0xF;
    (void)irq;
}

uint32_t pxi_fifo_status() {
    uint32_t s = 0x0001;  // send FIFO always drains immediately
    s |= g_recv.empty() ? 0x0100 : 0;
    s |= g_recv.size() >= 16 ? 0x0200 : 0;
    return s;
}

void pxi_clear_send_fifo() {}

void pxi_send_word(uint32_t word) {
    uint32_t tag = word & 0x1F;
    bool err = word & 0x20;
    uint32_t data = word >> 6;
    auto it = g_services.find(tag);
    if (it == g_services.end()) {
        NTR_LOG("[pxi] ARM9 -> %s: %08X (no service)\n", kTagNames[tag] ? kTagNames[tag] : "?", data);
        return;
    }
    it->second(data, err);
}

uint32_t pxi_recv_word() {
    if (g_recv.empty()) return 0;
    uint32_t w = g_recv.front();
    g_recv.pop_front();
    return w;
}

void pxi_reply(uint32_t tag, uint32_t data, bool err) {
    g_recv.push_back(tag | (err ? 0x20 : 0) | data << 6);
    update_recv_irq();
}

void pxi_register_service(uint32_t tag, PxiService service) {
    g_services[tag] = std::move(service);
    g_ready_tags |= 1u << tag;
    wr32(kPxiReadyArm7, g_ready_tags);
}

void arm7_boot() {
    // Services the game needs on the ARM7; each logs until it is implemented.
    // Wireless (WMW, tag 10) is left unregistered on purpose: Nintendo WFC is gone and
    // there is no local radio, so WM_Init sees the ARM7 library as absent and returns
    // WM_ERRCODE_WM_DISABLE at once. Every wireless mode (local play and Wi-Fi through
    // DWC in ov015) then takes the game's own error path instead of waiting for replies.
    for (uint32_t tag : {6u, 7u, 8u, 11u, 12u, 13u, 14u, 17u}) {
        pxi_register_service(tag, [tag](uint32_t data, bool err) {
            NTR_LOG("[pxi] ARM9 -> %s: %08X%s\n", kTagNames[tag], data, err ? " (err)" : "");
        });
    }
    arm7_rtc_init();
    arm7_sound_init();
    arm7_input_init();
    const char* save = std::getenv("NTR_SAVE");
    arm7_card_init(save ? save : "");
}

}  // namespace ntr
