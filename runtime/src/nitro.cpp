// Cartridge images and their tables (see ntr/nitro.h). The package format is described in
// tools/ntr/pak.py, BLZ in tools/ntr/blz.py.
#include <algorithm>
#include <cstring>

#include <ntr/common.h>
#include <ntr/nitro.h>

namespace ntr {

namespace {

uint64_t load64(const uint8_t* p) { return load32(p) | uint64_t(load32(p + 4)) << 32; }

constexpr char kPakMagic[8] = {'N', 'T', 'R', 'P', 'A', 'K', 0, 0};
constexpr uint32_t kPakVersion = 1;
constexpr uint32_t kPakHeaderSize = 0x38, kPakRangeSize = 24;

}  // namespace

RomImage& RomImage::operator=(RomImage&& other) noexcept {
    if (this != &other) {
        if (base_) unmap_file(base_, file_size_);
        base_ = other.base_;
        file_size_ = other.file_size_;
        rom_size_ = other.rom_size_;
        pak_ = other.pak_;
        ranges_ = std::move(other.ranges_);
        other.base_ = nullptr;
    }
    return *this;
}

RomImage::~RomImage() {
    if (base_) unmap_file(base_, file_size_);
}

bool RomImage::parse_pak() {
    const uint8_t* p = base_;
    if (load32(p + 8) != kPakVersion) {
        NTR_LOG("[rom] package version %u is not supported; run tools/extract.py again\n", load32(p + 8));
        return false;
    }
    rom_size_ = load64(p + 0x28);
    uint32_t count = load32(p + 0x30);
    if (kPakHeaderSize + uint64_t(count) * kPakRangeSize > file_size_) return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* r = p + kPakHeaderSize + i * kPakRangeSize;
        Range range{load64(r), load64(r + 8), load64(r + 16)};
        if (range.file_offset + range.size > file_size_) return false;
        ranges_.push_back(range);
    }
    std::sort(ranges_.begin(), ranges_.end(), [](const Range& a, const Range& b) { return a.rom_offset < b.rom_offset; });
    char sha1[41];
    for (int i = 0; i < 20; i++) std::snprintf(sha1 + 2 * i, 3, "%02x", p[0x10 + i]);
    NTR_LOG("[rom] game data package, %u ranges, made from dump %s\n", count, sha1);
    return true;
}

bool RomImage::open(const std::string& path) {
    *this = RomImage();
    base_ = map_file(path, file_size_);
    if (!base_) return false;
    if (file_size_ >= kPakHeaderSize && std::memcmp(base_, kPakMagic, 8) == 0) {
        pak_ = true;
        if (parse_pak()) return true;
    } else if (file_size_ >= 0x200) {
        rom_size_ = file_size_;
        ranges_.push_back({0, file_size_, 0});
        return true;
    }
    *this = RomImage();
    return false;
}

void RomImage::read(uint32_t offset, void* dst, uint32_t size) const {
    uint8_t* out = static_cast<uint8_t*>(dst);
    std::memset(out, 0xFF, size);
    // First range that ends after `offset`.
    auto it = std::upper_bound(ranges_.begin(), ranges_.end(), uint64_t(offset),
                               [](uint64_t off, const Range& r) { return off < r.rom_offset + r.size; });
    uint64_t end = uint64_t(offset) + size;
    for (; it != ranges_.end() && it->rom_offset < end; ++it) {
        uint64_t lo = std::max<uint64_t>(offset, it->rom_offset);
        uint64_t hi = std::min(end, it->rom_offset + it->size);
        if (lo < hi) std::memcpy(out + (lo - offset), base_ + it->file_offset + (lo - it->rom_offset), hi - lo);
    }
}

uint32_t RomImage::read32(uint32_t offset) const {
    uint8_t b[4];
    read(offset, b, 4);
    return load32(b);
}

std::vector<uint8_t> RomImage::bytes(uint32_t offset, uint32_t size) const {
    std::vector<uint8_t> out(size);
    read(offset, out.data(), size);
    return out;
}

// --- NitroFS --------------------------------------------------------------------------

