// Function tables emitted by ntr-recomp (registry.cpp), used for indirect calls.
#pragma once

#include <cstdint>

#include "guest.h"

namespace ntr {

struct FuncEntry {
    uint32_t addr;
    uint32_t thumb;
    void (*fn)(Cpu&);
};

struct ModuleEntry {
    const char* name;
    uint32_t base;
    uint32_t size;
    const FuncEntry* funcs;  // sorted by address
    uint32_t count;
};

extern const ModuleEntry kRecompModules[];
extern const uint32_t kRecompModuleCount;

}  // namespace ntr
