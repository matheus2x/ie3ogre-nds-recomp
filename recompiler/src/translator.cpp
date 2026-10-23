#include "translator.h"

#include <bit>
#include <cstdio>
#include <deque>
#include <format>
#include <sstream>

namespace recomp {

std::string hex32(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08Xu", v);
    return buf;
}

namespace {

constexpr const char* kCond[16] = {
    "c.z", "!c.z", "c.c", "!c.c", "c.n", "!c.n", "c.v", "!c.v",
    "(c.c && !c.z)", "(!c.c || c.z)", "(c.n == c.v)", "(c.n != c.v)",
    "(!c.z && c.n == c.v)", "(c.z || c.n != c.v)", nullptr, nullptr,
};

int32_t sext(uint32_t v, int bits) {
    return int32_t(v << (32 - bits)) >> (32 - bits);
}

std::string wrap_cond(uint32_t cond, const std::string& body) {
    if (cond == 0xE) {
        return body;
    }
    return std::format("if ({}) {}", kCond[cond], body);
}

std::string label(uint32_t addr) {
    return std::format("L_{:08x}", addr);
}

// Shift by immediate as used by data processing and load/store offsets (no carry).
std::string shift_imm_expr(const std::string& v, uint32_t type, uint32_t amt) {
    switch (type) {
        case 0: return amt ? std::format("({} << {})", v, amt) : v;
        case 1: return amt ? std::format("({} >> {})", v, amt) : "0u";
        case 2: return std::format("uint32_t(int32_t({}) >> {})", v, amt ? amt : 31);
        default: return amt ? std::format("ror32({}, {})", v, amt) : std::format("((c.c << 31) | ({} >> 1))", v);
    }
}

}  // namespace

Translator::Translator(const Program& prog, const Module& mod, const Function& fn, Stats& stats)
    : prog_(prog), mod_(mod), fn_(fn), stats_(stats) {}

std::string Translator::reg(uint32_t n, uint32_t pc_value) const {
    return n == 15 ? hex32(pc_value) : std::format("c.r[{}]", n);
}

std::string Translator::unhandled(uint32_t addr, const std::string& what) {
    stats_.unhandled++;
    stats_.unhandled_kinds[what]++;
    return std::format("unhandled(c, {}, \"{}\");", hex32(addr), what);
}

// ---------------------------------------------------------------------------
// Analysis

int Translator::switch_case_count(uint32_t addr, uint32_t reg) const {
    // The upper bound comes from "cmp rN, #max" that guards the dispatch: either the
    // dispatch itself is conditional on LS/LE, or the cmp is followed by "bhi/bgt default".
    // Instructions between the cmp and a conditional dispatch must leave the flags and
    // rN alone ("cmp r1, #9; mov r5, #1; addls pc, pc, r1, lsl #2").
    uint32_t dispatch_cond = mod_.read32(addr).value_or(0) >> 28;
    bool flags_kept = true;
    for (uint32_t back = 4; back <= 24; back += 4) {
        auto raw = mod_.read32(addr - back);
        if (!raw) break;
        uint32_t r = *raw;
        if ((r & 0x0FF0F000) != 0x03500000 || ((r >> 16) & 0xF) != reg) {
            bool data_processing = (r & 0x0C000000) == 0 && (r & 0x0E000090) != 0x00000090;
            bool plain_mov = data_processing && !(r & 0x00100000) && ((r >> 12) & 0xF) != reg && (r >> 28) == 0xE;
            if (!plain_mov) flags_kept = false;
            continue;
        }
        int imm = int(std::rotr(r & 0xFF, ((r >> 8) & 0xF) * 2));
        if (flags_kept && (dispatch_cond == 0x9 || dispatch_cond == 0xD)) return imm;
        uint32_t next = mod_.read32(addr - back + 4).value_or(0);
        uint32_t cond = next >> 28;
        if ((next & 0x0F000000) == 0x0A000000 && (cond == 0x8 || cond == 0xC)) return imm;
    }
    // Otherwise count the branch table itself (unconditional B entries after the default).
    int n = 0;
    while (n < 256) {
        auto raw = mod_.read32(addr + 8 + 4 * n);
        if (!raw || (*raw & 0xFF000000) != 0xEA000000) break;
        n++;
    }
    return n - 1;
}

bool Translator::is_manual_call(uint32_t addr) const {
    auto raw = mod_.read32(addr);
    auto prev = mod_.read32(addr - 4);
    if (fn_.thumb || !raw || !prev || (*raw >> 28) != 0xE || (*prev >> 28) != 0xE) return false;
    bool bx = (*raw & 0x0FFFFFF0) == 0x012FFF10 && (*raw & 0xF) != 14;
    bool mov_pc = (*raw & 0x0FFFFFF0) == 0x01A0F000 && (*raw & 0xF) != 14;
    if (!bx && !mov_pc) return false;
    uint32_t lr;
    if ((*prev & 0x0FFFF000) == 0x028FE000) {  // add lr, pc, #imm
        lr = addr - 4 + 8 + std::rotr(*prev & 0xFF, ((*prev >> 8) & 0xF) * 2);
    } else if (*prev == 0xE1A0E00F) {  // mov lr, pc
        lr = addr - 4 + 8;
    } else {
        return false;
    }
    return lr == addr + 4;
}

Translator::Flow Translator::arm_flow(uint32_t addr, uint32_t raw) {
    Flow f;
    uint32_t cond = raw >> 28;
    bool always = cond == 0xE;
    if (cond == 0xF) {
        // BLX (immediate) and PLD are the only unconditional encodings we accept.
        f.falls_through = (raw & 0x0E000000) == 0x0A000000 || (raw & 0x0D70F000) == 0x0550F000;
        return f;
    }
    if ((raw & 0x0E000000) == 0x0A000000) {  // B / BL
        if (!(raw & 0x01000000)) {
            uint32_t target = addr + 8 + uint32_t(sext(raw & 0xFFFFFF, 24) * 4);
            if (is_local(target)) f.local_targets.push_back(target);
            f.falls_through = !always;
        }
        return f;
    }
    if ((raw & 0x0FFFFFF0) == 0x012FFF10) {  // BX
        f.falls_through = !always || is_manual_call(addr);
        return f;
    }
    bool dp = (raw & 0x0C000000) == 0 && !((raw & 0x0E000090) == 0x00000090) &&
              (raw & 0x0FBF0FFF) != 0x010F0000 && (raw & 0x0FB0FFF0) != 0x0120F000 &&
              (raw & 0x0FB0F000) != 0x0320F000 && (raw & 0x0FFFFFD0) != 0x012FFF10;
    uint32_t op = (raw >> 21) & 0xF;
    if (dp && ((raw >> 12) & 0xF) == 15 && !(op >= 8 && op <= 11)) {
        if (is_manual_call(addr)) return f;
        // add pc, pc, rM, lsl #2  -> jump table of branches after the default slot
        if (op == 4 && ((raw >> 16) & 0xF) == 15 && !(raw & 0x02000000) && (raw & 0xFF0) == 0x100) {
            int n = switch_case_count(addr, raw & 0xF);
            if (n >= 0) {
                std::vector<uint32_t> targets;
                for (int i = 0; i <= n; i++) targets.push_back(addr + 8 + 4 * i);
                switches_[addr] = targets;
                f.local_targets = targets;
            }
        }
        f.falls_through = !always;
        return f;
    }
    if ((raw & 0x0C100000) == 0x04100000 && ((raw >> 12) & 0xF) == 15) {  // LDR pc
        // ldr pc, [pc, rM, lsl #2]  -> table of absolute addresses after the default slot
        if ((raw & 0x0FFF0FF0) == 0x079FF100) {
            int n = switch_case_count(addr, raw & 0xF);
            if (n >= 0) {
                std::vector<uint32_t> targets;
                for (int i = 0; i <= n; i++) {
                    auto t = mod_.read32(addr + 8 + 4 * i);
                    if (!t || !is_local(*t)) { targets.clear(); break; }
                    targets.push_back(*t);
                }
                if (!targets.empty()) {
                    switches_[addr] = targets;
                    f.local_targets = targets;
                }
            }
        }
        f.falls_through = !always;
        return f;
    }
    if ((raw & 0x0E108000) == 0x08108000) {  // LDM with pc
        f.falls_through = !always;
        return f;
    }
    return f;
}

Translator::Flow Translator::thumb_flow(uint32_t addr, uint16_t raw, uint16_t next) {
    Flow f;
    if ((raw & 0xF000) == 0xD000) {
        uint32_t cond = (raw >> 8) & 0xF;
        if (cond == 0xE) { f.falls_through = false; return f; }
        if (cond != 0xF) {
            uint32_t target = addr + 4 + uint32_t(sext(raw & 0xFF, 8) * 2);
            if (is_local(target)) f.local_targets.push_back(target);
        }
        return f;
    }
    if ((raw & 0xF800) == 0xE000) {
        uint32_t target = addr + 4 + uint32_t(sext(raw & 0x7FF, 11) * 2);
        if (is_local(target)) f.local_targets.push_back(target);
        f.falls_through = false;
        return f;
    }
    if ((raw & 0xFF80) == 0x4700) {  // BX
        f.falls_through = false;
        return f;
    }
    if ((raw & 0xFC87) == 0x4487 && ((raw >> 8) & 3) != 1) {  // ADD/MOV pc, rM
        f.falls_through = false;
        return f;
    }
    if ((raw & 0xFF00) == 0xBD00) {  // POP {.., pc}
        f.falls_through = false;
        return f;
    }
    (void)next;
    return f;
}

void Translator::explore() {
    std::deque<uint32_t> work{fn_.addr};
    std::map<uint32_t, bool> falls;
    while (!work.empty()) {
        uint32_t addr = work.front();
        work.pop_front();
        if (insns_.count(addr) || addr < fn_.addr || addr >= fn_.body_end) continue;
        Flow f;
        uint32_t len;
        if (fn_.thumb) {
            auto raw = mod_.read16(addr);
            if (!raw) continue;
            uint16_t next = mod_.read16(addr + 2).value_or(0);
            bool pair = (*raw & 0xF800) == 0xF000 && (next & 0xE800) == 0xE800;
            len = pair ? 4 : 2;
            f = thumb_flow(addr, *raw, next);
        } else {
            auto raw = mod_.read32(addr);
            if (!raw) continue;
            len = 4;
            f = arm_flow(addr, *raw);
        }
        insns_[addr] = len;
        if (f.falls_through) work.push_back(addr + len);
        for (uint32_t t : f.local_targets) {
            labels_.insert(t);
            work.push_back(t);
        }
    }
    // An ARM function that never stores lr cannot pop a return address: its lone
    // "ldmia sp!, {pc}" jumps to a value it pushed itself (the DS Protect gates in ov129).
    // Pops with more registers are left alone: their lr may be saved by another chunk.
    if (!fn_.thumb) {
        saves_lr_ = false;
        for (auto [addr, len] : insns_) {
            uint32_t raw = *mod_.read32(addr);
            bool stm = (raw & 0x0E100000) == 0x08000000 && (raw & 0x4000);
            bool str = (raw & 0x0C100000) == 0x04000000 && ((raw >> 12) & 0xF) == 14;
            if (stm || str) saves_lr_ = true;
        }
    }
}

// ---------------------------------------------------------------------------
// Control transfer helpers

std::string Translator::branch_to(uint32_t target, bool thumb) {
    if (is_local(target) && insns_.count(target)) {
        // Backward branches close loops: give the runtime a chance to advance time.
        if (target <= cur_addr_) return std::format("{{ if (--c.budget < 0) poll(c); goto {}; }}", label(target));
        return std::format("goto {};", label(target));
    }
    if (const Function* f = prog_.resolve_call(mod_, target)) {
        const Module* owner = mod_.contains(target) ? &mod_ : nullptr;
        if (!owner) {
            for (const auto& m : prog_.modules) {
                if (!m.is_overlay() && m.contains(target)) owner = &m;
            }
        }
        return std::format("{{ {}(c); return; }}", symbol_for(*owner, *f));
    }
    if (mod_.contains(target)) stats_.missing_entries.insert({mod_.name, target | (thumb ? 1 : 0)});
    return std::format("{{ call_indirect(c, {}); return; }}", hex32(target | (thumb ? 1 : 0)));
}

std::string Translator::call_to(uint32_t target, bool thumb, uint32_t ret) {
    std::string set_lr = std::format("c.r[14] = {};", hex32(ret));
    if (const Function* f = prog_.resolve_call(mod_, target)) {
        const Module* owner = mod_.contains(target) ? &mod_ : nullptr;
        if (!owner) {
            for (const auto& m : prog_.modules) {
                if (!m.is_overlay() && m.contains(target)) owner = &m;
            }
        }
        return std::format("{{ {} {}(c); }}", set_lr, symbol_for(*owner, *f));
    }
    if (mod_.contains(target)) stats_.missing_entries.insert({mod_.name, target | (thumb ? 1 : 0)});
    return std::format("{{ {} call_indirect(c, {}); }}", set_lr, hex32(target | (thumb ? 1 : 0)));
}

std::string Translator::jump_indirect(const std::string& target_expr) {
    return std::format("call_indirect(c, {}); return;", target_expr);
}

// ---------------------------------------------------------------------------
// ARM

std::string Translator::arm_operand2(uint32_t addr, uint32_t raw, bool want_carry, std::string& carry) {
    carry.clear();
    if (raw & 0x02000000) {
        uint32_t rot = ((raw >> 8) & 0xF) * 2;
        uint32_t value = std::rotr(raw & 0xFF, rot);
        if (want_carry && rot) carry = std::to_string(value >> 31) + "u";
        return hex32(value);
    }
    uint32_t rm = raw & 0xF;
    uint32_t type = (raw >> 5) & 3;
    if (raw & 0x10) {
        std::string v = reg(rm, addr + 12);
        std::string s = reg((raw >> 8) & 0xF, addr + 12);
        static constexpr const char* kFns[4] = {"lsl_reg", "lsr_reg", "asr_reg", "ror_reg"};
        return want_carry ? std::format("{}_c(c, {}, {})", kFns[type], v, s) : std::format("{}({}, {})", kFns[type], v, s);
    }
    uint32_t amt = (raw >> 7) & 31;
    std::string v = reg(rm, addr + 8);
    if (want_carry) {
        switch (type) {
            case 0: if (amt) carry = std::format("(({} >> {}) & 1)", v, 32 - amt); break;
            case 1: carry = amt ? std::format("(({} >> {}) & 1)", v, amt - 1) : std::format("({} >> 31)", v); break;
            case 2: carry = amt ? std::format("(({} >> {}) & 1)", v, amt - 1) : std::format("({} >> 31)", v); break;
            default: carry = amt ? std::format("(({} >> {}) & 1)", v, amt - 1) : std::format("({} & 1)", v); break;
        }
    }
    return shift_imm_expr(v, type, amt);
}

std::string Translator::arm_data_processing(uint32_t addr, uint32_t raw) {
    uint32_t op = (raw >> 21) & 0xF;
    bool s = raw & 0x00100000;
    uint32_t rn = (raw >> 16) & 0xF;
    uint32_t rd = (raw >> 12) & 0xF;
    bool test = op >= 8 && op <= 11;
    bool logical = op <= 1 || op == 8 || op == 9 || op >= 12;
    bool reg_shift = !(raw & 0x02000000) && (raw & 0x10);

    if (rd == 15 && !test && switches_.count(addr)) {
        std::string body = std::format("{{ switch (c.r[{}]) {{", raw & 0xF);
        const auto& targets = switches_.at(addr);
        for (size_t i = 0; i < targets.size(); i++) {
            body += std::format(" case {}: goto {};", i, label(targets[i]));
        }
        return body + " default: break; } }";
    }
    if (rd == 15 && !test && !s && op == 13 && (raw & 0x02000FFF) == 0x0000000E) {
        return "return;";  // mov pc, lr
    }
    if (rd == 15 && is_manual_call(addr)) {
        return std::format("{{ call_indirect(c, c.r[{}]); }}", raw & 0xF);
    }
    if (rn == 15 && (raw & 0x02000000) && !s && (op == 2 || op == 4) && rd != 15) {
        uint32_t imm = std::rotr(raw & 0xFF, ((raw >> 8) & 0xF) * 2);
        uint32_t value = op == 4 ? addr + 8 + imm : addr + 8 - imm;
        if (is_local(value)) stats_.pc_relative_refs.insert({mod_.name, value});
    }

    std::string carry;
    std::string op2 = arm_operand2(addr, raw, s && logical, carry);
    std::string a = reg(rn, addr + (reg_shift ? 12 : 8));
    std::ostringstream o;
    o << "{ uint32_t op2 = " << op2 << ";";
    if (!carry.empty()) o << " uint32_t sc = " << carry << ";";
    std::string res;
    switch (op) {
        case 0: case 8: res = std::format("{} & op2", a); break;
        case 1: case 9: res = std::format("{} ^ op2", a); break;
        case 2: case 10: res = s || op == 10 ? std::format("sub_flags(c, {}, op2)", a) : std::format("{} - op2", a); break;
        case 3: res = s ? std::format("sub_flags(c, op2, {})", a) : std::format("op2 - {}", a); break;
        case 4: case 11: res = s || op == 11 ? std::format("add_flags(c, {}, op2)", a) : std::format("{} + op2", a); break;
        case 5: res = s ? std::format("add_flags(c, {}, op2, c.c)", a) : std::format("{} + op2 + c.c", a); break;
        case 6: res = s ? std::format("sub_flags(c, {}, op2, c.c)", a) : std::format("{} - op2 - (c.c ^ 1)", a); break;
        case 7: res = s ? std::format("sub_flags(c, op2, {}, c.c)", a) : std::format("op2 - {} - (c.c ^ 1)", a); break;
        case 12: res = std::format("{} | op2", a); break;
        case 13: res = "op2"; break;
        case 14: res = std::format("{} & ~op2", a); break;
        default: res = "~op2"; break;
    }
    o << " uint32_t res = " << res << ";";
    if ((s || test) && logical) {
        o << " set_nz(c, res);";
        if (!carry.empty()) o << " c.c = sc;";
    }
    if (!test) {
        if (rd == 15) {
            if (s) {
                o << " c.r[15] = res; exception_return(c); return;";
            } else {
                o << " " << jump_indirect("res & ~3u");
            }
        } else {
            o << " c.r[" << rd << "] = res;";
        }
    } else {
        o << " (void)res;";
    }
    o << " }";
    return o.str();
}

std::string Translator::arm_multiply(uint32_t raw) {
    uint32_t rd = (raw >> 16) & 0xF, rn = (raw >> 12) & 0xF, rs = (raw >> 8) & 0xF, rm = raw & 0xF;
    bool s = raw & 0x00100000;
    if ((raw & 0x0FC000F0) == 0x00000090) {
        std::string acc = (raw & 0x00200000) ? std::format(" + c.r[{}]", rn) : "";
        return std::format("{{ uint32_t res = c.r[{}] * c.r[{}]{}; c.r[{}] = res;{} }}", rm, rs, acc, rd,
                           s ? " set_nz(c, res);" : "");
    }
    bool is_signed = raw & 0x00400000;
    std::string prod = is_signed ? std::format("uint64_t(int64_t(int32_t(c.r[{}])) * int32_t(c.r[{}]))", rm, rs)
                                 : std::format("uint64_t(c.r[{}]) * c.r[{}]", rm, rs);
    std::string acc = (raw & 0x00200000) ? std::format(" + ((uint64_t(c.r[{}]) << 32) | c.r[{}])", rd, rn) : "";
    return std::format("{{ uint64_t p = {}{}; c.r[{}] = uint32_t(p); c.r[{}] = uint32_t(p >> 32);{} }}", prod, acc,
                       rn, rd, s ? " c.n = uint32_t(p >> 63); c.z = p == 0;" : "");
}

std::string Translator::arm_dsp(uint32_t raw) {
    uint32_t op = (raw >> 21) & 3;
    uint32_t rd = (raw >> 16) & 0xF, rn = (raw >> 12) & 0xF, rs = (raw >> 8) & 0xF, rm = raw & 0xF;
    auto half = [](uint32_t r, bool top) {
        return top ? std::format("int32_t(int16_t(c.r[{}] >> 16))", r) : std::format("int32_t(int16_t(c.r[{}]))", r);
    };
    std::string hx = half(rm, raw & 0x20), hy = half(rs, raw & 0x40);
    switch (op) {
        case 0:
            return std::format("{{ int64_t s = int64_t({} * {}) + int32_t(c.r[{}]); if (s != int32_t(s)) c.q = 1; c.r[{}] = uint32_t(s); }}",
                               hx, hy, rn, rd);
        case 1:
            if (raw & 0x20) {
                return std::format("c.r[{}] = uint32_t(int32_t((int64_t(int32_t(c.r[{}])) * {}) >> 16));", rd, rm, hy);
            }
            return std::format("{{ int64_t s = ((int64_t(int32_t(c.r[{}])) * {}) >> 16) + int32_t(c.r[{}]); if (s != int32_t(s)) c.q = 1; c.r[{}] = uint32_t(s); }}",
                               rm, hy, rn, rd);
        case 2:
            return std::format("{{ uint64_t acc = ((uint64_t(c.r[{0}]) << 32) | c.r[{1}]) + uint64_t(int64_t({2} * {3})); c.r[{1}] = uint32_t(acc); c.r[{0}] = uint32_t(acc >> 32); }}",
                               rd, rn, hx, hy);
        default:
            return std::format("c.r[{}] = uint32_t({} * {});", rd, hx, hy);
    }
}

std::string Translator::arm_load_store(uint32_t addr, uint32_t raw) {
    bool p = raw & 0x01000000, u = raw & 0x00800000, b = raw & 0x00400000, w = raw & 0x00200000, l = raw & 0x00100000;
    uint32_t rn = (raw >> 16) & 0xF, rd = (raw >> 12) & 0xF;
    char sign = u ? '+' : '-';

    if (l && rd == 15 && switches_.count(addr)) {
        std::string body = std::format("{{ switch (c.r[{}]) {{", raw & 0xF);
        const auto& targets = switches_.at(addr);
        for (size_t i = 0; i < targets.size(); i++) {
            body += std::format(" case {}: goto {};", i, label(targets[i]));
        }
        return body + " default: break; } }";
    }

    std::string off;
    if (raw & 0x02000000) {
        if (raw & 0x10) return unhandled(addr, "arm undefined (ldr/str reg shift)");
        off = shift_imm_expr(reg(raw & 0xF, addr + 8), (raw >> 5) & 3, (raw >> 7) & 31);
    } else {
        uint32_t imm = raw & 0xFFF;
        if (l && !b && rn == 15 && p && !w) {
            uint32_t a = addr + 8 + (u ? imm : -imm);
            auto value = mod_.read32(a);
            if (value && (a & 3) == 0) {
                if (rd == 15) return branch_to(*value & ~1u, *value & 1);
                return std::format("c.r[{}] = {};", rd, hex32(*value));
            }
        }
        off = hex32(imm);
    }

    std::ostringstream o;
    o << "{ uint32_t base = " << reg(rn, addr + 8) << "; uint32_t off = " << off << ";";
    o << " uint32_t a = " << (p ? std::format("base {} off", sign) : "base") << ";";
    bool writeback = (w || !p) && rn != 15;
    std::string wb = writeback ? std::format(" c.r[{}] = {};", rn, p ? "a" : std::format("base {} off", sign)) : "";
    if (l) {
        o << " uint32_t v = " << (b ? "rd8(a)" : "ldr32(a)") << ";" << wb;
        if (rd == 15) {
            o << (rn == 13 && !p && u ? " return;" : " " + jump_indirect("v"));
        } else {
            o << " c.r[" << rd << "] = v;";
        }
    } else {
        o << " uint32_t v = " << reg(rd, addr + 12) << ";";
        o << (b ? " wr8(a, uint8_t(v));" : " wr32(a, v);") << wb;
    }
    o << " }";
    return o.str();
}

std::string Translator::arm_halfword(uint32_t addr, uint32_t raw) {
    bool p = raw & 0x01000000, u = raw & 0x00800000, w = raw & 0x00200000, l = raw & 0x00100000;
    uint32_t rn = (raw >> 16) & 0xF, rd = (raw >> 12) & 0xF, sh = (raw >> 5) & 3;
    char sign = u ? '+' : '-';
    std::string off = (raw & 0x00400000) ? hex32(((raw >> 4) & 0xF0) | (raw & 0xF)) : reg(raw & 0xF, addr + 8);
    if (rd == 15) return unhandled(addr, "arm halfword transfer with pc");

    std::ostringstream o;
    o << "{ uint32_t base = " << reg(rn, addr + 8) << "; uint32_t off = " << off << ";";
    o << " uint32_t a = " << (p ? std::format("base {} off", sign) : "base") << ";";
    bool writeback = (w || !p) && rn != 15;
    std::string wb = writeback ? std::format(" c.r[{}] = {};", rn, p ? "a" : std::format("base {} off", sign)) : "";
    if (l) {
        static constexpr const char* kLoads[4] = {"", "rd16(a)", "uint32_t(int32_t(int8_t(rd8(a))))", "uint32_t(int32_t(int16_t(rd16(a))))"};
        o << " uint32_t v = " << kLoads[sh] << ";" << wb << " c.r[" << rd << "] = v;";
    } else if (sh == 1) {
        o << " wr16(a, uint16_t(" << reg(rd, addr + 12) << "));" << wb;
    } else if (sh == 2) {  // LDRD
        o << " uint32_t v0 = rd32(a), v1 = rd32(a + 4);" << wb << " c.r[" << rd << "] = v0; c.r[" << rd + 1 << "] = v1;";
    } else {  // STRD
        o << " wr32(a, c.r[" << rd << "]); wr32(a + 4, c.r[" << rd + 1 << "]);" << wb;
    }
    o << " }";
    return o.str();
}

std::string Translator::arm_block(uint32_t addr, uint32_t raw) {
    bool p = raw & 0x01000000, u = raw & 0x00800000, s = raw & 0x00400000, w = raw & 0x00200000, l = raw & 0x00100000;
    uint32_t rn = (raw >> 16) & 0xF;
    uint32_t list = raw & 0xFFFF;
    uint32_t n = std::popcount(list);
    if (n == 0) return unhandled(addr, "arm ldm/stm with empty list");
    std::string start = u ? (p ? "base + 4" : "base") : (p ? std::format("base - {}", 4 * n) : std::format("base - {}", 4 * n - 4));
    std::string wb = w ? std::format(" c.r[{}] = base {} {};", rn, u ? '+' : '-', 4 * n) : "";

    std::ostringstream o;
    o << "{ uint32_t base = c.r[" << rn << "]; uint32_t a = " << start << ";";
    if (l) {
        o << wb;
        for (uint32_t i = 0; i < 16; i++) {
            if (!(list & (1u << i))) continue;
            o << (i == 15 ? " uint32_t pc = rd32(a);" : std::format(" c.r[{}] = rd32(a);", i)) << " a += 4;";
        }
        if (list & 0x8000) {
            if (s) o << " c.r[15] = pc; exception_return(c); return;";
            else if (rn == 13 && (saves_lr_ || list != 0x8000)) o << " (void)pc; return;";
            else o << " " << jump_indirect("pc");
        }
    } else {
        for (uint32_t i = 0; i < 16; i++) {
            if (!(list & (1u << i))) continue;
            o << std::format(" wr32(a, {}); a += 4;", reg(i, addr + 12));
        }
        o << wb;
    }
    o << " }";
    return o.str();
}

std::string Translator::arm(uint32_t addr, uint32_t raw) {
    uint32_t cond = raw >> 28;
    if (cond == 0xF) {
        if ((raw & 0x0E000000) == 0x0A000000) {  // BLX immediate
            uint32_t target = addr + 8 + uint32_t(sext(raw & 0xFFFFFF, 24) * 4) + ((raw >> 23) & 2);
            return call_to(target, true, addr + 4);
        }
        if ((raw & 0x0D70F000) == 0x0550F000) return "/* pld */";
        return unhandled(addr, "arm unconditional encoding");
    }

    std::string body;
    switch ((raw >> 25) & 7) {
        case 0:
        case 1:
            if ((raw & 0x0FFFFFF0) == 0x012FFF10) {  // BX
                uint32_t rm = raw & 0xF;
                if (is_manual_call(addr)) body = std::format("call_indirect(c, c.r[{}]);", rm);
                else body = rm == 14 ? "return;" : jump_indirect(std::format("c.r[{}]", rm));
                body = "{ " + body + " }";
            } else if ((raw & 0x0FFFFFF0) == 0x012FFF30) {  // BLX register
                body = std::format("{{ uint32_t t = c.r[{}]; c.r[14] = {}; call_indirect(c, t); }}", raw & 0xF, hex32(addr + 4));
            } else if ((raw & 0x0FFF0FF0) == 0x016F0F10) {
                body = std::format("c.r[{}] = clz32(c.r[{}]);", (raw >> 12) & 0xF, raw & 0xF);
            } else if ((raw & 0x0F900FF0) == 0x01000050) {
                uint32_t rd = (raw >> 12) & 0xF, rn = (raw >> 16) & 0xF, rm = raw & 0xF;
                static constexpr const char* kQ[4] = {"qadd(c, c.r[{1}], c.r[{2}])", "qsub(c, c.r[{1}], c.r[{2}])",
                                                     "qadd(c, c.r[{1}], qadd(c, c.r[{2}], c.r[{2}]))",
                                                     "qsub(c, c.r[{1}], qadd(c, c.r[{2}], c.r[{2}]))"};
                body = std::format("c.r[{}] = ", rd) + std::vformat(kQ[(raw >> 21) & 3], std::make_format_args(rd, rm, rn)) + ";";
            } else if ((raw & 0x0F900090) == 0x01000080) {
                body = arm_dsp(raw);
            } else if ((raw & 0x0FC000F0) == 0x00000090 || (raw & 0x0F8000F0) == 0x00800090) {
                body = arm_multiply(raw);
            } else if ((raw & 0x0FB00FF0) == 0x01000090) {  // SWP / SWPB
                uint32_t rn = (raw >> 16) & 0xF, rd = (raw >> 12) & 0xF, rm = raw & 0xF;
                body = (raw & 0x00400000)
                    ? std::format("{{ uint32_t a = c.r[{}]; uint32_t v = rd8(a); wr8(a, uint8_t(c.r[{}])); c.r[{}] = v; }}", rn, rm, rd)
                    : std::format("{{ uint32_t a = c.r[{}]; uint32_t v = ldr32(a); wr32(a, c.r[{}]); c.r[{}] = v; }}", rn, rm, rd);
            } else if ((raw & 0x0E000090) == 0x00000090) {
                body = arm_halfword(addr, raw);
            } else if ((raw & 0x0FBF0FFF) == 0x010F0000) {  // MRS
                body = std::format("c.r[{}] = {}(c);", (raw >> 12) & 0xF, (raw & 0x00400000) ? "spsr_read" : "cpsr_read");
            } else if ((raw & 0x0FB0FFF0) == 0x0120F000 || (raw & 0x0FB0F000) == 0x0320F000) {  // MSR
                uint32_t mask = 0;
                for (int i = 0; i < 4; i++) {
                    if (raw & (1u << (16 + i))) mask |= 0xFFu << (8 * i);
                }
                std::string value = (raw & 0x02000000) ? hex32(std::rotr(raw & 0xFF, ((raw >> 8) & 0xF) * 2))
                                                       : std::format("c.r[{}]", raw & 0xF);
                body = std::format("{}(c, {}, {});", (raw & 0x00400000) ? "spsr_write" : "cpsr_write", value, hex32(mask));
            } else {
                body = arm_data_processing(addr, raw);
            }
            break;
        case 2:
        case 3:
            body = arm_load_store(addr, raw);
            break;
        case 4:
            body = arm_block(addr, raw);
            break;
        case 5: {
            uint32_t target = addr + 8 + uint32_t(sext(raw & 0xFFFFFF, 24) * 4);
            body = (raw & 0x01000000) ? call_to(target, false, addr + 4) : branch_to(target, false);
            break;
        }
        case 6:
            body = unhandled(addr, "arm coprocessor load/store");
            break;
        default:
            if (raw & 0x01000000) {
                body = std::format("swi(c, {});", (raw >> 16) & 0xFF);
            } else if (raw & 0x10) {
                uint32_t cp = (raw >> 8) & 0xF, op1 = (raw >> 21) & 7, crn = (raw >> 16) & 0xF, rd = (raw >> 12) & 0xF,
                         crm = raw & 0xF, op2 = (raw >> 5) & 7;
                if (raw & 0x00100000) {
                    body = rd == 15 ? unhandled(addr, "arm mrc to pc")
                                    : std::format("c.r[{}] = mrc(c, {}, {}, {}, {}, {});", rd, cp, op1, crn, crm, op2);
                } else {
                    body = std::format("mcr(c, {}, {}, {}, {}, {}, {});", cp, op1, reg(rd, addr + 12), crn, crm, op2);
                }
            } else {
                body = unhandled(addr, "arm cdp");
            }
            break;
    }
    return wrap_cond(cond, body);
}

// ---------------------------------------------------------------------------
// Thumb

std::string Translator::thumb(uint32_t addr, uint16_t raw, uint16_t next) {
    uint32_t pc = addr + 4;
    uint32_t rd = raw & 7, rs = (raw >> 3) & 7;

    if (raw < 0x1800) {  // shift by immediate
        uint32_t op = (raw >> 11) & 3, amt = (raw >> 6) & 31;
        std::string v = std::format("c.r[{}]", rs);
        std::string body;
        if (op == 0) {
            body = amt ? std::format("c.c = ({} >> {}) & 1; uint32_t res = {} << {};", v, 32 - amt, v, amt)
                       : std::format("uint32_t res = {};", v);
        } else if (op == 1) {
            body = amt ? std::format("c.c = ({} >> {}) & 1; uint32_t res = {} >> {};", v, amt - 1, v, amt)
                       : std::format("c.c = {} >> 31; uint32_t res = 0;", v);
        } else {
            body = amt ? std::format("c.c = ({} >> {}) & 1; uint32_t res = uint32_t(int32_t({}) >> {});", v, amt - 1, v, amt)
                       : std::format("c.c = {} >> 31; uint32_t res = uint32_t(int32_t({}) >> 31);", v, v);
        }
        return std::format("{{ {} set_nz(c, res); c.r[{}] = res; }}", body, rd);
    }
    if (raw < 0x2000) {  // add/sub register or imm3
        uint32_t rn = (raw >> 6) & 7;
        std::string operand = (raw & 0x0400) ? hex32(rn) : std::format("c.r[{}]", rn);
        const char* fn = (raw & 0x0200) ? "sub_flags" : "add_flags";
        return std::format("c.r[{}] = {}(c, c.r[{}], {});", rd, fn, rs, operand);
    }
    if (raw < 0x4000) {  // mov/cmp/add/sub imm8
        uint32_t r = (raw >> 8) & 7, imm = raw & 0xFF;
        switch ((raw >> 11) & 3) {
            case 0: return std::format("{{ c.r[{}] = {}; set_nz(c, {}); }}", r, hex32(imm), hex32(imm));
            case 1: return std::format("sub_flags(c, c.r[{}], {});", r, hex32(imm));
            case 2: return std::format("c.r[{0}] = add_flags(c, c.r[{0}], {1});", r, hex32(imm));
            default: return std::format("c.r[{0}] = sub_flags(c, c.r[{0}], {1});", r, hex32(imm));
        }
    }
    if (raw < 0x4400) {  // ALU
        std::string d = std::format("c.r[{}]", rd), s = std::format("c.r[{}]", rs);
        switch ((raw >> 6) & 0xF) {
            case 0: return std::format("{{ {0} &= {1}; set_nz(c, {0}); }}", d, s);
            case 1: return std::format("{{ {0} ^= {1}; set_nz(c, {0}); }}", d, s);
            case 2: return std::format("{{ {0} = lsl_reg_c(c, {0}, {1}); set_nz(c, {0}); }}", d, s);
            case 3: return std::format("{{ {0} = lsr_reg_c(c, {0}, {1}); set_nz(c, {0}); }}", d, s);
            case 4: return std::format("{{ {0} = asr_reg_c(c, {0}, {1}); set_nz(c, {0}); }}", d, s);
            case 5: return std::format("{0} = add_flags(c, {0}, {1}, c.c);", d, s);
            case 6: return std::format("{0} = sub_flags(c, {0}, {1}, c.c);", d, s);
            case 7: return std::format("{{ {0} = ror_reg_c(c, {0}, {1}); set_nz(c, {0}); }}", d, s);
            case 8: return std::format("set_nz(c, {} & {});", d, s);
            case 9: return std::format("{} = sub_flags(c, 0, {});", d, s);
            case 10: return std::format("sub_flags(c, {}, {});", d, s);
            case 11: return std::format("add_flags(c, {}, {});", d, s);
            case 12: return std::format("{{ {0} |= {1}; set_nz(c, {0}); }}", d, s);
            case 13: return std::format("{{ {0} *= {1}; set_nz(c, {0}); }}", d, s);
            case 14: return std::format("{{ {0} &= ~{1}; set_nz(c, {0}); }}", d, s);
            default: return std::format("{{ {0} = ~{1}; set_nz(c, {0}); }}", d, s);
        }
    }
    if (raw < 0x4800) {  // hi register operations / BX / BLX
        uint32_t op = (raw >> 8) & 3;
        uint32_t hd = rd | ((raw >> 4) & 8), hm = (raw >> 3) & 0xF;
        std::string m = reg(hm, pc);
        if (op == 3) {
            if (raw & 0x80) {
                return std::format("{{ uint32_t t = {}; c.r[14] = {}; call_indirect(c, t); }}", m, hex32((addr + 2) | 1));
            }
            return hm == 14 ? "return;" : "{ " + jump_indirect(m) + " }";
        }
        if (op == 1) return std::format("sub_flags(c, {}, {});", reg(hd, pc), m);
        if (hd == 15) {
            if (op == 2 && hm == 14) return "return;";
            std::string target = op == 0 ? std::format("{} + {}", hex32(pc), m) : m;
            return std::format("{{ uint32_t t = {}; {} }}", target, jump_indirect("t | 1u"));
        }
        return op == 0 ? std::format("c.r[{0}] += {1};", hd, m) : std::format("c.r[{}] = {};", hd, m);
    }
    if (raw < 0x5000) {  // ldr rd, [pc, #imm]
        uint32_t a = (pc & ~3u) + (raw & 0xFF) * 4;
        auto value = mod_.read32(a);
        uint32_t r = (raw >> 8) & 7;
        return value ? std::format("c.r[{}] = {};", r, hex32(*value)) : std::format("c.r[{}] = rd32({});", r, hex32(a));
    }
    if (raw < 0x6000) {  // load/store register offset
        uint32_t ro = (raw >> 6) & 7;
        std::string a = std::format("c.r[{}] + c.r[{}]", rs, ro);
        switch ((raw >> 9) & 7) {
            case 0: return std::format("wr32({}, c.r[{}]);", a, rd);
            case 1: return std::format("wr16({}, uint16_t(c.r[{}]));", a, rd);
            case 2: return std::format("wr8({}, uint8_t(c.r[{}]));", a, rd);
            case 3: return std::format("c.r[{}] = uint32_t(int32_t(int8_t(rd8({}))));", rd, a);
            case 4: return std::format("c.r[{}] = ldr32({});", rd, a);
            case 5: return std::format("c.r[{}] = rd16({});", rd, a);
            case 6: return std::format("c.r[{}] = rd8({});", rd, a);
            default: return std::format("c.r[{}] = uint32_t(int32_t(int16_t(rd16({}))));", rd, a);
        }
    }
    if (raw < 0x8000) {  // load/store immediate offset (word/byte)
        bool byte = raw & 0x1000, load = raw & 0x0800;
        uint32_t off = ((raw >> 6) & 31) * (byte ? 1 : 4);
        std::string a = std::format("c.r[{}] + {}", rs, hex32(off));
        if (load) return byte ? std::format("c.r[{}] = rd8({});", rd, a) : std::format("c.r[{}] = ldr32({});", rd, a);
        return byte ? std::format("wr8({}, uint8_t(c.r[{}]));", a, rd) : std::format("wr32({}, c.r[{}]);", a, rd);
    }
    if (raw < 0x9000) {  // halfword immediate offset
        std::string a = std::format("c.r[{}] + {}", rs, hex32(((raw >> 6) & 31) * 2));
        return (raw & 0x0800) ? std::format("c.r[{}] = rd16({});", rd, a) : std::format("wr16({}, uint16_t(c.r[{}]));", a, rd);
    }
    if (raw < 0xA000) {  // sp-relative
        uint32_t r = (raw >> 8) & 7;
        std::string a = std::format("c.r[13] + {}", hex32((raw & 0xFF) * 4));
        return (raw & 0x0800) ? std::format("c.r[{}] = ldr32({});", r, a) : std::format("wr32({}, c.r[{}]);", a, r);
    }
    if (raw < 0xB000) {  // add rd, pc/sp, #imm
        uint32_t r = (raw >> 8) & 7, imm = (raw & 0xFF) * 4;
        return (raw & 0x0800) ? std::format("c.r[{}] = c.r[13] + {};", r, hex32(imm))
                              : std::format("c.r[{}] = {};", r, hex32((pc & ~3u) + imm));
    }
    if (raw < 0xC000) {  // misc
        if ((raw & 0xFF00) == 0xB000) {
            uint32_t imm = (raw & 0x7F) * 4;
            return std::format("c.r[13] {}= {};", (raw & 0x80) ? '-' : '+', hex32(imm));
        }
        if ((raw & 0xF600) == 0xB400) {
            bool pop = raw & 0x0800, extra = raw & 0x0100;
            uint32_t list = raw & 0xFF;
            uint32_t n = std::popcount(list) + (extra ? 1 : 0);
            std::ostringstream o;
            if (!pop) {
                o << "{ uint32_t a = c.r[13] - " << 4 * n << "; c.r[13] = a;";
                for (uint32_t i = 0; i < 8; i++) {
                    if (list & (1u << i)) o << " wr32(a, c.r[" << i << "]); a += 4;";
                }
                if (extra) o << " wr32(a, c.r[14]);";
                o << " }";
            } else {
                o << "{ uint32_t a = c.r[13]; c.r[13] = a + " << 4 * n << ";";
                for (uint32_t i = 0; i < 8; i++) {
                    if (list & (1u << i)) o << " c.r[" << i << "] = rd32(a); a += 4;";
                }
                if (extra) o << " return;";
                o << " }";
            }
            return o.str();
        }
        return unhandled(addr, "thumb misc encoding");
    }
    if (raw < 0xD000) {  // ldmia / stmia
        uint32_t rb = (raw >> 8) & 7, list = raw & 0xFF;
        if (!list) return unhandled(addr, "thumb ldm/stm with empty list");
        bool load = raw & 0x0800;
        std::ostringstream o;
        o << "{ uint32_t a = c.r[" << rb << "];";
        if (load) {
            if (!(list & (1u << rb))) o << " c.r[" << rb << "] = a + " << 4 * std::popcount(list) << ";";
            for (uint32_t i = 0; i < 8; i++) {
                if (list & (1u << i)) o << " c.r[" << i << "] = rd32(a); a += 4;";
            }
        } else {
            for (uint32_t i = 0; i < 8; i++) {
                if (list & (1u << i)) o << " wr32(a, c.r[" << i << "]); a += 4;";
            }
            o << " c.r[" << rb << "] = a;";
        }
        o << " }";
        return o.str();
    }
    if (raw < 0xE000) {  // conditional branch / swi
        uint32_t cond = (raw >> 8) & 0xF;
        if (cond == 0xF) return std::format("swi(c, {});", raw & 0xFF);
        if (cond == 0xE) return unhandled(addr, "thumb undefined");
        uint32_t target = pc + uint32_t(sext(raw & 0xFF, 8) * 2);
        return wrap_cond(cond, branch_to(target, true));
    }
    if (raw < 0xE800) {
        return branch_to(pc + uint32_t(sext(raw & 0x7FF, 11) * 2), true);
    }
    if ((raw & 0xF800) == 0xF000 && (next & 0xE800) == 0xE800) {
        uint32_t target = pc + uint32_t(sext(raw & 0x7FF, 11) << 12) + (next & 0x7FF) * 2;
        bool blx = (next & 0xF800) == 0xE800;
        if (blx) target &= ~3u;
        return call_to(target, !blx, (addr + 4) | 1);
    }
    return unhandled(addr, "thumb bl/blx half");
}

// ---------------------------------------------------------------------------

std::string Translator::translate() {
    stats_.functions++;
    std::ostringstream o;
    o << "// " << fn_.name << (fn_.thumb ? " (thumb)" : " (arm)") << "\n";
    std::string sym = symbol_for(mod_, fn_);
    if (!fn_.hle.empty()) {
        // The HLE hook receives the recompiled original, so it can wrap instead of replace.
        stats_.hle_ids.insert(fn_.hle);
        o << "void " << sym << "(ntr::Cpu& c) {\n    ntr::hle_" << fn_.hle << "(c, &orig_" << sym << ");\n}\n\n";
        sym = "orig_" + sym;
    }
    o << "void " << sym << "(ntr::Cpu& c) {\n";
    o << "    using namespace ntr;\n";
    o << "    NTR_ENTER(" << hex32(fn_.addr) << ");\n";
    if (fn_.size == 0) {
        o << "    " << unhandled(fn_.addr, "entry outside any function") << "\n}\n\n";
        return o.str();
    }

    explore();
    for (auto it = insns_.begin(); it != insns_.end(); ++it) {
        auto [addr, len] = *it;
        stats_.instructions++;
        if (labels_.count(addr)) o << label(addr) << ":;\n";
        std::string code;
        bool ends;
        cur_addr_ = addr;
        if (fn_.thumb) {
            uint16_t raw = *mod_.read16(addr);
            uint16_t next = mod_.read16(addr + 2).value_or(0);
            code = thumb(addr, raw, next);
            ends = !thumb_flow(addr, raw, next).falls_through;
            o << std::format("    /* {:08x}: {:04x} */ ", addr, raw);
        } else {
            uint32_t raw = *mod_.read32(addr);
            code = arm(addr, raw);
            ends = !arm_flow(addr, raw).falls_through;
            o << std::format("    /* {:08x}: {:08x} */ ", addr, raw);
        }
        o << code << "\n";
        // Fall-through into code we did not emit: the next function, or an error.
        uint32_t next_addr = addr + len;
        auto nx = std::next(it);
        if (!ends && (nx == insns_.end() || nx->first != next_addr)) {
            if (const Function* f = mod_.function_at(next_addr)) {
                o << "    " << symbol_for(mod_, *f) << "(c); return;\n";
            } else {
                o << "    " << unhandled(next_addr, "fell off function end") << "\n";
            }
        }
    }
    o << "}\n\n";
    return o.str();
}

}  // namespace recomp
