// ARMv5TE interpreter (ARM state) for code the static recompiler cannot express: hand-
// written assembly that jumps through computed addresses and "calls" by setting lr to a
// point further ahead before jumping. The recompiled model (bx lr = return from the host
// function) does not hold there, so such code runs instruction by instruction until it
// returns to the address that was in lr on entry.
#include "runtime.h"

namespace ntr {

namespace {

bool cond_pass(const Cpu& c, uint32_t cond) {
    switch (cond) {
        case 0x0: return c.z;
        case 0x1: return !c.z;
        case 0x2: return c.c;
        case 0x3: return !c.c;
        case 0x4: return c.n;
        case 0x5: return !c.n;
        case 0x6: return c.v;
        case 0x7: return !c.v;
        case 0x8: return c.c && !c.z;
        case 0x9: return !c.c || c.z;
        case 0xA: return c.n == c.v;
        case 0xB: return c.n != c.v;
        case 0xC: return !c.z && c.n == c.v;
        case 0xD: return c.z || c.n != c.v;
        default: return true;
    }
}

// Barrel shifter for register operands. `carry` receives the shifter carry-out.
uint32_t shift_imm(uint32_t v, uint32_t type, uint32_t amount, uint32_t& carry) {
    switch (type) {
        case 0:  // LSL
            if (amount == 0) return v;
            carry = (v >> (32 - amount)) & 1;
            return v << amount;
        case 1:  // LSR (#0 means #32)
            if (amount == 0) { carry = v >> 31; return 0; }
            carry = (v >> (amount - 1)) & 1;
            return v >> amount;
        case 2:  // ASR (#0 means #32)
            if (amount == 0) { carry = v >> 31; return uint32_t(int32_t(v) >> 31); }
            carry = (v >> (amount - 1)) & 1;
            return uint32_t(int32_t(v) >> amount);
        default:  // ROR (#0 means RRX)
            if (amount == 0) {
                uint32_t out = (carry << 31) | (v >> 1);
                carry = v & 1;
                return out;
            }
            carry = (v >> (amount - 1)) & 1;
            return ror32(v, amount);
    }
}

uint32_t shift_reg(uint32_t v, uint32_t type, uint32_t s, uint32_t& carry) {
    s &= 0xFF;
    if (s == 0) return v;
    switch (type) {
        case 0:
            if (s < 32) { carry = (v >> (32 - s)) & 1; return v << s; }
            carry = s == 32 ? (v & 1) : 0;
            return 0;
        case 1:
            if (s < 32) { carry = (v >> (s - 1)) & 1; return v >> s; }
            carry = s == 32 ? (v >> 31) : 0;
            return 0;
        case 2:
            if (s < 32) { carry = (v >> (s - 1)) & 1; return uint32_t(int32_t(v) >> s); }
            carry = v >> 31;
            return uint32_t(int32_t(v) >> 31);
        default:
            s &= 31;
            if (s == 0) { carry = v >> 31; return v; }
            carry = (v >> (s - 1)) & 1;
            return ror32(v, s);
    }
}

struct Interp {
    Cpu& c;
    uint32_t pc = 0;     // address of the instruction being executed
    uint32_t next = 0;   // where execution continues
    bool exit = false;

    uint32_t reg(uint32_t n) const { return n == 15 ? pc + 8 : c.r[n]; }

    // Writes to pc branch; `interwork` lets bit 0 select Thumb (LDR/LDM/BX on ARMv5).
    void set_reg(uint32_t n, uint32_t v, bool interwork = false) {
        if (n != 15) {
            c.r[n] = v;
            return;
        }
        if (interwork && (v & 1)) unhandled(c, pc, "interpreter: switch to Thumb");
        next = v & ~3u;
    }

