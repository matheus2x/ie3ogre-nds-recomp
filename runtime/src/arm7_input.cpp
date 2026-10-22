// ARM7 per-frame work and input services, high level: the touch panel (PXI tag 6) and
// the X/Y buttons.
//
// Touch: the ARM9 sends commands in data bits 8-14 (0 = one sample, 1 = start auto
// sampling, 2 = stop). The ARM7 writes the sample to a shared word and replies with
// 0x01000000 | command << 8 | result. Sample: x bits 0-11, y bits 12-23, touch bit 24,
// validity bits 25-26 (0 = valid). Coordinates are raw ADC values; the SDK converts them
// with the calibration from the user settings, so screen pixels are mapped back through
// the same calibration.
//
// The ARM7 VBlank handler also counts frames at HW_VBLANK_COUNT_BUF (OS_GetVBlankCount);
// games use it to measure frame time.
//
// Buttons: X, Y, debug and hinge are only readable by the ARM7 (EXTKEYIN); its firmware
// copies them every frame to a halfword in PAD_Read's layout: X bit 10, Y bit 11, debug
// bit 13 (all 1 = released, like KEYINPUT) and hinge closed bit 15. PAD_Read ORs it with
// KEYINPUT, so leaving X/Y/debug at 0 reads as those buttons held.
#include <cstdlib>

#include "pxi.h"
#include "runtime.h"

namespace ntr {

namespace {

constexpr uint32_t kTouchBuffer = 0x027FFFAA;  // literal at 0x02015EEC in TP's PXI callback
constexpr uint32_t kButtonXYBuffer = 0x027FFFA8;  // HW_BUTTON_XY_BUF
constexpr uint32_t kVBlankCount = 0x027FFC3C;     // HW_VBLANK_COUNT_BUF
constexpr uint32_t kTouchCalibration = 0x027FFC80 + 0x58;  // user settings, see boot.cpp

uint32_t to_adc(uint32_t pixel, uint32_t adc1, uint32_t scr1, uint32_t adc2, uint32_t scr2) {
    if (scr2 == scr1) return adc1;
    int32_t v = int32_t(adc1) + (int32_t(pixel) - int32_t(scr1)) * (int32_t(adc2) - int32_t(adc1)) / (int32_t(scr2) - int32_t(scr1));
    return uint32_t(v < 0 ? 0 : v > 0xFFF ? 0xFFF : v);
}

void write_sample() {
    int x = 0, y = 0;
    bool down = host_touch(x, y);
    uint32_t sample = 0;
    if (down) {
        uint32_t c = kTouchCalibration;
        uint32_t adc_x = to_adc(x, rd16(c + 0), rd8(c + 4), rd16(c + 6), rd8(c + 10));
        uint32_t adc_y = to_adc(y, rd16(c + 2), rd8(c + 5), rd16(c + 8), rd8(c + 11));
        sample = adc_x | adc_y << 12 | 1u << 24;
    }
    wr16(kTouchBuffer, uint16_t(sample));
    wr16(kTouchBuffer + 2, uint16_t(sample >> 16));
}

void on_command(uint32_t data, bool) {
    uint32_t command = (data >> 8) & 0x7F;
    static const bool log = std::getenv("NTR_LOG_TP");
    if (log) NTR_LOG("[tp] command %08X\n", data);
    if (command == 0) write_sample();
    else NTR_LOG("[tp] auto sampling command %u is not implemented\n", command);
    pxi_reply(6, 0x01000000 | command << 8 | 0);  // TP_RESULT_SUCCESS
}

uint16_t xy_buffer() {
    uint16_t ext = host_extkeys();  // EXTKEYIN: X bit 0, Y bit 1, debug bit 3, hinge bit 7
    return uint16_t((ext & 0x0B) << 10 | (ext & 0x80) << 8);
}

}  // namespace

void arm7_input_init() {
    pxi_register_service(6, on_command);
    wr16(kButtonXYBuffer, xy_buffer());
}

void arm7_vblank() {
    wr32(kVBlankCount, rd32(kVBlankCount) + 1);
    wr16(kButtonXYBuffer, xy_buffer());
}

}  // namespace ntr
