// Entry points for the host program.
#pragma once

#include <string>
#include <vector>

#include <ntr/common.h>

namespace ntr {

// Opens the cartridge image the game data is read from: the game data package made by
// tools/extract.py (boej.pak) or a .nds dump. It stays memory-mapped while the game runs.
bool rom_load(const std::string& path);

// Lays the mods turned on in boej.ini over the image (after rom_load, before boot).
void mods_apply();

// boej --check-mods: opens the package and prints what each folder in mods/ would do
// (format in mods.cpp), for the launcher. Returns the exit code.
int mods_check(const std::string& pak_path);

// log_printf (ntr/common.h) prints to the terminal and to boej.log next to the executable.

// Directory of the running executable, with a trailing separator.
std::string host_base_path();

// The command line, for "Reiniciar o jogo" in the pause menu (UTF-8, argv[0] first).
void host_set_command_line(const std::vector<std::string>& args);

// Opens the window (unless NTR_HEADLESS is set). Must run on the main thread before boot().
bool host_init();

// With a window: makes the game data package at `pak_path`, asking for the dump with a
// file dialog when `nds_path` is empty and showing the progress. False if there is no
// window, the player gives up, or the dump given in `nds_path` cannot be used.
bool host_first_run(const std::string& nds_path, const std::string& pak_path);

// Boots the cartridge like the DS firmware and runs the game. Does not return.
[[noreturn]] void boot();

}  // namespace ntr
