// IPC between the ARM9 and the high-level ARM7 (IPCSYNC + IPC FIFO, NitroSDK PXI).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ntr {

// Called by the MMIO layer.
uint32_t pxi_sync_from_arm7();
void pxi_sync_from_arm9(uint32_t value, bool irq);
uint32_t pxi_fifo_status();  // IPCFIFOCNT bits 0-1 and 8-9
void pxi_send_word(uint32_t word);
uint32_t pxi_recv_word();
void pxi_clear_send_fifo();

// ARM7 side.
void arm7_boot();
void arm7_rtc_init();
void arm7_sound_init();
void arm7_input_init();
void arm7_vblank();  // per-frame ARM7 work: buttons and touch state in shared memory
void arm7_card_init(const std::string& save_path);
void pxi_reply(uint32_t tag, uint32_t data, bool err = false);

// A service receives (data, err) for words the ARM9 sends with its tag.
using PxiService = std::function<void(uint32_t data, bool err)>;
void pxi_register_service(uint32_t tag, PxiService service);

}  // namespace ntr