    void data_processing(uint32_t op) {
        uint32_t opcode = (op >> 21) & 0xF, s = (op >> 20) & 1;
        uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
        uint32_t carry = c.c;
        uint32_t b;
        uint32_t a;
        if (op & (1u << 25)) {
            uint32_t rot = ((op >> 8) & 0xF) * 2;
            b = ror32(op & 0xFF, rot);
            if (rot) carry = b >> 31;
            a = reg(rn);
        } else if (op & 0x10) {
            // Register-specified shift: pc reads as +12.
            uint32_t rm = op & 0xF;
            uint32_t vm = rm == 15 ? pc + 12 : c.r[rm];
            b = shift_reg(vm, (op >> 5) & 3, c.r[(op >> 8) & 0xF], carry);
            a = rn == 15 ? pc + 12 : c.r[rn];
        } else {
            b = shift_imm(reg(op & 0xF), (op >> 5) & 3, (op >> 7) & 0x1F, carry);
            a = reg(rn);
        }
        uint32_t res = 0;
        bool logical = false, write = true;
        switch (opcode) {
            case 0x0: res = a & b; logical = true; break;
            case 0x1: res = a ^ b; logical = true; break;
            case 0x2: res = s ? sub_flags(c, a, b) : a - b; break;
            case 0x3: res = s ? sub_flags(c, b, a) : b - a; break;
            case 0x4: res = s ? add_flags(c, a, b) : a + b; break;
            case 0x5: res = s ? add_flags(c, a, b, c.c) : a + b + c.c; break;
            case 0x6: res = s ? sub_flags(c, a, b, c.c) : a - b - (1 - c.c); break;
            case 0x7: res = s ? sub_flags(c, b, a, c.c) : b - a - (1 - c.c); break;
            case 0x8: res = a & b; logical = true; write = false; break;
            case 0x9: res = a ^ b; logical = true; write = false; break;
            case 0xA: sub_flags(c, a, b); write = false; break;
            case 0xB: add_flags(c, a, b); write = false; break;
            case 0xC: res = a | b; logical = true; break;
            case 0xD: res = b; logical = true; break;
            case 0xE: res = a & ~b; logical = true; break;
            case 0xF: res = ~b; logical = true; break;
        }
        if (s && logical) {
            set_nz(c, res);
            c.c = carry;
        }
        if (!write) return;
        if (rd == 15 && s) {
            // MOVS/SUBS pc: exception return.
            exception_return(c);
            if (c.thumb) unhandled(c, pc, "interpreter: exception return to Thumb");
            next = res & ~3u;
            return;
        }
        set_reg(rd, res);
    }

    void psr_transfer(uint32_t op) {
        bool spsr = op & (1u << 22);
        if (!(op & (1u << 21))) {  // MRS
            c.r[(op >> 12) & 0xF] = spsr ? spsr_read(c) : cpsr_read(c);
            return;
        }
        uint32_t v = (op & (1u << 25)) ? ror32(op & 0xFF, ((op >> 8) & 0xF) * 2) : c.r[op & 0xF];
        uint32_t mask = 0;
        for (int f = 0; f < 4; f++) {
            if (op & (1u << (16 + f))) mask |= 0xFFu << (f * 8);
        }
        if (spsr) spsr_write(c, v, mask);
        else cpsr_write(c, v, mask);
    }

    void multiply(uint32_t op) {
        uint32_t rd = (op >> 16) & 0xF, rn = (op >> 12) & 0xF, rs = (op >> 8) & 0xF, rm = op & 0xF;
        bool s = op & (1u << 20);
        switch ((op >> 21) & 0xF) {
            case 0x0:  // MUL
            case 0x1: {  // MLA
                uint32_t res = c.r[rm] * c.r[rs] + ((op >> 21) & 1 ? c.r[rn] : 0);
                c.r[rd] = res;
                if (s) set_nz(c, res);
                return;
            }
            case 0x4: case 0x5: case 0x6: case 0x7: {  // UMULL, UMLAL, SMULL, SMLAL
                bool sign = op & (1u << 22), acc = op & (1u << 21);
                uint64_t res = sign ? uint64_t(int64_t(int32_t(c.r[rm])) * int32_t(c.r[rs]))
                                    : uint64_t(c.r[rm]) * c.r[rs];
                if (acc) res += uint64_t(c.r[rd]) << 32 | c.r[rn];
                c.r[rn] = uint32_t(res);
                c.r[rd] = uint32_t(res >> 32);
                if (s) {
                    c.n = uint32_t(res >> 63);
                    c.z = res == 0;
                }
                return;
            }
        }
        unhandled(c, pc, "interpreter: multiply form");
    }

    // SMLAxy, SMLAWy, SMULWy, SMLALxy, SMULxy.
    void dsp_multiply(uint32_t op) {
        uint32_t rd = (op >> 16) & 0xF, rn = (op >> 12) & 0xF, rs = (op >> 8) & 0xF, rm = op & 0xF;
        bool x = op & 0x20, y = op & 0x40;
        auto half = [](uint32_t v, bool top) { return int32_t(int16_t(top ? v >> 16 : v)); };
        switch ((op >> 21) & 3) {
            case 0: {  // SMLAxy
                int32_t p = half(c.r[rm], x) * half(c.r[rs], y);
                c.r[rd] = qadd_flag(uint32_t(p), c.r[rn]);
                return;
            }
            case 1: {  // SMLAWy / SMULWy
                int64_t p = int64_t(int32_t(c.r[rm])) * half(c.r[rs], y);
                uint32_t res = uint32_t(p >> 16);
                c.r[rd] = x ? res : qadd_flag(res, c.r[rn]);
                return;
            }
            case 2: {  // SMLALxy
                int64_t p = int64_t(half(c.r[rm], x)) * half(c.r[rs], y);
                uint64_t acc = (uint64_t(c.r[rd]) << 32 | c.r[rn]) + uint64_t(p);
                c.r[rn] = uint32_t(acc);
                c.r[rd] = uint32_t(acc >> 32);
                return;
            }
            default:  // SMULxy
                c.r[rd] = uint32_t(half(c.r[rm], x) * half(c.r[rs], y));
                return;
        }
    }

