// Entry points for the host program.
#pragma once

#include <string>

#include <ntr/common.h>

namespace ntr {

// Opens the cartridge image the game data is read from: the game data package made by
// tools/extract.py (boej.pak) or a .nds dump. It stays memory-mapped while the game runs.
bool rom_load(const std::string& path);

// log_printf (ntr/common.h) prints to the terminal and to boej.log next to the executable.

// Directory of the running executable, with a trailing separator.
std::string host_base_path();

// Opens the window (unless NTR_HEADLESS is set). Must run on the main thread before boot().
bool host_init();

// With a window: makes the game data package at `pak_path`, asking for the dump with a
// file dialog when `nds_path` is empty and showing the progress. False if there is no
// window, the player gives up, or the dump given in `nds_path` cannot be used.
bool host_first_run(const std::string& nds_path, const std::string& pak_path);

// Boots the cartridge like the DS firmware and runs the game. Does not return.
[[noreturn]] void boot();

}  // namespace ntr
