// boej: runs the recompiled Inazuma Eleven 3 (BOEJ).
//
// Usage: boej [boej.pak | rom.nds]
//
// The game data comes from boej.pak, which tools/extract.py makes from the user's dump;
// without an argument it is looked up next to the executable, then in the current
// directory. A .nds dump can still be passed directly.
#include <cstdio>
#include <string>

#include <ntr/runtime.h>

int main(int argc, char** argv) {
    if (argc > 2) {
        std::fprintf(stderr, "usage: %s [boej.pak | rom.nds]\n", argv[0]);
        return 2;
    }
    std::string path;
    if (argc == 2) {
        path = argv[1];
        if (!ntr::rom_load(path)) {
            ntr::log_printf("error: cannot read %s\n", path.c_str());
            return 1;
        }
    } else {
        for (const std::string& candidate : {ntr::host_base_path() + "boej.pak", std::string("boej.pak")}) {
            if (ntr::rom_load(candidate)) {
                path = candidate;
                break;
            }
        }
        if (path.empty()) {
            ntr::log_printf(
                         "error: boej.pak not found next to the executable.\n"
                         "Create it once from your dump with: python3 tools/extract.py <rom.nds>\n");
            return 1;
        }
    }
    if (!ntr::host_init()) {
        ntr::log_printf("error: cannot open the window (set NTR_HEADLESS=1 to run without one)\n");
        return 1;
    }
    ntr::boot();
}
