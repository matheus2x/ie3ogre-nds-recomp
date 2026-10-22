// Entry points for the host program.
#pragma once

#include <string>

// printf-style checking; MinGW's default "printf" archetype is the old MSVCRT one.
#ifndef NTR_PRINTF_FORMAT
#ifdef __MINGW32__
#define NTR_PRINTF_FORMAT __attribute__((format(gnu_printf, 1, 2)))
#else
#define NTR_PRINTF_FORMAT __attribute__((format(printf, 1, 2)))
#endif
#endif

namespace ntr {

// Opens the cartridge image the game data is read from: the game data package made by
// tools/extract.py (boej.pak) or a .nds dump. It stays memory-mapped while the game runs.
bool rom_load(const std::string& path);

// Prints to the terminal and to the log file (boej.log next to the executable).
void log_printf(const char* fmt, ...) NTR_PRINTF_FORMAT;

// Directory of the running executable, with a trailing separator.
std::string host_base_path();

// Opens the window (unless NTR_HEADLESS is set). Must run on the main thread before boot().
bool host_init();

// Boots the cartridge like the DS firmware and runs the game. Does not return.
[[noreturn]] void boot();

}  // namespace ntr
