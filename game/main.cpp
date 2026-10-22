// boej: runs the recompiled Inazuma Eleven 3 (BOEJ) against the user's ROM.
//
// Usage: boej <rom.nds>
#include <cstdio>

#include <ntr/runtime.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <rom.nds>\n", argv[0]);
        return 2;
    }
    if (!ntr::rom_load(argv[1])) {
        std::fprintf(stderr, "error: cannot read %s\n", argv[1]);
        return 1;
    }
    ntr::boot();
}
