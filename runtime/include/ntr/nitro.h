// Reading a cartridge image: a .nds dump or the game data package (boej.pak), by ROM
// offset, plus the parts of it the mods work with: the NitroFS file table, the ARM9
// overlay table and BLZ, the compression of the overlays. Shared by the game (rom.cpp,
// mods.cpp) and the launcher (mods made from a modified dump).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ntr {

class RomImage {
public:
    RomImage() = default;
    RomImage(const RomImage&) = delete;
    RomImage& operator=(const RomImage&) = delete;
    RomImage(RomImage&& other) noexcept { *this = std::move(other); }
    RomImage& operator=(RomImage&& other) noexcept;
    ~RomImage();

    // Memory-maps the file. False (and a log line) if it is neither a package nor a dump.
    bool open(const std::string& path);
    bool is_open() const { return base_ != nullptr; }
    bool is_pak() const { return pak_; }
    uint64_t size() const { return rom_size_; }  // of the cartridge image, not of the file

    // Bytes outside the image (the padding a package leaves out) read as 0xFF.
    void read(uint32_t offset, void* dst, uint32_t size) const;
    uint32_t read32(uint32_t offset) const;
    std::vector<uint8_t> bytes(uint32_t offset, uint32_t size) const;

private:
    struct Range {
        uint64_t rom_offset, size, file_offset;
    };
    bool parse_pak();

    const uint8_t* base_ = nullptr;
    uint64_t file_size_ = 0, rom_size_ = 0;
    bool pak_ = false;
    std::vector<Range> ranges_;  // sorted by rom_offset
};

// --- NitroFS --------------------------------------------------------------------------

struct NitroFile {
    std::string path;  // "data_iz/logic/xxx.dat"
    uint16_t id;
    uint32_t start, end;  // ROM offsets (FAT entry)
};
// Every named file, in FNT order. Empty if the tables are damaged.
std::vector<NitroFile> nitro_files(const RomImage& rom);

// --- ARM9 overlays ----------------------------------------------------------------------

struct NitroOverlay {
    uint32_t id, ram_address, ram_size, bss_size, sinit_start, sinit_end, file_id;
    uint32_t packed;  // bits 0-23 compressed size, bit 24 compressed
    bool compressed() const { return packed & 0x01000000; }
};
constexpr uint32_t kOverlayEntrySize = 32;
std::vector<NitroOverlay> nitro_overlays(const RomImage& rom);
// The overlay as the game has it in RAM after loading (decompressed); empty on error.
std::vector<uint8_t> nitro_overlay_image(const RomImage& rom, const NitroOverlay& ov);

// BLZ (backwards LZ) in place; false if the data is not valid BLZ.
bool blz_decompress(std::vector<uint8_t>& data);

}  // namespace ntr