    // Addition that sets Q on signed overflow without saturating (SMLA*).
    uint32_t qadd_flag(uint32_t a, uint32_t b) {
        uint32_t res = a + b;
        if ((~(a ^ b) & (a ^ res)) >> 31) c.q = 1;
        return res;
    }

    void saturating(uint32_t op) {
        uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF, rm = op & 0xF;
        uint32_t b = c.r[rn];
        if (op & (1u << 22)) b = qadd(c, b, b);  // QDADD/QDSUB double rn first
        c.r[rd] = (op & (1u << 21)) ? qsub(c, c.r[rm], b) : qadd(c, c.r[rm], b);
    }

    void single_transfer(uint32_t op) {
        uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
        bool p = op & (1u << 24), u = op & (1u << 23), byte = op & (1u << 22), w = op & (1u << 21),
             load = op & (1u << 20);
        uint32_t off;
        if (op & (1u << 25)) {
            uint32_t carry = c.c;
            off = shift_imm(c.r[op & 0xF], (op >> 5) & 3, (op >> 7) & 0x1F, carry);
        } else {
            off = op & 0xFFF;
        }
        uint32_t base = reg(rn);
        uint32_t addr = p ? (u ? base + off : base - off) : base;
        uint32_t wb = u ? base + off : base - off;
        if (load) {
            uint32_t v = byte ? rd8(addr) : ldr32(addr);
            if (!p || w) c.r[rn] = wb;
            set_reg(rd, v, true);
        } else {
            uint32_t v = rd == 15 ? pc + 12 : c.r[rd];
            if (byte) wr8(addr, uint8_t(v));
            else wr32(addr, v);
            if (!p || w) c.r[rn] = wb;
        }
    }

    // LDRH/STRH/LDRSB/LDRSH/LDRD/STRD.
    void halfword_transfer(uint32_t op) {
        uint32_t rn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF;
        bool p = op & (1u << 24), u = op & (1u << 23), w = op & (1u << 21), load = op & (1u << 20);
        uint32_t off = (op & (1u << 22)) ? ((op >> 4) & 0xF0) | (op & 0xF) : c.r[op & 0xF];
        uint32_t base = reg(rn);
        uint32_t addr = p ? (u ? base + off : base - off) : base;
        uint32_t wb = u ? base + off : base - off;
        uint32_t sh = (op >> 5) & 3;
        if (load) {
            uint32_t v = sh == 1 ? rd16(addr) : sh == 2 ? uint32_t(int32_t(int8_t(rd8(addr))))
                                                         : uint32_t(int32_t(int16_t(rd16(addr))));
            if (!p || w) c.r[rn] = wb;
            set_reg(rd, v);
            return;
        }
        if (sh == 1) {
            wr16(addr, uint16_t(c.r[rd]));
        } else if (sh == 2) {  // LDRD
            uint32_t lo = rd32(addr), hi = rd32(addr + 4);
            if (!p || w) c.r[rn] = wb;
            c.r[rd] = lo;
            c.r[rd + 1] = hi;
            return;
        } else {  // STRD
            wr32(addr, c.r[rd]);
            wr32(addr + 4, c.r[rd + 1]);
        }
        if (!p || w) c.r[rn] = wb;
    }

    void block_transfer(uint32_t op) {
        uint32_t rn = (op >> 16) & 0xF, list = op & 0xFFFF;
        bool p = op & (1u << 24), u = op & (1u << 23), s = op & (1u << 22), w = op & (1u << 21),
             load = op & (1u << 20);
        if (s && !(load && (list & 0x8000))) unhandled(c, pc, "interpreter: user-bank LDM/STM");
        uint32_t count = __builtin_popcount(list);
        if (count == 0) unhandled(c, pc, "interpreter: empty register list");
        uint32_t base = c.r[rn];
        uint32_t start = u ? base + (p ? 4 : 0) : base - count * 4 + (p ? 0 : 4);
        uint32_t end_base = u ? base + count * 4 : base - count * 4;
        uint32_t addr = start;
        if (load) {
            uint32_t pc_value = 0;
            for (uint32_t i = 0; i < 16; i++) {
                if (!(list & (1u << i))) continue;
                uint32_t v = rd32(addr);
                addr += 4;
                if (i == 15) pc_value = v;
                else c.r[i] = v;
            }
            // ARMv5: writeback unless rn is in the list (then the loaded value wins).
            if (w && !(list & (1u << rn))) c.r[rn] = end_base;
            if (list & 0x8000) {
                if (s) {
                    exception_return(c);
                    if (c.thumb) unhandled(c, pc, "interpreter: exception return to Thumb");
                    next = pc_value & ~3u;
                } else {
                    set_reg(15, pc_value, true);
                }
            }
        } else {
            for (uint32_t i = 0; i < 16; i++) {
                if (!(list & (1u << i))) continue;
                wr32(addr, i == 15 ? pc + 12 : c.r[i]);
                addr += 4;
            }
            if (w) c.r[rn] = end_base;
        }
    }

