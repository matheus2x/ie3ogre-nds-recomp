// Entry points for the host program.
#pragma once

#include <string>

namespace ntr {

// Opens the cartridge image the game data is read from: the game data package made by
// tools/extract.py (boej.pak) or a .nds dump. It stays memory-mapped while the game runs.
bool rom_load(const std::string& path);

// Directory of the running executable, with a trailing separator.
std::string host_base_path();

// Opens the window (unless NTR_HEADLESS is set). Must run on the main thread before boot().
bool host_init();

// Boots the cartridge like the DS firmware and runs the game. Does not return.
[[noreturn]] void boot();

}  // namespace ntr
