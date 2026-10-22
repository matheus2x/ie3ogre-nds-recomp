#include "program.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace recomp {

namespace {

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, '\t')) {
        out.push_back(field);
    }
    return out;
}

uint32_t parse_u32(const std::string& s) {
    return static_cast<uint32_t>(std::stoul(s, nullptr, 0));
}

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + path);
    }
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}  // namespace

std::optional<uint32_t> Module::read32(uint32_t addr) const {
    if (!contains(addr) || !contains(addr + 3)) {
        return std::nullopt;
    }
    const uint8_t* p = data.data() + (addr - base);
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

std::optional<uint16_t> Module::read16(uint32_t addr) const {
    if (!contains(addr) || !contains(addr + 1)) {
        return std::nullopt;
    }
    const uint8_t* p = data.data() + (addr - base);
    return uint16_t(p[0] | p[1] << 8);
}

const Function* Module::function_at(uint32_t addr) const {
    auto it = std::lower_bound(functions.begin(), functions.end(), addr,
                               [](const Function& f, uint32_t a) { return f.addr < a; });
    return it != functions.end() && it->addr == addr ? &*it : nullptr;
}

const Module* Program::module(const std::string& name) const {
    for (const auto& m : modules) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}

const Function* Program::resolve_call(const Module& from, uint32_t target) const {
    if (from.contains(target)) {
        return from.function_at(target);
    }
    for (const auto& m : modules) {
        if (!m.is_overlay() && m.contains(target)) {
            return m.function_at(target);
        }
    }
    return nullptr;
}

std::string symbol_for(const Module& m, const Function& f) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "fn_%s_%08x", m.name.c_str(), f.addr);
    return buf;
}

Program load_program(const std::string& manifest_path) {
    std::ifstream in(manifest_path);
    if (!in) {
        throw std::runtime_error("cannot open " + manifest_path);
    }
    Program prog;
    std::map<std::string, std::string> hle;  // function name -> hle id
    std::string line;
    while (std::getline(in, line)) {
        auto f = split_tabs(line);
        if (f.empty()) {
            continue;
        }
        if (f[0] == "module") {
            Module m;
            m.name = f[1];
            m.base = parse_u32(f[2]);
            m.data = read_file(f[3]);
            prog.modules.push_back(std::move(m));
        } else if (f[0] == "section") {
            prog.modules.back().sections.push_back({parse_u32(f[1]), parse_u32(f[2]), f[3]});
        } else if (f[0] == "func") {
            Function fn;
            fn.addr = parse_u32(f[1]);
            fn.size = parse_u32(f[2]);
            fn.thumb = f[3] == "thumb";
            fn.name = f[4];
            fn.body_end = fn.addr + fn.size;
            prog.modules.back().functions.push_back(fn);
        } else if (f[0] == "entry") {
            Function fn;
            fn.addr = parse_u32(f[1]);
            fn.size = 0;
            fn.thumb = f[2] == "thumb";
            fn.name = f[3];
            fn.alt_entry = true;
            prog.modules.back().functions.push_back(fn);
        } else if (f[0] == "hle") {
            hle[f[1]] = f[2];
        } else {
            throw std::runtime_error("bad manifest line: " + line);
        }
    }

    for (auto& m : prog.modules) {
        auto& fns = m.functions;
        std::sort(fns.begin(), fns.end(), [](const Function& a, const Function& b) { return a.addr < b.addr; });
        // An alternate entry runs to the end of the function that contains it.
        uint32_t enclosing_end = 0;
        for (auto& fn : fns) {
            if (!fn.alt_entry) {
                enclosing_end = fn.body_end;
            } else {
                fn.body_end = enclosing_end;
                fn.size = enclosing_end > fn.addr ? enclosing_end - fn.addr : 0;
            }
            if (auto it = hle.find(fn.name); it != hle.end()) {
                fn.hle = it->second;
            }
        }
    }
    return prog;
}

}  // namespace recomp
