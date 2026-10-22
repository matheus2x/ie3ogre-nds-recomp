// Program model loaded from build/recomp/manifest.tsv (see "tools/analyze.py manifest").
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace recomp {

struct Section {
    uint32_t start;
    uint32_t end;
    std::string kind;  // code, rodata, data, bss
};

struct Function {
    uint32_t addr;
    uint32_t size;
    bool thumb;
    std::string name;
    std::string hle;          // non-empty: body replaced by a call to hle_<hle>
    bool alt_entry = false;   // entry in the middle of another function
    uint32_t body_end = 0;    // end of the enclosing function body
};

struct Module {
    std::string name;
    uint32_t base = 0;
    std::vector<uint8_t> data;
    std::vector<Section> sections;
    std::vector<Function> functions;  // sorted by address, includes alternate entries

    bool is_overlay() const { return name.rfind("ov", 0) == 0; }
    bool contains(uint32_t addr) const { return addr >= base && addr - base < data.size(); }
    std::optional<uint32_t> read32(uint32_t addr) const;
    std::optional<uint16_t> read16(uint32_t addr) const;
    const Function* function_at(uint32_t addr) const;
};

struct Program {
    std::vector<Module> modules;

    // Resolves a direct branch/call target from code in `from`. Overlay code may call
    // its own module and the static modules; anything else needs a runtime lookup.
    const Function* resolve_call(const Module& from, uint32_t target) const;
    const Module* module(const std::string& name) const;
};

// Loads the manifest and module binaries. HLE entries come from "hle" lines.
Program load_program(const std::string& manifest_path);

// Adds an alternate entry (a call target found inside a function, e.g. in a code blob).
// Returns false when no function encloses the address.
bool add_discovered_entry(Module& m, uint32_t addr, bool thumb);

std::string symbol_for(const Module& m, const Function& f);

}  // namespace recomp
