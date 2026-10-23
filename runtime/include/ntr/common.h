// Code shared by the game runtime and the launcher (the ntr-common library): UTF-8 file
// helpers, the settings file, hashes, dump identification and the game data package.
#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
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

// Provided by the program: the game prints to the terminal and boej.log (diag.cpp), the
// launcher to its own log.
void log_printf(const char* fmt, ...) NTR_PRINTF_FORMAT;
#define NTR_LOG(...) ::ntr::log_printf(__VA_ARGS__)

// Raw little-endian accessors into host buffers.
inline uint32_t load32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
inline void store32(uint8_t* p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24); }

// --- files (files.cpp) ----------------------------------------------------------------
// Paths are UTF-8, like SDL's and the command line's (std::string paths would be read in
// the ANSI code page on Windows).

const uint8_t* map_file(const std::string& path, uint64_t& size);  // read-only; nullptr on failure
void unmap_file(const uint8_t* data, uint64_t size);
std::filesystem::path fs_path(const std::string& utf8);
std::string utf8_string(const std::filesystem::path& path);
FILE* open_file(const std::filesystem::path& path, const char* mode);

// --- settings (config.cpp) ------------------------------------------------------------

// boej.ini, over the built-in defaults; keys are case-insensitive.
struct Config {
    struct Entry {
        std::string value;
        int line = 0;  // in the user's file; 0 = built-in default
    };
    std::string path;
    std::map<std::string, Entry> entries;  // "section.key"

    const Entry* find(const std::string& section, const std::string& key) const;
    std::string get(const std::string& section, const std::string& key) const;
    // Invalid values are logged and replaced by the default.
    int get_int(const std::string& section, const std::string& key, int min, int max) const;
    bool get_bool(const std::string& section, const std::string& key) const;
    void set(const std::string& section, const std::string& key, const std::string& value);
    void warn(const Entry& e, const std::string& message) const;  // "[config] file:line: ..."
};
bool config_load(const std::string& path, Config& out);  // false (defaults only) if the file is missing
bool config_write_default(const std::string& path);
// Writes the defaults file with this config's values in place of the defaults (comments
// and order kept).
bool config_save(const std::string& path, const Config& config);

// --- hashes (hash.cpp) ----------------------------------------------------------------

class Sha1 {
public:
    void update(const uint8_t* p, size_t n);
    std::string hex();  // finishes the hash

private:
    void compress(const uint8_t* p);
    uint32_t h_[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint8_t block_[64];
    size_t used_ = 0;
    uint64_t total_ = 0;
};

class Sha256 {
public:
    void update(const uint8_t* p, size_t n);
    std::string hex();  // finishes the hash

private:
    void compress(const uint8_t* p);
    uint32_t h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t block_[64];
    size_t used_ = 0;
    uint64_t total_ = 0;
};

// SHA-256 of a whole file; "" if it cannot be read.
std::string sha256_file(const std::string& path);

// --- dumps and the game data package (setup.cpp) --------------------------------------

struct KnownDump {
    const char* sha1;
    const char* variant;
    bool supported;  // the code is recompiled from this dump
};
const KnownDump* find_dump(const std::string& sha1);
const KnownDump& supported_dump();
// SHA-1 of the file, with progress (0..1); "" if it cannot be read.
std::string sha1_file(const std::string& path, const std::function<void(double)>& progress);
// Player-facing message for a dump that cannot be used ("" when it can).
std::string dump_problem(const std::string& sha1);

// Checks a .nds dump by SHA-1 and, for the supported (retail) dump, writes the game data
// package from it. `progress` gets 0..1; on failure `error` is a message for the player.
bool pak_create(const std::string& nds_path, const std::string& pak_path, std::string& error,
                const std::function<void(double)>& progress);

}  // namespace ntr
