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

// --- text ----------------------------------------------------------------------------
// The name entry field (overlay 55) draws the name with Font_DrawString over a row of
// 12 px boxes, 13 px apart. Every letter of the game's font advances 11 px, which with
// the field's 2 px spacing makes 13; a translated font with narrower letters (the keyboard
// types full-width letters) drifts out of the boxes. So that call draws one letter per
// box: a letter that advances 11 px where the game puts it, any other centered by its ink.

namespace {

constexpr uint32_t kNameFieldOverlay = 55;
constexpr uint32_t kNameFieldReturn = 0x0211B140;  // after "blx r5" in func_ov055_0211b07c
constexpr uint32_t kNameFieldCall = 0xE12FFF35;
constexpr int kNameBoxStep = 13;
constexpr int kNameBoxWidth = 12;
constexpr int kNameLetterAdvance = 11;  // CWDH advance of every letter of the game's font
constexpr uint32_t kFontGlyphIndex = 0x020182AC;
constexpr uint32_t kFontGlyphWidths = 0x020182F4;
constexpr uint32_t kNoGlyph = 0xFFFF;

// The objective on the top screen (overlay 0) is drawn into a 64 px wide canvas that wraps
// a line where a letter does not fit; four sprites show it, each starting 16 rows below the
// last. The font's line spacing there is 5, so the lines are 17 rows apart and every sprite
// shows its line 1 px lower than the one before (Japanese too, barely seen). That call gets
// spacing 4: the lines fall 16 rows apart, all at the same height.
constexpr uint32_t kObjectiveOverlay = 0;
constexpr uint32_t kObjectiveReturn = 0x020C739C;  // after "blx r4" in func_ov000_020c72d4
constexpr uint32_t kObjectiveCall = 0xE12FFF34;
constexpr uint32_t kObjectiveSpacing = 4;
constexpr uint32_t kFontLineSpacing = 0x18;  // field of the font object: rows between lines

// The place, the label and the objective are drawn (by func_ov000_020c749c, 020c73bc and
// 020c72d4) from a copy of the data at 0x020AA044, which the save keeps with the texts in it.
// Before the draw, a text of another version of the scripts becomes the one in use
// (saved_text_fix): a save made with a translation, loaded without it, has no letters "?".
struct SavedText {
    uint32_t ret, call, offset, size;  // after the call; its "blx"; the field in the data
};
constexpr SavedText kSavedTexts[] = {
    {0x020C7558, 0xE12FFF3C, 0x44, 0x20},  // the place
    {0x020C747C, 0xE12FFF36, 0x76, 0x10},  // the label ("もくてき")
    {kObjectiveReturn, kObjectiveCall, 0x86, 0x40},
};
constexpr uint32_t kSavedData = 0x020AA044;

void fix_saved_text(const Cpu& c) {
    for (const SavedText& t : kSavedTexts) {
        if (c.r[14] == t.ret && overlay_active_at(kObjectiveOverlay, t.ret) && rd32(t.ret - 4) == t.call) {
            saved_text_fix(c.r[3], kSavedData + t.offset, t.size);
            return;
        }
    }
}

bool is_objective(const Cpu& c) {
    return c.r[14] == kObjectiveReturn && overlay_active_at(kObjectiveOverlay, kObjectiveReturn) &&
           rd32(kObjectiveReturn - 4) == kObjectiveCall;
}

bool is_name_field(const Cpu& c) {
    return c.r[14] == kNameFieldReturn && overlay_active_at(kNameFieldOverlay, kNameFieldReturn) &&
           rd32(kNameFieldReturn - 4) == kNameFieldCall;
}

// Where a letter starts in its box. The CWDH entry (left, glyph width, advance) is looked
// up as Font_DrawString does.
int letter_offset(Cpu& c, uint32_t font, uint32_t code) {
    c.r[0] = font + 4;
    c.r[1] = code;
    call_address(c, kFontGlyphIndex);
    uint32_t glyph = c.r[0];
    if (glyph == kNoGlyph) glyph = rd16(rd32(font + 4) + 2);
    c.r[0] = font + 4;
    c.r[1] = glyph;
    call_address(c, kFontGlyphWidths);
    uint32_t widths = c.r[0];
    if (int8_t(rd8(widths + 2)) == kNameLetterAdvance) return 0;
    return (kNameBoxWidth - rd8(widths + 1)) / 2 - int8_t(rd8(widths));
}

}  // namespace

void hle_font_draw_string(Cpu& c, GuestFn orig) {
    fix_saved_text(c);
    if (is_objective(c)) {
        const uint32_t field = c.r[0] + kFontLineSpacing, spacing = rd32(field), lr = c.r[14];
        wr32(field, kObjectiveSpacing);
        orig(c);
        wr32(field, spacing);
        c.r[14] = lr;
        return;
    }
    if (!is_name_field(c) || !c.r[3]) return orig(c);
    const uint32_t font = c.r[0], x = c.r[1], y = c.r[2], lr = c.r[14];
    uint32_t at = c.r[3];
    for (int box = 0; uint8_t lead = rd8(at); box++) {
        bool single = (lead >= 0x20 && lead <= 0x7E) || (lead >= 0xA1 && lead <= 0xDF);
        uint32_t size = single ? 1 : 2;
        uint32_t code = single ? lead : uint32_t(lead) << 8 | rd8(at + 1);
        int offset = letter_offset(c, font, code);
        uint8_t after = rd8(at + size);
        wr8(at + size, 0);  // the string ends after this letter for the call
        c.r[0] = font;
        c.r[1] = x + box * kNameBoxStep + offset;
        c.r[2] = y;
        c.r[3] = at;
        c.r[14] = lr;
        orig(c);
        wr8(at + size, after);
        at += size;
    }
    c.r[0] = 1;
    c.r[14] = lr;
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
