// A mod as the player leaves it in mods/: a folder or a .zip (docs/MODS.md). Both look the
// same from here: the files under the mod's root, with '/' separators. The root is the
// folder itself; in a zip, the top of the zip or its only folder, whichever has mod.ini.
// Shared by the game (runtime/src/mods.cpp) and the launcher (the Mods tab).
//
// Zips are read in place, without extracting: stored entries straight from the mapped file,
// deflated ones inflated when read (and checked against their CRC-32).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ntr {

class ModPackage {
public:
    struct Entry {
        std::string path;  // relative to the root, e.g. "files/data_iz/font/FONT8.NFTR"
        uint64_t size = 0;
        uint64_t stamp = 0;  // changes when the file changes: CRC-32 in a zip, mtime in a folder
        // where the bytes are
        std::string file;  // folder: the file on disk
        uint64_t offset = 0, packed = 0;
        uint16_t method = 0;  // zip: 0 stored, 8 deflated
        uint32_t crc = 0;
    };

    ModPackage() = default;
    ModPackage(const ModPackage&) = delete;
    ModPackage& operator=(const ModPackage&) = delete;
    ~ModPackage();

    // `path`: a folder or a .zip. False with a message for the player.
    bool open(const std::string& path, std::string& error);
    bool is_zip() const { return zip_ != nullptr; }
    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(const std::string& path) const;  // case-insensitive

    // The whole file. False with a message (damaged zip, unsupported compression...).
    bool read(const Entry& e, std::vector<uint8_t>& out, std::string& error) const;
    // The bytes in place when they need no inflating (a folder's file, mapped, or a stored
    // zip entry); nullptr otherwise. They stay valid while the package lives.
    const uint8_t* direct(const Entry& e) const;

private:
    bool open_zip(const std::string& path, std::string& error);

    const uint8_t* zip_ = nullptr;
    uint64_t zip_size_ = 0;
    std::vector<Entry> entries_;
    mutable std::vector<std::pair<const uint8_t*, uint64_t>> mapped_;
};

// BPS patches (files/<path>.bps in a mod; format in tools/ntr/bps.py): the sizes in the
// header, and the patch applied over `source` into `out`, with the CRC-32 of the source, of
// the result and of the patch checked. False with a message for the player.
bool bps_sizes(const std::vector<uint8_t>& patch, uint64_t& source_size, uint64_t& target_size);
bool bps_apply(const std::vector<uint8_t>& patch, const uint8_t* source, size_t source_size,
               std::vector<uint8_t>& out, std::string& error);

// Raw DEFLATE (RFC 1951) into `out` (appended). False if the stream is damaged.
bool inflate(const uint8_t* data, size_t size, std::vector<uint8_t>& out);
uint32_t crc32(const uint8_t* data, size_t size);

}  // namespace ntr
