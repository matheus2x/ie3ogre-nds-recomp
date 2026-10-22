// ARM7 sound driver, high level. For now it only acknowledges command lists so the
// ARM9 side keeps running; commands are counted, not executed.
//
// SND_FlushCommand sends the address of a linked list of SNDCommand over PXI tag 7:
//   +0 next, +4 id, +8 arg[4]
// The ARM9 waits until a "finished command tag" in shared memory advances. Its address
// is held in an ARM9 global (read by the function at 0x0200C3C4).
#include <cstdlib>
#include <map>

#include "pxi.h"
#include "runtime.h"

namespace ntr {

namespace {

constexpr uint32_t kFinishTagPtr = 0x02095920;  // literal pool of func_0200c3c4

std::map<uint32_t, uint64_t> g_command_counts;

void on_command_list(uint32_t list, bool) {
    for (uint32_t cmd = list, guard = 0; cmd && guard < 1024; cmd = rd32(cmd), guard++) {
        g_command_counts[rd32(cmd + 4)]++;
    }
    uint32_t tag_addr = rd32(kFinishTagPtr);
    if (tag_addr) wr32(tag_addr, rd32(tag_addr) + 1);
    if (std::getenv("NTR_LOG_SND")) NTR_LOG("[snd] command list %08X done (tag @%08X)\n", list, tag_addr);
}

}  // namespace

void arm7_sound_init() { pxi_register_service(7, on_command_list); }

}  // namespace ntr
