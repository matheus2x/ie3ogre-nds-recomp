// The game's save read on the host (ntr/savedata.h).
#include <algorithm>
#include <cstring>

#include <ntr/common.h>
#include <ntr/savedata.h>

namespace ntr {

namespace {

constexpr size_t kHalf = 0x8000;
constexpr uint32_t kSchemaVersion = 0x24;
constexpr size_t kHeader = 0x2E;  // SRecordHeader, after SRecordInfo
// Bit offsets in st_data (from kHeader) for the schema version 0x24.
constexpr uint32_t kTeamBits = 89483;  // TeamData.team_member[0].unitno_: 16 x 12 bits
constexpr uint32_t kUnitBits = 32683, kUnitStride = 562, kUnits = 100;  // UnitData.unit_save[]
constexpr uint32_t kUnitNo = 20, kUniformNo = 507;  // in a unit_save: 12 and 5 bits

uint32_t lcg(uint32_t x) { return x * 0x5D588B65u + 0x269EC3u; }

// The game's decryption (0x0205ffb0 with mode 0) of `length` bytes; bytes 4..0x13 (the
// magic number) are never touched.
bool decrypt(uint8_t* buf, uint32_t length) {
    const uint32_t n = length - 12;
    const uint32_t q = (n & 0xFFFF) >> 4;
    uint32_t seed = 0xB5BDCF5Au;
    uint32_t p = 0x20 + (q ? (q * (seed >> 16)) >> 16 : seed >> 16);
    uint8_t key[12];
    for (int i = 0; i < 12; i++) {
        seed = lcg(seed);
        uint32_t step = seed >> 16;
        if (q) step = (step * q) >> 16;
        p += step + 2;
        if (p >= n) return false;
        key[i] = buf[p];
        buf[p] = buf[n + i];  // the key byte took the place of one of the last 12
    }
    uint32_t k0 = load32(key), k1 = load32(key + 4), k2 = load32(key + 8);
    uint16_t table[256];
    for (uint16_t& t : table) {
        k0 = lcg(k0);
        t = uint16_t(k0 >> 16);
    }
    auto fixed = [](uint32_t i) { return i >= 4 && i <= 0x13; };
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = (i + ((n * table[i & 0xFF]) >> 16)) % n;
        if (!fixed(i) && !fixed(j)) std::swap(buf[i], buf[j]);
    }
    for (uint32_t i = 0; i < n; i++) {
        k1 = lcg(k1);
        if (!fixed(i)) buf[i] ^= uint8_t(k1 >> 24);
    }
    for (uint32_t i = 0; i < n; i++) {
        k2 = lcg(k2);
        if (!fixed(i)) buf[i] = uint8_t(((buf[i] << 8) | buf[i]) >> (k2 >> 29));
    }
    return true;
}

}  // namespace

SaveSummary read_save_data(const uint8_t* data, size_t size) {
    SaveSummary info;
    if (size < kSaveDataSize) return info;
    if (std::all_of(data, data + kSaveDataSize, [](uint8_t b) { return b == 0xFF; })) {
        info.valid = info.blank = true;
        return info;
    }
    static const char kMagic[] = "INAZUMA_ELEVEN3";
    const uint8_t* best = nullptr;
    std::vector<uint8_t> halves[2];
    for (int h = 0; h < 2; h++) {
        std::vector<uint8_t>& half = halves[h];
        half.assign(data + h * kHalf, data + (h + 1) * kHalf);
        if (std::memcmp(half.data() + 4, kMagic, sizeof kMagic) != 0) continue;
        info.valid = true;
        // Only the first 0x80 bytes in use: the save was made but the game was never saved.
        if (std::all_of(half.begin() + 0x80, half.begin() + 0x100, [](uint8_t b) { return b == 0; })) continue;
        if (!decrypt(half.data(), 0x7F80)) continue;
        if (load32(half.data() + 0x18) != kSchemaVersion) {
            info.unknown = true;
            continue;
        }
        // The newer copy by its counter, compared like the game does (8 bits, wrapping).
        if (best && (uint8_t(load32(half.data() + 0x14) - load32(best + 0x14)) & 0x80)) continue;
        best = half.data();
    }
    if (!best) return info;
    const uint8_t* head = best + kHeader;
    info.empty = false;
    info.unknown = false;
    info.play_frames = load32(head + 0x10);
    info.chapter = int8_t(head[0x14]);
    info.level = head[0x15];
    info.members = head[0x17];
    std::memcpy(info.user, head + 0x1C, sizeof info.user);

    const uint32_t limit = uint32_t(kHalf - 0x80 - kHeader) * 8;  // the decrypted part of the half
    auto bits = [&](uint32_t at, int n) {
        uint32_t v = 0;
        for (int k = 0; k < n && at + k < limit; k++) v |= uint32_t(head[(at + k) >> 3] >> ((at + k) & 7) & 1) << k;
        return v;
    };
    for (uint32_t i = 0; i < kUnits; i++) {
        uint32_t at = kUnitBits + i * kUnitStride;
        uint16_t unit = uint16_t(bits(at + kUnitNo, 12));
        info.shirt[unit] = bits(at + kUniformNo, 5);
        if (unit) info.units.push_back(unit);
    }
    for (uint32_t i = 0; i < 16; i++) {
        if (uint16_t unit = uint16_t(bits(kTeamBits + i * 12, 12))) info.team_units.push_back(unit);
    }
    return info;
}

}  // namespace ntr
