// Cartridge boot as the DS firmware does it: copy the ROM header and the ARM9 binary
// (still compressed; the game's crt0 decompresses it) into RAM, set up the shared
// work area, then jump to the ARM9 entry point.
#include <fstream>

#include "fiber.h"
#include "pxi.h"
#include "runtime.h"

namespace ntr {

namespace {

Rom g_rom;

// Firmware user settings at 0x027FFC80 (owner info, touch calibration, language).
void write_user_settings() {
    constexpr uint32_t base = 0x027FFC80;
    for (uint32_t i = 0; i < 0x70; i++) wr8(base + i, 0);
    wr16(base + 0x00, 5);       // version
    wr8(base + 0x02, 0);        // favorite color
    wr8(base + 0x03, 1);        // birthday month
    wr8(base + 0x04, 1);        // birthday day
    const char16_t name[] = u"IE3";
    for (int i = 0; i < 3; i++) wr16(base + 0x06 + i * 2, name[i]);
    wr16(base + 0x1A, 3);       // name length
    // Touch calibration: two reference points (ADC values and screen pixels).
    wr16(base + 0x58, 0x02DF);
    wr16(base + 0x5A, 0x032C);
    wr8(base + 0x5C, 0x20);
    wr8(base + 0x5D, 0x20);
    wr16(base + 0x5E, 0x0D3B);
    wr16(base + 0x60, 0x0CE7);
    wr8(base + 0x62, 0xE0);
    wr8(base + 0x63, 0xA0);
    wr16(base + 0x64, 0x0000);  // language: Japanese
}

}  // namespace

Rom& rom() { return g_rom; }

bool rom_load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    g_rom.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return g_rom.data.size() >= 0x200;
}

[[noreturn]] void boot() {
    const auto& r = g_rom.data;
    auto u32 = [&](uint32_t off) { return load32(r.data() + off); };
    uint32_t arm9_off = u32(0x20), arm9_entry = u32(0x24), arm9_ram = u32(0x28), arm9_size = u32(0x2C);

    memory_init();
    gpu_init();
    guest_copy_in(arm9_ram, r.data() + arm9_off, arm9_size);
    guest_copy_in(0x027FFE00, r.data(), 0x170);

    // Card chip IDs and boot flags the firmware leaves behind.
    wr32(0x027FF800, 0x00001FC2);
    wr32(0x027FF804, 0x00001FC2);
    wr32(0x027FFC00, 0x00001FC2);
    wr32(0x027FFC04, 0x00001FC2);
    wr16(0x027FFC10, 0x5835);
    wr16(0x027FFC40, 0x0001);  // booted from a cartridge
    write_user_settings();

    trace_install();
    arm7_boot();
    fiber_init_main();

    cpu_reset(0x1F);  // SYS mode, interrupts disabled
    Cpu& c = cpu();
    c.budget = 64;
    NTR_LOG("[boot] ARM9 %08X..%08X, entry %08X\n", arm9_ram, arm9_ram + arm9_size, arm9_entry);
    call_address(c, arm9_entry);
    unhandled(c, arm9_entry, "ARM9 entry returned");
}

}  // namespace ntr