std::vector<NitroFile> nitro_files(const RomImage& rom) {
    uint32_t fnt_off = rom.read32(0x40), fnt_size = rom.read32(0x44);
    uint32_t fat_off = rom.read32(0x48), fat_size = rom.read32(0x4C);
    std::vector<uint8_t> fnt = rom.bytes(fnt_off, fnt_size), fat = rom.bytes(fat_off, fat_size);
    std::vector<NitroFile> out;
    bool ok = true;
    // Directory entries: u32 offset of its names, u16 first file id, u16 parent/count.
    auto walk = [&](auto&& self, uint32_t dir, const std::string& prefix, int depth) -> void {
        uint32_t at = (dir & 0xFFF) * 8;
        if (depth > 64 || at + 8 > fnt.size()) {
            ok = false;
            return;
        }
        uint32_t pos = load32(&fnt[at]);
        uint16_t id = uint16_t(fnt[at + 4] | fnt[at + 5] << 8);
        while (ok) {
            if (pos >= fnt.size()) {
                ok = false;
                return;
            }
            uint8_t entry = fnt[pos++];
            if (entry == 0) return;
            uint32_t len = entry & 0x7F;
            if (pos + len + ((entry & 0x80) ? 2 : 0) > fnt.size()) {
                ok = false;
                return;
            }
            std::string name(reinterpret_cast<const char*>(&fnt[pos]), len);
            pos += len;
            if (entry & 0x80) {
                uint32_t child = fnt[pos] | fnt[pos + 1] << 8;
                pos += 2;
                self(self, child, prefix + name + "/", depth + 1);
            } else {
                if (uint64_t(id) * 8 + 8 > fat.size()) {
                    ok = false;
                    return;
                }
                out.push_back({prefix + name, id, load32(&fat[id * 8]), load32(&fat[id * 8 + 4])});
                id++;
            }
        }
    };
    walk(walk, 0xF000, "", 0);
    if (!ok) out.clear();
    return out;
}

// --- overlays ---------------------------------------------------------------------------

std::vector<NitroOverlay> nitro_overlays(const RomImage& rom) {
    uint32_t off = rom.read32(0x50), size = rom.read32(0x54);
    std::vector<uint8_t> table = rom.bytes(off, size);
    std::vector<NitroOverlay> out;
    for (uint32_t at = 0; at + kOverlayEntrySize <= table.size(); at += kOverlayEntrySize) {
        const uint8_t* e = &table[at];
        out.push_back({load32(e), load32(e + 4), load32(e + 8), load32(e + 12), load32(e + 16), load32(e + 20),
                       load32(e + 24), load32(e + 28)});
    }
    return out;
}

std::vector<uint8_t> nitro_overlay_image(const RomImage& rom, const NitroOverlay& ov) {
    uint32_t fat = rom.read32(0x48);
    uint32_t start = rom.read32(fat + ov.file_id * 8), end = rom.read32(fat + ov.file_id * 8 + 4);
    if (end < start) return {};
    std::vector<uint8_t> data = rom.bytes(start, end - start);
    if (ov.compressed()) {
        if (uint32_t packed_size = ov.packed & 0xFFFFFF; packed_size && packed_size < data.size()) data.resize(packed_size);
        if (!blz_decompress(data)) return {};
    }
    return data;
}

// The compressed region is read from its end towards its start. Footer: u32 [-8] = length
// of the compressed region (bits 0-23) and of the footer (bits 24-31); u32 [-4] = how
// much the data grows.
bool blz_decompress(std::vector<uint8_t>& data) {
    size_t n = data.size();
    if (n < 8) return false;
    uint32_t info = load32(&data[n - 8]), extra = load32(&data[n - 4]);
    size_t footer = info >> 24, enc = info & 0xFFFFFF;
    if (enc > n || footer > enc) return false;
    data.resize(n + extra);
    uint8_t* out = data.data();
    size_t src = n - footer, dst = n + extra, limit = n - enc;
    while (src > limit) {
        uint8_t flags = out[--src];
        for (int bit = 0; bit < 8 && src > limit; bit++, flags <<= 1) {
            if (flags & 0x80) {
                if (src < limit + 2) return false;
                src -= 2;
                uint32_t pair = out[src] | out[src + 1] << 8;
                size_t len = (pair >> 12) + 3, disp = (pair & 0xFFF) + 3;
                if (dst < len || dst - len < src || dst - 1 + disp >= data.size()) return false;
                for (size_t i = 0; i < len; i++, dst--) out[dst - 1] = out[dst - 1 + disp];
            } else {
                if (dst == 0) return false;
                out[--dst] = out[--src];
            }
        }
    }
    return true;
}

}  // namespace ntr
