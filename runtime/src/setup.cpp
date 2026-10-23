// First-run setup without Python: identifies the player's .nds dump by SHA-1 and writes
// the game data package (boej.pak) from it, byte for byte what tools/extract.py writes
// (format in tools/ntr/pak.py).
#include <algorithm>
#include <cstring>
#include <system_error>
#include <vector>

#include <ntr/common.h>

namespace ntr {

namespace {

// Keep in sync with config/boej/dumps.json (tools/tests checks it). Only the retail dump
// gets a package: the code is recompiled from it.
constexpr KnownDump kKnownDumps[] = {
    {"9380f1d7ee75e7c6ef24f1b76c88ecdc85088957", "retail", true},
    {"a70e9a336f6ea8ff78003938dcd024533f034a68", "fan-translation", false},
};

constexpr uint64_t kAlign = 0x1000, kMergeGap = 0x1000;

uint64_t align_up(uint64_t v) { return (v + kAlign - 1) / kAlign * kAlign; }

// (start, end) of every region the game can read, merged and sorted (pak.py rom_ranges).
std::vector<std::pair<uint64_t, uint64_t>> rom_ranges(const uint8_t* rom, uint64_t size) {
    std::vector<std::pair<uint64_t, uint64_t>> spans = {{0, 0x4000}};  // header and secure area
    for (uint32_t at : {0x20u, 0x30u, 0x40u, 0x48u, 0x50u, 0x58u}) {  // ARM9, ARM7, FNT, FAT, OVT9, OVT7
        uint32_t off = load32(rom + at), len = load32(rom + at + (at < 0x40 ? 0x0C : 4));
        if (len) spans.push_back({off, uint64_t(off) + len});
    }
    if (uint32_t banner = load32(rom + 0x68)) spans.push_back({banner, uint64_t(banner) + 0x1240});
    uint32_t fat = load32(rom + 0x48), fat_size = load32(rom + 0x4C);
    for (uint64_t i = 0; i + 8 <= fat_size && fat + i + 8 <= size; i += 8) {
        uint32_t start = load32(rom + fat + i), end = load32(rom + fat + i + 4);
        if (end > start) spans.push_back({start, end});
    }
    std::sort(spans.begin(), spans.end());
    std::vector<std::pair<uint64_t, uint64_t>> merged;
    for (auto [start, end] : spans) {
        end = std::min(end, size);
        if (start >= end) continue;
        if (!merged.empty() && start <= merged.back().second + kMergeGap) {
            merged.back().second = std::max(merged.back().second, end);
        } else {
            merged.push_back({start, end});
        }
    }
    return merged;
}

void put32(std::vector<uint8_t>& out, size_t at, uint32_t v) { store32(out.data() + at, v); }

void put64(std::vector<uint8_t>& out, size_t at, uint64_t v) {
    put32(out, at, uint32_t(v));
    put32(out, at + 4, uint32_t(v >> 32));
}

bool write_pak(const uint8_t* rom, uint64_t size, const std::string& sha1, const std::string& path,
               const std::function<void(double)>& progress, double done, std::string& error) {
    auto ranges = rom_ranges(rom, size);
    uint64_t pos = align_up(0x38 + 24 * ranges.size());
    std::vector<uint8_t> head(pos, 0);
    std::memcpy(head.data(), "NTRPAK\0\0", 8);
    put32(head, 0x08, 1);
    std::memcpy(head.data() + 0x0C, rom + 0x0C, 4);
    for (int i = 0; i < 20; i++) head[0x10 + i] = uint8_t(std::stoul(sha1.substr(2 * i, 2), nullptr, 16));
    put64(head, 0x28, size);
    put32(head, 0x30, uint32_t(ranges.size()));
    uint64_t total = 0;
    for (size_t i = 0; i < ranges.size(); i++) {
        auto [start, end] = ranges[i];
        put64(head, 0x38 + 24 * i, start);
        put64(head, 0x38 + 24 * i + 8, end - start);
        put64(head, 0x38 + 24 * i + 16, pos);
        pos = align_up(pos + end - start);
        total += end - start;
    }

    std::filesystem::path final_path = fs_path(path), tmp = final_path;
    tmp += ".tmp";
    FILE* f = open_file(tmp, "wb");
    if (!f) {
        error = "Não foi possível criar " + utf8_string(tmp) + ". Coloque o jogo numa pasta em que dê para gravar.";
        return false;
    }
    static const uint8_t kZeros[kAlign] = {};
    bool ok = std::fwrite(head.data(), 1, head.size(), f) == head.size();
    uint64_t written = 0;
    for (auto [start, end] : ranges) {
        for (uint64_t at = start; ok && at < end;) {
            size_t n = size_t(std::min<uint64_t>(end - at, 8u << 20));
            ok = std::fwrite(rom + at, 1, n, f) == n;
            at += n;
            written += n;
            progress(done + (1 - done) * double(written) / double(total));
        }
        size_t padding = size_t(align_up(end - start) - (end - start));
        if (ok && padding) ok = std::fwrite(kZeros, 1, padding, f) == padding;
    }
    ok = std::fclose(f) == 0 && ok;
    std::error_code ec;
    if (ok) std::filesystem::rename(tmp, final_path, ec);
    if (!ok || ec) {
        std::filesystem::remove(tmp, ec);
        error = "Não foi possível gravar " + path + " (o disco está cheio?).";
        return false;
    }
    return true;
}

}  // namespace

const KnownDump* find_dump(const std::string& sha1) {
    for (const KnownDump& d : kKnownDumps) {
        if (sha1 == d.sha1) return &d;
    }
    return nullptr;
}

const KnownDump& supported_dump() { return kKnownDumps[0]; }

std::string sha1_file(const std::string& path, const std::function<void(double)>& progress) {
    uint64_t size = 0;
    const uint8_t* data = map_file(path, size);
    if (!data) return "";
    Sha1 sha;
    for (uint64_t at = 0; at < size;) {
        size_t n = size_t(std::min<uint64_t>(size - at, 8u << 20));
        sha.update(data + at, n);
        at += n;
        progress(double(at) / double(size));
    }
    unmap_file(data, size);
    return sha.hex();
}

std::string dump_problem(const std::string& sha1) {
    const KnownDump* known = find_dump(sha1);
    if (!known) {
        return "Este arquivo não é um dump conhecido de Inazuma Eleven 3 - The Ogre (BOEJ).\n\n"
               "SHA-1: " + sha1 + "\nEsperado: " + supported_dump().sha1 +
               "\n\nFaça o dump do seu cartucho sem modificações.";
    }
    if (!known->supported) {
        return std::string("Este dump é uma versão modificada (") + known->variant +
               ") que ainda não é suportada.\n\nUse o dump do cartucho original, sem modificações.";
    }
    return "";
}

bool pak_create(const std::string& nds_path, const std::string& pak_path, std::string& error,
                const std::function<void(double)>& progress) {
    uint64_t size = 0;
    const uint8_t* rom = map_file(nds_path, size);
    if (!rom) {
        error = "Não foi possível ler " + nds_path + ".";
        return false;
    }
    NTR_LOG("[setup] checking %s (%llu bytes)\n", nds_path.c_str(), (unsigned long long)size);
    // The hash reads the whole dump and the package writes most of it again: about half
    // of the work each.
    constexpr double kHashShare = 0.5;
    std::string sha1 = sha1_file(nds_path, [&](double p) { progress(kHashShare * p); });
    const KnownDump* known = find_dump(sha1);
    NTR_LOG("[setup] SHA-1 %s: %s\n", sha1.c_str(), known ? known->variant : "unknown dump");
    error = dump_problem(sha1);
    bool ok = error.empty() && write_pak(rom, size, sha1, pak_path, progress, kHashShare, error);
    if (ok) NTR_LOG("[setup] wrote %s\n", pak_path.c_str());
    unmap_file(rom, size);
    return ok;
}

}  // namespace ntr
