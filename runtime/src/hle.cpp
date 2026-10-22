// Host implementations of the functions listed in config/boej/hle.tsv. Each hook gets
// the recompiled original so it can wrap it; most replace it.
#include <cstdlib>
#include <cstring>
#include <vector>

#include "runtime.h"

namespace ntr {

namespace {

uint64_t pair(uint32_t lo, uint32_t hi) { return uint64_t(hi) << 32 | lo; }

void set_pair(Cpu& c, uint64_t v) {
    c.r[0] = uint32_t(v);
    c.r[1] = uint32_t(v >> 32);
}

}  // namespace

void hle_nop(Cpu&, GuestFn) {}

// --- CodeWarrior runtime: 64-bit and 32-bit integer helpers --------------------
// Arguments and results use the r0:r1 / r2:r3 register pairs (low word first).

void hle_ll_mul(Cpu& c, GuestFn) { set_pair(c, pair(c.r[0], c.r[1]) * pair(c.r[2], c.r[3])); }

void hle_ll_sdiv(Cpu& c, GuestFn) {
    int64_t a = int64_t(pair(c.r[0], c.r[1])), b = int64_t(pair(c.r[2], c.r[3]));
    if (b == 0) return;  // the guest routine returns the dividend unchanged
    set_pair(c, uint64_t(a == INT64_MIN && b == -1 ? a : a / b));
}

void hle_ll_mod(Cpu& c, GuestFn) {
    int64_t a = int64_t(pair(c.r[0], c.r[1])), b = int64_t(pair(c.r[2], c.r[3]));
    if (b == 0) return;
    set_pair(c, uint64_t(a == INT64_MIN && b == -1 ? 0 : a % b));
}

void hle_ull_div(Cpu& c, GuestFn) {
    uint64_t a = pair(c.r[0], c.r[1]), b = pair(c.r[2], c.r[3]);
    if (b == 0) return;
    set_pair(c, a / b);
}

void hle_ull_mod(Cpu& c, GuestFn) {
    uint64_t a = pair(c.r[0], c.r[1]), b = pair(c.r[2], c.r[3]);
    if (b == 0) return;
    set_pair(c, a % b);
}

void hle_s32_div(Cpu& c, GuestFn) {
    int32_t a = int32_t(c.r[0]), b = int32_t(c.r[1]);
    if (b == 0) return;
    int32_t q = (a == INT32_MIN && b == -1) ? a : a / b;
    c.r[1] = uint32_t(a - q * b);
    c.r[0] = uint32_t(q);
}

void hle_u32_div(Cpu& c, GuestFn) {
    uint32_t a = c.r[0], b = c.r[1];
    if (b == 0) return;  // "cmp r1, #0; bxeq lr"
    c.r[0] = a / b;
    c.r[1] = a % b;
}

// --- OS ------------------------------------------------------------------------

void hle_os_halt(Cpu& c, GuestFn) { wait_for_interrupt(c); }

// --- card ----------------------------------------------------------------------

// CARDi_ReadRomWithCPU(unused, dst, src, len) -> len
void hle_card_read_cpu(Cpu& c, GuestFn) {
    uint32_t dst = c.r[1], src = c.r[2], len = c.r[3];
    static const bool log = std::getenv("NTR_LOG_CARD");
    if (log) NTR_LOG("[card] rom read %08X -> %08X, %u bytes\n", src, dst, len);
    std::vector<uint8_t> buf(len);
    rom_read(src, buf.data(), len);
    guest_copy_in(dst, buf.data(), len);
    c.r[0] = len;
}

void hle_card_no_dma(Cpu& c, GuestFn) { c.r[0] = 0; }

}  // namespace ntr

namespace ntr {

// --- overlays --------------------------------------------------------------------
// FSOverlayInfo starts with the overlay id. The recompiled code of every overlay is
// always present; this hook tells the dispatcher which one occupies its slot. Unloading
// (FS_EndOverlay) changes nothing: like on hardware, the code stays callable until
// another overlay is loaded over it, and games do call into an overlay they just ended.

// Files the game opens go to the log file (not the terminal): the last ones before a
// problem usually tell what the game was doing.
void hle_fs_open_file(Cpu& c, GuestFn orig) {
    char path[128] = {};
    for (uint32_t i = 0; i + 1 < sizeof path; i++) {
        path[i] = char(rd8(c.r[1] + i));
        if (!path[i]) break;
    }
    orig(c);
    log_detail("[fs] open %s%s\n", path, c.r[0] ? "" : " (failed)");
}

void hle_fs_open_file_fast(Cpu& c, GuestFn orig) {
    uint32_t id = c.r[2];
    orig(c);
    log_detail("[fs] open file id %u%s\n", id, c.r[0] ? "" : " (failed)");
}

void hle_fs_start_overlay(Cpu& c, GuestFn orig) {
    overlay_set_active(rd32(c.r[0]), true);
    orig(c);
}

}  // namespace ntr

namespace ntr {

// --- video ---------------------------------------------------------------------------
// The Mobiclip (.mods) decoder is a hand-written ARM blob that jumps through computed
// addresses and calls by setting lr ahead of the jump, which the recompiled model cannot
// follow. It runs in the interpreter: r0 is the decoder state, r0 on return the number of
// bytes the frame used. The game itself converts the YCoCg planes and deblocks them.

void hle_mobiclip_decode(Cpu& c, GuestFn) { interpret_arm(c, 0x02130754); }

}  // namespace ntr
