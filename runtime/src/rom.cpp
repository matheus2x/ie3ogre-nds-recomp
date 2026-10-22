// The cartridge image, read by ROM offset: either a .nds dump or the game data package
// (boej.pak) that tools/extract.py makes from one (format in tools/ntr/pak.py). Both are
// memory-mapped, so opening is instant whatever the size.
#include <algorithm>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "runtime.h"

namespace ntr {

namespace {

struct Range {
    uint64_t rom_offset, size, file_offset;
};

struct Image {
    const uint8_t* base = nullptr;
    uint64_t file_size = 0;
    uint64_t rom_size = 0;
    std::vector<Range> ranges;  // sorted by rom_offset
};
Image g_image;

const uint8_t* map_file(const std::string& path, uint64_t& size) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
    HANDLE file = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER li;
    if (!GetFileSizeEx(file, &li) || li.QuadPart == 0) {
        CloseHandle(file);
        return nullptr;
    }
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(file);
    if (!mapping) return nullptr;
    void* p = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);  // the view keeps the mapping alive
    size = uint64_t(li.QuadPart);
    return static_cast<const uint8_t*>(p);
#else
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return nullptr;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size == 0) {
        close(fd);
        return nullptr;
    }
    void* p = mmap(nullptr, size_t(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return nullptr;
    size = uint64_t(st.st_size);
    return static_cast<const uint8_t*>(p);
#endif
}

uint32_t u32(const uint8_t* p) { return load32(p); }
uint64_t u64(const uint8_t* p) { return u32(p) | uint64_t(u32(p + 4)) << 32; }

constexpr char kPakMagic[8] = {'N', 'T', 'R', 'P', 'A', 'K', 0, 0};
constexpr uint32_t kPakVersion = 1;
constexpr uint32_t kPakHeaderSize = 0x38, kPakRangeSize = 24;

bool parse_pak(Image& img) {
    const uint8_t* p = img.base;
    if (u32(p + 8) != kPakVersion) {
        NTR_LOG("[rom] package version %u is not supported; run tools/extract.py again\n", u32(p + 8));
        return false;
    }
    img.rom_size = u64(p + 0x28);
    uint32_t count = u32(p + 0x30);
    if (kPakHeaderSize + uint64_t(count) * kPakRangeSize > img.file_size) return false;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* r = p + kPakHeaderSize + i * kPakRangeSize;
        Range range{u64(r), u64(r + 8), u64(r + 16)};
        if (range.file_offset + range.size > img.file_size) return false;
        img.ranges.push_back(range);
    }
    std::sort(img.ranges.begin(), img.ranges.end(),
              [](const Range& a, const Range& b) { return a.rom_offset < b.rom_offset; });
    char sha1[41];
    for (int i = 0; i < 20; i++) std::snprintf(sha1 + 2 * i, 3, "%02x", p[0x10 + i]);
    NTR_LOG("[rom] game data package, %u ranges, made from dump %s\n", count, sha1);
    return true;
}

}  // namespace

bool rom_load(const std::string& path) {
    Image img;
    img.base = map_file(path, img.file_size);
    if (!img.base) return false;
    if (img.file_size >= kPakHeaderSize && std::memcmp(img.base, kPakMagic, 8) == 0) {
        if (!parse_pak(img)) return false;
    } else {
        if (img.file_size < 0x200) return false;
        img.rom_size = img.file_size;
        img.ranges.push_back({0, img.file_size, 0});
    }
    g_image = std::move(img);
    char code[5] = {};
    rom_read(0x0C, code, 4);
    if (std::strcmp(code, "BOEJ") != 0) NTR_LOG("[rom] warning: game code is %s, not BOEJ\n", code);
    return true;
}

void rom_read(uint32_t offset, void* dst, uint32_t size) {
    uint8_t* out = static_cast<uint8_t*>(dst);
    std::memset(out, 0xFF, size);
    const auto& ranges = g_image.ranges;
    // First range that ends after `offset`.
    auto it = std::upper_bound(ranges.begin(), ranges.end(), uint64_t(offset),
                               [](uint64_t off, const Range& r) { return off < r.rom_offset + r.size; });
    uint64_t end = uint64_t(offset) + size;
    for (; it != ranges.end() && it->rom_offset < end; ++it) {
        uint64_t lo = std::max<uint64_t>(offset, it->rom_offset);
        uint64_t hi = std::min(end, it->rom_offset + it->size);
        if (lo < hi) std::memcpy(out + (lo - offset), g_image.base + it->file_offset + (lo - it->rom_offset), hi - lo);
    }
}

uint32_t rom_read32(uint32_t offset) {
    uint8_t b[4];
    rom_read(offset, b, 4);
    return load32(b);
}

}  // namespace ntr
