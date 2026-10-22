// ARM7 touch panel service (PXI tag 6), high level.
//
// The ARM9 sends commands in data bits 8-14 (0 = one sample, 1 = start auto sampling,
// 2 = stop). The ARM7 writes the sample to a shared word and replies with
// 0x01000000 | command << 8 | result. Sample: x bits 0-11, y bits 12-23, touch bit 24,
// validity bits 25-26.
#include "pxi.h"
#include "runtime.h"

namespace ntr {

namespace {

constexpr uint32_t kTouchBuffer = 0x027FFFAA;  // literal at 0x02015EEC in TP's PXI callback

struct TouchState {
    bool down = false;
    uint32_t adc_x = 0, adc_y = 0;
};
TouchState g_touch;

void write_sample() {
    uint32_t sample = (g_touch.adc_x & 0xFFF) | (g_touch.adc_y & 0xFFF) << 12 | (g_touch.down ? 1u << 24 : 0);
    wr16(kTouchBuffer, uint16_t(sample));
    wr16(kTouchBuffer + 2, uint16_t(sample >> 16));
}

void on_command(uint32_t data, bool) {
    uint32_t command = (data >> 8) & 0x7F;
    if (command == 0) write_sample();
    pxi_reply(6, 0x01000000 | command << 8 | 0);  // TP_RESULT_SUCCESS
}

}  // namespace

void arm7_input_init() { pxi_register_service(6, on_command); }

}  // namespace ntr
