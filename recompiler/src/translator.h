// Translates one guest function (ARM or Thumb) into a C++ function.
#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "program.h"

namespace recomp {

struct Stats {
    uint64_t functions = 0;
    uint64_t instructions = 0;
    uint64_t unhandled = 0;
    std::map<std::string, uint64_t> unhandled_kinds;
    std::set<std::string> hle_ids;
    // Direct call/branch targets in the caller's module with no function entry:
    // (module, address | thumb bit). main.cpp turns those inside a function into entries.
    std::set<std::pair<std::string, uint32_t>> missing_entries;
    // Code addresses formed with "add/sub rD, pc, #imm" (ARM), same encoding as above.
    std::set<std::pair<std::string, uint32_t>> pc_relative_refs;
};

class Translator {
public:
    Translator(const Program& prog, const Module& mod, const Function& fn, Stats& stats);
    std::string translate();

private:
    // How control leaves an instruction.
    struct Flow {
        bool falls_through = true;
        std::vector<uint32_t> local_targets;  // branch/switch targets inside the function
    };

    // --- analysis ---
    void explore();
    Flow arm_flow(uint32_t addr, uint32_t raw);
    Flow thumb_flow(uint32_t addr, uint16_t raw, uint16_t next);
    bool is_local(uint32_t target) const { return target >= fn_.addr && target < fn_.body_end; }
    // "add lr, pc, #k" or "mov lr, pc" right before a "bx rM"/"mov pc, rM" that returns
    // to the next instruction: a call written by hand (assembly code blobs).
    bool is_manual_call(uint32_t addr) const;
    int switch_case_count(uint32_t addr, uint32_t reg) const;

    // --- emission ---
    std::string arm(uint32_t addr, uint32_t raw);
    std::string thumb(uint32_t addr, uint16_t raw, uint16_t next);
    std::string arm_data_processing(uint32_t addr, uint32_t raw);
    std::string arm_operand2(uint32_t addr, uint32_t raw, bool want_carry, std::string& carry);
    std::string arm_load_store(uint32_t addr, uint32_t raw);
    std::string arm_halfword(uint32_t addr, uint32_t raw);
    std::string arm_block(uint32_t addr, uint32_t raw);
    std::string arm_multiply(uint32_t raw);
    std::string arm_dsp(uint32_t raw);

    std::string branch_to(uint32_t target, bool thumb);         // B: local goto or tail call
    std::string call_to(uint32_t target, bool thumb, uint32_t ret);  // BL/BLX
    std::string jump_indirect(const std::string& target_expr);  // computed PC write
    std::string reg(uint32_t n, uint32_t pc_value) const;
    std::string unhandled(uint32_t addr, const std::string& what);

    const Program& prog_;
    const Module& mod_;
    const Function& fn_;
    Stats& stats_;
    std::map<uint32_t, uint32_t> insns_;               // reachable address -> length
    std::set<uint32_t> labels_;
    std::map<uint32_t, std::vector<uint32_t>> switches_;  // switch insn -> case targets
    uint32_t cur_addr_ = 0;                             // instruction being emitted
};

std::string hex32(uint32_t v);

}  // namespace recomp
