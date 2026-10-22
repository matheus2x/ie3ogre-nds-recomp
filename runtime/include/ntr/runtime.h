// Entry points for the host program.
#pragma once

#include <string>

namespace ntr {

// Reads the whole ROM image; the game data and code are taken from it at run time.
bool rom_load(const std::string& path);

// Boots the cartridge like the DS firmware and runs the game. Does not return.
[[noreturn]] void boot();

}  // namespace ntr