    void coprocessor(uint32_t op) {
        if ((op & 0x0F000010) != 0x0E000010) unhandled(c, pc, "interpreter: coprocessor data/transfer");
        uint32_t cp = (op >> 8) & 0xF, op1 = (op >> 21) & 7, crn = (op >> 16) & 0xF, rd = (op >> 12) & 0xF,
                 crm = op & 0xF, op2 = (op >> 5) & 7;
        if (op & (1u << 20)) {
            uint32_t v = mrc(c, cp, op1, crn, crm, op2);
            if (rd == 15) {
                c.n = v >> 31;
                c.z = (v >> 30) & 1;
                c.c = (v >> 29) & 1;
                c.v = (v >> 28) & 1;
            } else {
                c.r[rd] = v;
            }
        } else {
            mcr(c, cp, op1, reg(rd), crn, crm, op2);
        }
    }

    void step(uint32_t op) {
        uint32_t cond = op >> 28;
        if (cond == 0xF) {
            if ((op & 0x0E000000) == 0x0A000000) {  // BLX imm
                c.r[14] = pc + 4;
                unhandled(c, pc, "interpreter: BLX to Thumb");
            }
            if ((op & 0x0D70F000) == 0x0550F000) return;  // PLD
            unhandled(c, pc, "interpreter: unconditional instruction");
        }
        if (!cond_pass(c, cond)) return;
        switch ((op >> 25) & 7) {
            case 0:
                if ((op & 0x0FFFFFD0) == 0x012FFF10) {  // BX / BLX reg
                    uint32_t target = c.r[op & 0xF];
                    if (op & 0x20) c.r[14] = pc + 4;
                    set_reg(15, target, true);
                    return;
                }
                if ((op & 0x0FFF0FF0) == 0x016F0F10) {  // CLZ
                    c.r[(op >> 12) & 0xF] = clz32(c.r[op & 0xF]);
                    return;
                }
                if ((op & 0x0F9000F0) == 0x01000050) return saturating(op);
                if ((op & 0x0F900090) == 0x01000080) return dsp_multiply(op);
                if ((op & 0x0FC000F0) == 0x00000090 || (op & 0x0F8000F0) == 0x00800090) return multiply(op);
                if ((op & 0x0FB00FF0) == 0x01000090) {  // SWP/SWPB
                    uint32_t addr = c.r[(op >> 16) & 0xF], rm = c.r[op & 0xF];
                    uint32_t v;
                    if (op & (1u << 22)) { v = rd8(addr); wr8(addr, uint8_t(rm)); }
                    else { v = ldr32(addr); wr32(addr, rm); }
                    c.r[(op >> 12) & 0xF] = v;
                    return;
                }
                if ((op & 0x90) == 0x90) return halfword_transfer(op);
                if ((op & 0x0F900000) == 0x01000000) return psr_transfer(op);
                return data_processing(op);
            case 1:
                if ((op & 0x0FB00000) == 0x03200000) return psr_transfer(op);
                return data_processing(op);
            case 2:
                return single_transfer(op);
            case 3:
                if (op & 0x10) unhandled(c, pc, "interpreter: undefined instruction");
                return single_transfer(op);
            case 4:
                return block_transfer(op);
            case 5: {
                int32_t off = int32_t(op << 8) >> 6;
                if (op & (1u << 24)) c.r[14] = pc + 4;
                next = pc + 8 + uint32_t(off);
                return;
            }
            case 6:
                unhandled(c, pc, "interpreter: coprocessor transfer");
            default:
                if (op & (1u << 24)) return swi(c, (op >> 16) & 0xFF);
                return coprocessor(op);
        }
    }
};

}  // namespace

void interpret_arm(Cpu& c, uint32_t entry) {
    if (entry & 1) unhandled(c, entry, "interpreter: Thumb entry");
    uint32_t ret = c.r[14];
    Interp it{c};
    it.pc = entry & ~3u;
    while (it.pc != (ret & ~1u)) {
        it.next = it.pc + 4;
        it.step(rd32(it.pc));
        it.pc = it.next;
    }
}

}  // namespace ntr
