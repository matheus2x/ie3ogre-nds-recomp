// The Saves tab: the .sav files in saves/, what each one holds (the same summary as the
// game's slot card) and which one the game uses ([game] save in boej.ini, read by
// arm7_card_init in the runtime). Every save is named "<name>-<n>.sav": the name the player
// typed in "Novo save", or the file's own name for one imported (dropped on the window or put
// in the folder by hand), in lowercase ASCII letters and digits (kana in romaji), and n the
// first number free for that name. [game] save follows a rename.
//
// The save (64 KB EEPROM) has two copies of the game, one per 32 KB half; the game loads
// the newer one (load at 0x020617d8). Each half starts with "INAZUMA_ELEVEN3" at +4 and is
// encrypted except for bytes 4..0x13 (cipher at 0x0205ffb0, with the key hidden inside the
// block). Decrypted, it is a bit stream written from the type table in
// /data_iz/bfmt/cnvdat.bin: SRecordInfo (0x2E bytes) and then st_data, which starts with
// SRecordHeader, the summary the slot card shows. All the fields read here are whole bytes,
// so their offsets are fixed for the schema version 0x24. Further in st_data, the fields are
// packed bits (LSB first, each with its width from the table): the team (TeamData.team_member[0],
// the unit numbers) and each unit's shirt number (UnitData.unit_save[].uniformno), at bit
// offsets worked out from cnvdat.bin.
//
// The team's faces are the icons of the friends list: /data_iz/pic3d/ih.pkh/.pkb in boej.pak,
// keyed by unit number. That .pkh is "PackNum" with (u32 key, u32 size) entries from 0x30; the
// .pkb keeps each entry at index * (the largest size, u32 at 0x1C). An entry is LZ10 of a 3D PAC:
// a 32x32 4 bpp texture, 16 colours (colour 0 transparent) and the sizes.
//
// Each row is drawn like the game's slot card (pic2d/menu/MMSlot.SPF_: SFDN_BG00, its
// colours), and the save in use is marked with the card's lightning box (SFDN_I00 there: a
// 2D PAC of 12 tiles, two 24x16 pictures of 3x2 tiles; the first is the blue box).
#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <ctime>
#include <map>

#ifdef _WIN32
#include <windows.h>
#else
#include <iconv.h>
#endif

#include <ntr/nitro.h>

#include "launcher.h"

namespace fs = std::filesystem;

namespace launcher {

namespace {

constexpr size_t kSaveSize = 0x10000, kHalf = 0x8000;
constexpr uint32_t kSchemaVersion = 0x24;
constexpr size_t kHeader = 0x2E;  // SRecordHeader, after SRecordInfo
// Bit offsets in st_data (from kHeader) for the schema version 0x24.
constexpr uint32_t kTeamBits = 89483;  // TeamData.team_member[0].unitno_: 16 x 12 bits
constexpr uint32_t kUnitBits = 32683, kUnitStride = 562, kUnits = 100;  // UnitData.unit_save[]
constexpr uint32_t kUnitNo = 20, kUniformNo = 507;  // in a unit_save: 12 and 5 bits

struct SaveInfo {
    std::string file;  // the name in saves/
    std::string modified;
    bool valid = false;    // a save of this game
    bool empty = true;     // valid, but no game saved in it yet
    bool blank = false;    // all 0xFF, like a new cartridge (made by "Novo save")
    bool unknown = false;  // valid, another schema version
    uint32_t play_frames = 0;
    int chapter = 0, level = 0, members = 0;
    std::string user;
    std::vector<uint16_t> team_units;  // unit numbers of the team, by shirt number
};

std::vector<SaveInfo> g_saves;
std::string g_listing;  // names, sizes and dates at the last scan
double g_listing_time = -10;
std::string g_status;
bool g_status_error = false;
char g_new_name[64] = "";

uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
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
    uint32_t k0 = rd32(key), k1 = rd32(key + 4), k2 = rd32(key + 8);
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

// The game's names are Shift-JIS; full-width letters and digits become ASCII.
std::string from_sjis(const uint8_t* p, size_t max) {
    size_t len = 0;
    while (len < max && p[len]) len++;
    std::string out;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(p), int(len), nullptr, 0);
    std::wstring wide(size_t(std::max(wlen, 0)), L'\0');
    MultiByteToWideChar(932, 0, reinterpret_cast<const char*>(p), int(len), wide.data(), wlen);
    std::u32string text(wide.begin(), wide.end());  // the names have no characters outside the BMP
#else
    std::u32string text;
    if (iconv_t cd = iconv_open("UTF-32LE", "CP932"); cd != iconv_t(-1)) {
        std::string in(reinterpret_cast<const char*>(p), len);
        std::vector<char> buf(len * 4 + 4);
        char* src = in.data();
        char* dst = buf.data();
        size_t in_left = in.size(), out_left = buf.size();
        iconv(cd, &src, &in_left, &dst, &out_left);
        iconv_close(cd);
        for (char* c = buf.data(); c + 4 <= dst; c += 4) text += char32_t(rd32(reinterpret_cast<const uint8_t*>(c)));
    }
#endif
    for (char32_t c : text) {
        if (c >= 0xFF01 && c <= 0xFF5E) c -= 0xFF01 - 0x21;
        else if (c == 0x3000) c = ' ';
        if (c < 0x80) out += char(c);
        else if (c < 0x800) out += {char(0xC0 | c >> 6), char(0x80 | (c & 0x3F))};
        else out += {char(0xE0 | c >> 12), char(0x80 | (c >> 6 & 0x3F)), char(0x80 | (c & 0x3F))};
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::u32string decode_utf8(const std::string& s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        uint8_t c = uint8_t(s[i]);
        int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        char32_t cp = len == 1 ? c : len == 2 ? c & 0x1F : len == 3 ? c & 0x0F : c & 0x07;
        for (int k = 1; k < len && i + k < s.size(); k++) cp = cp << 6 | (uint8_t(s[i + k]) & 0x3F);
        out += cp;
        i += len;
    }
    return out;
}

// Hepburn romaji of the hiragana U+3041..U+3096 (katakana is the same 0x60 higher). "" is the
// small tsu, which doubles the next consonant.
const char* const kRomaji[] = {
    "a",  "a",  "i",  "i",   "u",  "u",  "e",  "e",  "o",  "o",  "ka", "ga", "ki", "gi", "ku",  "gu",  "ke", "ge",
    "ko", "go", "sa", "za",  "shi", "ji", "su", "zu", "se", "ze", "so", "zo", "ta", "da", "chi", "ji",  "",   "tsu",
    "zu", "te", "de", "to",  "do", "na", "ni", "nu", "ne", "no", "ha", "ba", "pa", "hi", "bi", "pi",  "fu",  "bu",
    "pu", "he", "be", "pe",  "ho", "bo", "po", "ma", "mi", "mu", "me", "mo", "ya", "ya", "yu", "yu",  "yo",  "yo",
    "ra", "ri", "ru", "re",  "ro", "wa", "wa", "i",  "e",  "o",  "n",  "vu", "ka", "ke",
};

// A name as a file name: lowercase ASCII letters and digits, accents dropped, kana in romaji,
// anything else (spaces, symbols, kanji) left out. "" when nothing is left.
std::string file_name_for(const std::string& user) {
    static const std::u32string kAccented = U"ÀÁÂÃÄÅàáâãäåÇçÈÉÊËèéêëÌÍÎÏìíîïÑñÒÓÔÕÖØòóôõöøÙÚÛÜùúûüÝýÿ";
    static const char kPlain[] = "AAAAAAaaaaaaCcEEEEeeeeIIIIiiiiNnOOOOOOooooooUUUUuuuuYyy";
    std::string out;
    bool tsu = false;  // a small tsu before this kana
    auto add = [&](std::string text) {
        if (tsu && !text.empty() && std::isalpha(uint8_t(text[0])) && !std::strchr("aeiou", text[0])) text.insert(0, 1, text[0] == 'c' ? 't' : text[0]);
        tsu = false;
        out += text;
    };
    for (char32_t c : decode_utf8(user)) {
        if (c < 0x80 && std::isalnum(int(c))) {
            add(std::string(1, char(std::tolower(int(c)))));
        } else if (size_t a = kAccented.find(c); a != std::u32string::npos) {
            add(std::string(1, char(std::tolower(uint8_t(kPlain[a])))));
        } else if ((c >= 0x3041 && c <= 0x3096) || (c >= 0x30A1 && c <= 0x30F6)) {
            char32_t h = c >= 0x30A1 ? c - 0x60 : c;
            bool small_y = h == 0x3083 || h == 0x3085 || h == 0x3087;  // ゃ ゅ ょ after an i: kya, sha, ja
            if (small_y && out.size() >= 2 && out.back() == 'i') {
                out.pop_back();
                bool soft = out.back() == 'j' || (out.size() >= 2 && out.back() == 'h' && std::strchr("sc", out[out.size() - 2]));
                out += std::string(soft ? "" : "y") + kRomaji[h - 0x3041][1];
            } else if (h == 0x3063) {
                tsu = true;
            } else {
                add(kRomaji[h - 0x3041]);
            }
        } else if (c == 0x30FC) {
            // ー (long vowel): nothing
        } else {
            tsu = false;
        }
    }
    if (out.size() > 32) out.resize(32);
    return out;
}

std::string lowercase(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

// "<name>-<n>" with the name already sanitized.
bool formatted(const std::string& stem) {
    size_t dash = stem.rfind('-');
    if (dash == std::string::npos || dash == 0 || dash + 1 == stem.size()) return false;
    return std::all_of(stem.begin(), stem.begin() + dash, [](char c) { return std::islower(uint8_t(c)) || std::isdigit(uint8_t(c)); }) &&
           std::all_of(stem.begin() + dash + 1, stem.end(), [](char c) { return std::isdigit(uint8_t(c)); });
}

// "<base>-<n>.sav" with the first n not in use.
std::string free_name(const std::string& base) {
    std::error_code ec;
    std::string file;
    for (int n = 1; file.empty() || fs::exists(ntr::fs_path(paths().saves() + file), ec); n++)
        file = base + "-" + std::to_string(n) + ".sav";
    return file;
}

// The name a file in saves/ (or one imported) should have; "" when it has it already.
std::string target_name(const std::string& file) {
    std::string stem = ntr::utf8_string(ntr::fs_path(file).stem());
    if (formatted(stem)) return "";
    // Only the case is wrong: keep the number (on Windows the new name "exists" already).
    if (formatted(lowercase(stem))) return lowercase(stem) + ".sav";
    std::string base = file_name_for(stem);
    return free_name(base.empty() ? "save" : base);
}

void read_save(const fs::path& path, SaveInfo& info) {
    std::vector<uint8_t> data(kSaveSize);
    FILE* f = ntr::open_file(path, "rb");
    if (!f) return;
    size_t got = std::fread(data.data(), 1, data.size(), f);
    std::fclose(f);
    if (got < kSaveSize) return;
    if (std::all_of(data.begin(), data.end(), [](uint8_t b) { return b == 0xFF; })) {
        info.valid = info.blank = true;
        return;
    }
    static const char kMagic[] = "INAZUMA_ELEVEN3";
    const uint8_t* best = nullptr;
    std::vector<uint8_t> halves[2];
    for (int h = 0; h < 2; h++) {
        std::vector<uint8_t>& half = halves[h];
        half.assign(data.begin() + h * kHalf, data.begin() + (h + 1) * kHalf);
        if (std::memcmp(half.data() + 4, kMagic, sizeof kMagic) != 0) continue;
        info.valid = true;
        // Only the first 0x80 bytes in use: the save was made but the game was never saved.
        if (std::all_of(half.begin() + 0x80, half.begin() + 0x100, [](uint8_t b) { return b == 0; })) continue;
        if (!decrypt(half.data(), 0x7F80)) continue;
        if (rd32(half.data() + 0x18) != kSchemaVersion) {
            info.unknown = true;
            continue;
        }
        // The newer copy by its counter, compared like the game does (8 bits, wrapping).
        if (best && (uint8_t(rd32(half.data() + 0x14) - rd32(best + 0x14)) & 0x80)) continue;
        best = half.data();
    }
    if (!best) return;
    const uint8_t* head = best + kHeader;
    info.empty = false;
    info.unknown = false;
    info.play_frames = rd32(head + 0x10);
    info.chapter = int8_t(head[0x14]);
    info.level = head[0x15];
    info.members = head[0x17];
    info.user = from_sjis(head + 0x1C, 24);

    const uint32_t limit = uint32_t(kHalf - 0x80 - kHeader) * 8;  // the decrypted part of the half
    auto bits = [&](uint32_t at, int n) {
        uint32_t v = 0;
        for (int k = 0; k < n && at + k < limit; k++) v |= uint32_t(head[(at + k) >> 3] >> ((at + k) & 7) & 1) << k;
        return v;
    };
    std::map<uint16_t, uint32_t> shirt;
    for (uint32_t i = 0; i < kUnits; i++) {
        uint32_t at = kUnitBits + i * kUnitStride;
        shirt[uint16_t(bits(at + kUnitNo, 12))] = bits(at + kUniformNo, 5);
    }
    for (uint32_t i = 0; i < 16; i++) {
        if (uint16_t unit = uint16_t(bits(kTeamBits + i * 12, 12))) info.team_units.push_back(unit);
    }
    std::stable_sort(info.team_units.begin(), info.team_units.end(), [&](uint16_t a, uint16_t b) { return shirt[a] < shirt[b]; });
}

std::string modified_text(const fs::directory_entry& entry) {
    std::error_code ec;
    auto when = entry.last_write_time(ec);
    if (ec) return "";
    auto sys = std::chrono::system_clock::now() +
               std::chrono::duration_cast<std::chrono::system_clock::duration>(when - fs::file_time_type::clock::now());
    std::time_t t = std::chrono::system_clock::to_time_t(sys);
    char text[64];
    std::strftime(text, sizeof text, L("%Y-%m-%d %H:%M", "%d/%m/%Y %H:%M"), std::localtime(&t));
    return text;
}

bool is_sav(const std::string& name) {
    std::string ext = ntr::utf8_string(ntr::fs_path(name).extension());
    for (char& c : ext) c = char(std::tolower(uint8_t(c)));
    return ext == ".sav";
}

// By name, with the blank ones from "Novo save" first: the one just made shows up at the top.
void sort_saves() {
    std::sort(g_saves.begin(), g_saves.end(), [](const SaveInfo& a, const SaveInfo& b) {
        return a.blank != b.blank ? a.blank : a.file < b.file;
    });
}

bool same_file(const std::string& a, const std::string& b) {
#ifdef _WIN32
    return lowercase(a) == lowercase(b);
#else
    return a == b;
#endif
}

// --- the team's icons ------------------------------------------------------------------

constexpr int kIconCropX = 4, kIconCropY = 6, kIconW = 25, kIconH = 23;  // the faces' area in the 32x32
constexpr int kBoltCropX = 4, kBoltW = 17, kBoltH = 15;                  // the box's area in the 24x16

struct Icons {
    bool loaded = false;
    std::vector<uint32_t> bolt_pixels;  // RGBA, kBoltW x kBoltH; empty without the package
    SDL_Texture* bolt = nullptr;
    std::vector<uint8_t> pkb;
    uint32_t stride = 0;
    std::map<uint16_t, std::pair<uint32_t, uint32_t>> entries;  // unit -> (offset, size) in pkb
    std::map<uint16_t, SDL_Texture*> textures;                  // nullptr: no icon
};
Icons g_icons;

std::vector<uint8_t> lz10(const uint8_t* src, size_t size) {
    std::vector<uint8_t> out;
    if (size < 4 || src[0] != 0x10) return out;
    size_t want = src[1] | src[2] << 8 | src[3] << 16, at = 4;
    out.reserve(want);
    while (out.size() < want && at < size) {
        uint8_t flags = src[at++];
        for (int b = 7; b >= 0 && out.size() < want && at < size; b--) {
            if (!(flags >> b & 1)) {
                out.push_back(src[at++]);
                continue;
            }
            if (at + 1 >= size) return {};
            size_t len = (src[at] >> 4) + 3, disp = ((src[at] & 0xF) << 8 | src[at + 1]) + 1;
            at += 2;
            if (disp > out.size()) return {};
            for (size_t k = 0; k < len && out.size() < want; k++) out.push_back(out[out.size() - disp]);
        }
    }
    if (out.size() != want) out.clear();
    return out;
}

uint32_t rgba(const uint8_t* pal, int c) {
    if (c == 0) return 0;  // transparent
    uint16_t v = uint16_t(pal[c * 2] | pal[c * 2 + 1] << 8);
    uint32_t r = (v & 31) * 255 / 31, g = (v >> 5 & 31) * 255 / 31, b = (v >> 10 & 31) * 255 / 31;
    return r | g << 8 | b << 16 | 0xFFu << 24;  // RGBA32 in memory order
}

// A PAC's sections: u32 count, then (u32 offset, u32 size) each.
std::vector<std::pair<uint32_t, uint32_t>> pac_sections(const std::vector<uint8_t>& pac) {
    std::vector<std::pair<uint32_t, uint32_t>> out;
    uint32_t n = pac.size() >= 4 ? rd32(pac.data()) : 0;
    for (uint32_t i = 0; i < n && i < 8 && 12 + i * 8 <= pac.size(); i++) {
        uint32_t at = rd32(pac.data() + 4 + i * 8), size = rd32(pac.data() + 8 + i * 8);
        if (uint64_t(at) + size > pac.size()) return {};
        out.push_back({at, size});
    }
    return out;
}

// SFDN_I00 of MMSlot.SPF_ (LZ10 of an SFP archive: index of (u32 name offset, u32 size,
// u32 offset / 0x20, u32) from 0x20, data from the u32 at 0x10).
void load_bolt(const std::vector<uint8_t>& spf) {
    std::vector<uint8_t> d = spf.size() >= 4 && std::memcmp(spf.data(), "SFP", 4) == 0 ? spf : lz10(spf.data(), spf.size());
    if (d.size() < 0x24 || std::memcmp(d.data(), "SFP", 4) != 0) return;
    uint32_t base = rd32(d.data() + 0x10), end = rd32(d.data() + 0x20);
    for (uint32_t k = 0x20; k + 16 <= end && k + 16 <= d.size(); k += 16) {
        uint32_t name = rd32(d.data() + k), size = rd32(d.data() + k + 4), at = base + rd32(d.data() + k + 8) * 0x20;
        if (name >= d.size() || std::strncmp(reinterpret_cast<const char*>(d.data() + name), "SFDN_I00.PAC", d.size() - name) != 0)
            continue;
        if (uint64_t(at) + size > d.size()) return;
        std::vector<uint8_t> pac(d.begin() + at, d.begin() + at + size);
        auto parts = pac_sections(pac);  // palette, map, tiles
        if (parts.size() < 3 || parts[0].second < 32 || parts[2].second < 6 * 32) return;
        const uint8_t* pal = pac.data() + parts[0].first;
        const uint8_t* tiles = pac.data() + parts[2].first;
        g_icons.bolt_pixels.resize(size_t(kBoltW) * kBoltH);
        for (int y = 0; y < kBoltH; y++) {
            for (int x = 0; x < kBoltW; x++) {
                int sx = x + kBoltCropX, tile = (y / 8) * 3 + sx / 8;
                g_icons.bolt_pixels[size_t(y) * kBoltW + x] = rgba(pal, tiles[tile * 32 + (y % 8) * 4 + (sx % 8) / 2] >> (4 * (sx & 1)) & 15);
            }
        }
        return;
    }
}

// ih.pkh/.pkb and MMSlot.SPF_ from boej.pak, once. Without the package (game not prepared
// yet): no pictures.
void load_icons() {
    g_icons.loaded = true;
    ntr::RomImage rom;
    if (!rom.open(paths().pak())) return;
    std::vector<uint8_t> pkh;
    for (const ntr::NitroFile& f : ntr::nitro_files(rom)) {
        if (f.path == "data_iz/pic3d/ih.pkh") pkh = rom.bytes(f.start, f.end - f.start);
        else if (f.path == "data_iz/pic3d/ih.pkb") g_icons.pkb = rom.bytes(f.start, f.end - f.start);
        else if (f.path == "data_iz/pic2d/menu/MMSlot.SPF_") load_bolt(rom.bytes(f.start, f.end - f.start));
    }
    if (pkh.size() < 0x30 || std::memcmp(pkh.data(), "PackNum", 7) != 0 || g_icons.pkb.empty()) {
        NTR_LOG("[launcher] no team icons: ih.pkh/ih.pkb not found in boej.pak\n");
        g_icons.pkb.clear();
        return;
    }
    uint32_t count = pkh[0x16] | pkh[0x17] << 8;
    g_icons.stride = rd32(pkh.data() + 0x1C);
    for (uint32_t i = 0; i < count && 0x30 + i * 8 + 8 <= pkh.size(); i++) {
        uint32_t key = rd32(pkh.data() + 0x30 + i * 8), size = rd32(pkh.data() + 0x34 + i * 8);
        if (key < 0x10000 && uint64_t(i) * g_icons.stride + size <= g_icons.pkb.size())
            g_icons.entries[uint16_t(key)] = {i * g_icons.stride, size};
    }
}

SDL_Texture* make_texture(SDL_Renderer* renderer, const std::vector<uint32_t>& pixels, int w, int h) {
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!texture) return nullptr;
    SDL_UpdateTexture(texture, nullptr, pixels.data(), w * 4);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return texture;
}

SDL_Texture* bolt(SDL_Renderer* renderer) {
    if (!g_icons.loaded) load_icons();
    if (!g_icons.bolt && !g_icons.bolt_pixels.empty()) {
        g_icons.bolt = make_texture(renderer, g_icons.bolt_pixels, kBoltW, kBoltH);
        g_icons.bolt_pixels.clear();
    }
    return g_icons.bolt;
}

SDL_Texture* icon(SDL_Renderer* renderer, uint16_t unit) {
    if (!g_icons.loaded) load_icons();
    auto cached = g_icons.textures.find(unit);
    if (cached != g_icons.textures.end()) return cached->second;
    SDL_Texture*& texture = g_icons.textures[unit];
    auto entry = g_icons.entries.find(unit);
    if (entry == g_icons.entries.end()) return nullptr;
    std::vector<uint8_t> pac = lz10(g_icons.pkb.data() + entry->second.first, entry->second.second);
    // PAC: u32 count, (u32 offset, u32 size) per section: texture, palette, sizes.
    if (pac.size() < 28 || rd32(pac.data()) != 3) return nullptr;
    uint32_t tex_at = rd32(pac.data() + 4), tex_size = rd32(pac.data() + 8);
    uint32_t pal_at = rd32(pac.data() + 12), par_at = rd32(pac.data() + 20);
    if (par_at + 4 > pac.size() || pal_at + 32 > pac.size()) return nullptr;
    int w = 8 << pac[par_at + 2], h = 8 << pac[par_at + 3];
    if (w != 32 || h != 32 || tex_at + tex_size > pac.size() || tex_size < uint32_t(w * h / 2)) return nullptr;
    uint32_t colors[16];
    for (int c = 0; c < 16; c++) colors[c] = rgba(pac.data() + pal_at, c);
    std::vector<uint32_t> pixels(size_t(kIconW) * kIconH);
    for (int y = 0; y < kIconH; y++) {
        for (int x = 0; x < kIconW; x++) {
            int sx = x + kIconCropX, sy = y + kIconCropY;
            pixels[size_t(y) * kIconW + x] = colors[pac[tex_at + (sy * w + sx) / 2] >> (4 * (sx & 1)) & 15];
        }
    }
    texture = make_texture(renderer, pixels, kIconW, kIconH);
    return texture;
}

// Gives each save in saves/ its "<name>-<n>.sav" name (files put there by hand). Not while
// the game runs: it would write its save under the old name again.
void rename_saves() {
    bool game_running = false, checked = false;
    for (SaveInfo& s : g_saves) {
        std::string file = target_name(s.file);
        if (file.empty()) continue;
        if (!checked) {
            game_running = game_process_running();
            checked = true;
        }
        if (game_running) {
            g_listing.clear();  // try again on the next scan
            return;
        }
        std::error_code ec;
        fs::rename(ntr::fs_path(paths().saves() + s.file), ntr::fs_path(paths().saves() + file), ec);
        if (ec) {
            NTR_LOG("[launcher] cannot rename the save %s to %s: %s\n", s.file.c_str(), file.c_str(), ec.message().c_str());
            continue;
        }
        NTR_LOG("[launcher] renamed the save %s to %s\n", s.file.c_str(), file.c_str());
        if (same_file(s.file, current_save())) settings_set("game", "save", file);
        s.file = file;
    }
    if (checked && !game_running) {
        sort_saves();
        g_listing.clear();  // the names changed: list again (no rename is left to do)
    }
}

// Lists saves/ again when a file there was added, removed or written (by the game, too).
void scan() {
    std::vector<fs::directory_entry> entries;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(ntr::fs_path(paths().saves()), ec)) {
        std::string name = ntr::utf8_string(entry.path().filename());
        if (entry.is_regular_file(ec) && is_sav(name) && name[0] != '.') entries.push_back(entry);
    }
    std::string listing;
    for (const auto& entry : entries) {
        listing += ntr::utf8_string(entry.path().filename()) + '\t' + std::to_string(entry.file_size(ec)) + '\t' +
                   std::to_string(int64_t(entry.last_write_time(ec).time_since_epoch().count())) + '\n';
    }
    if (listing == g_listing) return;
    g_listing = listing;
    g_saves.clear();
    for (const auto& entry : entries) {
        SaveInfo info;
        info.file = ntr::utf8_string(entry.path().filename());
        info.modified = modified_text(entry);
        read_save(entry.path(), info);
        g_saves.push_back(std::move(info));
    }
    sort_saves();
    rename_saves();
}

void set_status(const std::string& text, bool error = false) {
    g_status = text;
    g_status_error = error;
}

void use(const std::string& file) {
    settings_set("game", "save", file);
    set_status(L("The game will use the save ", "O jogo vai usar o save ") + file + ".");
}

// --- the card ------------------------------------------------------------------------------

// The slot card's colours (SFDN_BG00).
constexpr ImU32 kCardBody = IM_COL32(49, 115, 230, 255), kCardBand = IM_COL32(49, 57, 172, 255),
                kCardDark = IM_COL32(16, 74, 172, 255), kCardPanel = IM_COL32(172, 180, 213, 255),
                kCardPanelLow = IM_COL32(131, 148, 189, 255), kCardText = IM_COL32(255, 255, 255, 255),
                kCardSoft = IM_COL32(197, 213, 255, 255);

struct CardLayout {
    float scale;   // of the pixel art
    float line;    // a row of text
    float height;  // of the whole card
};

CardLayout card_layout() {
    CardLayout l;
    l.scale = std::max(1.0f, std::round(ImGui::GetFontSize() / 18.0f));
    l.line = ImGui::GetTextLineHeight() + 9;
    l.height = 3 * l.line + (kIconH * l.scale + 10) + 26;  // band, two rows, the team row, margins
    return l;
}

// A save as the game's slot card: the player in the band, team level and members, play time,
// the chapter and the team. Other files get the card with a message.
void card(const SaveInfo& s, bool in_use, SDL_Renderer* renderer, const CardLayout& l) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, pad = 10, rounding = 6;
    const bool data = s.valid && !s.unknown && !s.blank && !s.empty;
    const ImVec2 q(p.x + w, p.y + l.height);
    dl->AddRectFilled(p, q, data || s.blank ? kCardBody : IM_COL32(70, 82, 110, 255), rounding);
    dl->AddRect(p, q, in_use ? kCardText : kCardDark, rounding, 0, in_use ? 3.0f : 2.0f);
    // The band with the player's name, and the file name, small, at its right.
    const ImVec2 band0(p.x + 3, p.y + 3), band1(q.x - 3, p.y + 3 + l.line + 2);
    dl->AddRectFilled(band0, band1, data || s.blank ? kCardBand : IM_COL32(50, 58, 80, 255), rounding - 2,
                      ImDrawFlags_RoundCornersTop);
    auto text_at = [&](float x, float y, ImU32 color, const std::string& text) { dl->AddText(ImVec2(x, y), color, text.c_str()); };
    auto width = [](const std::string& text) { return ImGui::CalcTextSize(text.c_str()).x; };
    const float band_y = band0.y + (band1.y - band0.y - ImGui::GetTextLineHeight()) / 2;
    std::string title = data       ? (s.user.empty() ? L("(no name)", "(sem nome)") : s.user)
                        : s.blank  ? L("New save", "Save novo")
                        : !s.valid ? L("Invalid file", "Arquivo inválido")
                                   : L("Empty", "Vazio");
    text_at(p.x + (w - width(title)) / 2, band_y, kCardText, title);
    {
        const float small = ImGui::GetFontSize() * 0.8f;
        ImFont* font = ImGui::GetFont();
        ImVec2 size = font->CalcTextSizeA(small, FLT_MAX, 0, s.file.c_str());
        dl->AddText(font, small, ImVec2(band1.x - pad - size.x, band0.y + (band1.y - band0.y - size.y) / 2), IM_COL32(156, 189, 246, 255),
                    s.file.c_str());
    }
    float y = band1.y + 4;
    auto row_line = [&](float x0, float x1, float row_y) { dl->AddRectFilled(ImVec2(x0, row_y + l.line - 3), ImVec2(x1, row_y + l.line - 1), kCardDark); };
    if (!data) {
        std::string message = s.blank     ? L("Click to use it and start a New Game.", "Clique para usar e comece um Novo Jogo.")
                              : !s.valid  ? L("This does not look like a save of this game.", "Não parece um save deste jogo.")
                              : s.unknown ? L("A save of this game, in a format the launcher does not know.",
                                              "Save deste jogo, num formato que o launcher não conhece.")
                                          : L("The game has not been saved to it yet.", "O jogo ainda não foi salvo nele.");
        text_at(p.x + pad + 4, y + l.line / 2, s.valid ? kCardSoft : IM_COL32(255, 160, 150, 255), message);
        ImGui::Dummy(ImVec2(w, l.height));
        return;
    }
    // Team level and members; play time. The numbers larger, as on the card, ending at `right`.
    const float x0 = p.x + pad + 4, x1 = q.x - pad - 4, mid = p.x + w * 0.55f;
    const float big = ImGui::GetFontSize() * 1.3f;
    const float row_y = (l.line - 3 - ImGui::GetTextLineHeight()) / 2;  // the text above the row's line
    auto number = [&](float right, float y, const std::string& text) {
        ImFont* font = ImGui::GetFont();
        ImVec2 size = font->CalcTextSizeA(big, FLT_MAX, 0, text.c_str());
        // Same baseline as the normal text next to it.
        float top = y + row_y + ImGui::GetTextLineHeight() - size.y + (big - ImGui::GetFontSize()) * 0.2f;
        dl->AddText(font, big, ImVec2(right - size.x, top), kCardText, text.c_str());
        return right - size.x;
    };
    uint32_t minutes = s.play_frames / 60 / 60;
    text_at(x0, y + row_y, kCardText, L("Team Lv.", "Nív. Equipe"));
    number(mid - pad - 8, y, std::to_string(s.level));
    text_at(mid + 8, y + row_y, kCardText, L("Members", "Membros"));
    number(x1, y, std::to_string(s.members));
    row_line(x0 - 4, x1 + 4, y);
    y += l.line;
    text_at(x0, y + row_y, kCardText, L("Play time", "Tempo de jogo"));
    float right = x1 - width("Min");
    text_at(right, y + row_y, kCardText, "Min");
    char mm[8];
    std::snprintf(mm, sizeof mm, "%02u", minutes % 60);
    const char* hours = L("Hours", "Horas");
    right = number(right - 6, y, mm) - 8 - width(hours);
    text_at(right, y + row_y, kCardText, hours);
    number(right - 6, y, std::to_string(minutes / 60));
    row_line(x0 - 4, x1 + 4, y);
    y += l.line + 6;
    // The chapter and the team's faces in the light panel.
    const float panel_h = kIconH * l.scale + 10;
    char chapter[16];
    std::snprintf(chapter, sizeof chapter, "Ch%02d", s.chapter);
    const float chapter_w = width("Ch00") + 16;
    text_at(x0, y + (panel_h - ImGui::GetTextLineHeight()) / 2, kCardText, chapter);
    row_line(x0 - 4, x0 + chapter_w, y + panel_h - l.line + 2);
    const ImVec2 panel0(x0 + chapter_w + 12, y), panel1(x1 + 4, y + panel_h);
    dl->AddRectFilled(panel0, panel1, kCardPanelLow, 3);
    dl->AddRectFilled(panel0, ImVec2(panel1.x, panel0.y + panel_h * 0.45f), kCardPanel, 3, ImDrawFlags_RoundCornersTop);
    dl->AddRect(ImVec2(panel0.x - 1, panel0.y - 1), ImVec2(panel1.x + 1, panel1.y + 1), kCardDark, 3.0f, 0, 2.0f);
    float icon_x = panel0.x + 8;
    for (uint16_t unit : s.team_units) {
        SDL_Texture* texture = icon(renderer, unit);
        if (!texture) continue;
        if (icon_x + kIconW * l.scale > panel1.x - 4) break;  // no room for more
        dl->AddImage(ImTextureRef(ImTextureID(intptr_t(texture))), ImVec2(icon_x, panel0.y + 5),
                     ImVec2(icon_x + kIconW * l.scale, panel0.y + 5 + kIconH * l.scale));
        icon_x += (kIconW + 3) * l.scale;
    }
    ImGui::Dummy(ImVec2(w, l.height));
}

// "Novo save": a blank save (all 0xFF, what the game sees in a new cartridge), listed for the
// player to choose.
bool new_save(const std::string& file) {
    std::error_code ec;
    fs::create_directories(ntr::fs_path(paths().saves()), ec);
    std::vector<uint8_t> blank(kSaveSize, 0xFF);
    FILE* f = ntr::open_file(ntr::fs_path(paths().saves() + file), "wb");
    bool ok = f && std::fwrite(blank.data(), 1, blank.size(), f) == blank.size();
    if (f && std::fclose(f) != 0) ok = false;
    if (!ok) {
        set_status(L("Could not create ", "Não foi possível criar ") + file + L(" in the saves folder.", " na pasta saves."), true);
        return false;
    }
    NTR_LOG("[launcher] made the blank save %s\n", file.c_str());
    set_status(L("Created the save ", "Criado o save ") + file + L(". Click it in the list to use it.", ". Clique nele na lista para usar."));
    g_listing.clear();
    g_listing_time = -10;
    return true;
}

void new_save_popup() {
    if (!ImGui::BeginPopupModal(L("New save###new_save", "Novo save###new_save"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted(L("Save name:", "Nome do save:"));
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool enter = ImGui::InputText("##name", g_new_name, sizeof g_new_name, ImGuiInputTextFlags_EnterReturnsTrue);
    std::string base = file_name_for(g_new_name);
    std::string file = base.empty() ? "" : free_name(base);
    if (!base.empty()) ImGui::TextDisabled(L("File: %s", "Arquivo: %s"), file.c_str());
    else if (g_new_name[0]) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", L("Use letters or numbers in the name.", "Use letras ou números no nome."));
    else ImGui::TextDisabled("%s", L("File: <name>-1.sav (lowercase, letters and numbers only)",
                                     "Arquivo: <nome>-1.sav (minúsculas, só letras e números)"));
    ImGui::TextDisabled("%s", L("Creates a blank save in the list; the others do not change.",
                                "Cria um save em branco na lista; os outros não mudam."));
    ImGui::Spacing();
    ImGui::BeginDisabled(base.empty());
    if ((ImGui::Button(L("Create", "Criar")) || (enter && !base.empty())) && new_save(file)) ImGui::CloseCurrentPopup();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(L("Cancel", "Cancelar"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

}  // namespace

std::string current_save() {
    std::string name = settings().get("game", "save");
    return name.empty() ? "boej.sav" : name;
}

// A .sav dropped on the window: a copy in saves/, as "<its name>-<n>.sav". The original stays.
void saves_drop(const std::string& path) {
    fs::path from = ntr::fs_path(path);
    std::error_code ec;
    fs::path folder = ntr::fs_path(paths().saves());
    fs::create_directories(folder, ec);
    if (fs::equivalent(from.parent_path(), folder, ec)) {
        set_status(ntr::utf8_string(from.filename()) + L(" is already in the saves folder.", " já está na pasta saves."));
        return;
    }
    std::string base = file_name_for(ntr::utf8_string(from.stem()));
    std::string file = free_name(base.empty() ? "save" : base);
    if (!fs::copy_file(from, folder / ntr::fs_path(file), ec)) {
        set_status(L("Could not copy ", "Não foi possível copiar ") + path + L(" to the saves folder: ", " para a pasta saves: ") +
                       ec.message(),
                   true);
        return;
    }
    NTR_LOG("[launcher] copied the save %s to %s\n", path.c_str(), file.c_str());
    set_status(L("Imported as ", "Importado como ") + file + L(". Click it in the list to use it.", ". Clique nele na lista para usar."));
    g_listing_time = -10;
}

void saves_update() {
    if (ImGui::GetTime() - g_listing_time > 1.5) {
        g_listing_time = ImGui::GetTime();
        scan();
    }
}

void saves_tab(SDL_Renderer* renderer) {
    const std::string current = current_save();

    ImGui::TextWrapped("%s", L("You can import saves dumped from the cartridge or from other emulators: put the .sav in the "
                               "saves folder (or drag it onto this window). Keep a backup first, because the game writes over it.",
                               "Você pode importar saves dumpados do cartucho ou de outros emuladores: coloque o .sav na pasta "
                               "saves (ou arraste-o para esta janela). Guarde um backup antes, porque o jogo grava por cima."));
    ImGui::Spacing();
    // Tests: IE3_LAUNCHER_NEW_SAVE=<name> opens the "Novo save" box with that name typed.
    static const char* test_name = std::getenv("IE3_LAUNCHER_NEW_SAVE");
    if (ImGui::Button(L("New save", "Novo save")) || test_name) {
        std::snprintf(g_new_name, sizeof g_new_name, "%s", test_name ? test_name : "");
        test_name = nullptr;
        ImGui::OpenPopup("###new_save");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", L("Creates a blank save in the list, with the name you choose",
                                  "Cria na lista um save em branco, com o nome que você escolher"));
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Open the saves folder", "Abrir pasta de saves"))) open_folder(paths().saves());
    ImGui::SameLine();
    if (ImGui::Button(L("Refresh", "Atualizar"))) {
        g_listing.clear();
        g_listing_time = -10;
    }
    if (!g_status.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, g_status_error ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(0.6f, 0.85f, 0.6f, 1));
        ImGui::TextWrapped("%s", g_status.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();

    bool current_found = std::any_of(g_saves.begin(), g_saves.end(), [&](const SaveInfo& s) { return s.file == current; });
    if (!current_found) {
        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.4f, 1), L("The chosen save, %s, does not exist yet:", "O save escolhido, %s, ainda não existe:"),
                           current.c_str());
        ImGui::TextWrapped("%s", L("the game starts from scratch and creates that file when you save.",
                                   "o jogo começa do zero e cria esse arquivo quando você salvar."));
        ImGui::Spacing();
    }
    if (ImGui::BeginTable("saves", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::max(ImGui::GetContentRegionAvail().y, 120.0f)))) {
        ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn(L("Save data", "Dados do save"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(L("Last changed", "Última alteração"), ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        // Every row is a card of the same height, the lightning box and the date centered next to
        // it; a click anywhere on a row chooses that save.
        if (!g_icons.loaded) load_icons();
        const CardLayout layout = card_layout();
        const float row_height = layout.height;
        const float bolt_scale = std::max(2.0f, std::round(ImGui::GetFontSize() / 9.0f));
        const ImVec2 bolt_size(kBoltW * bolt_scale, kBoltH * bolt_scale);
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
        auto center = [&](float top, float height) { ImGui::SetCursorPosY(top + (row_height - height) / 2); };
        for (size_t i = 0; i < g_saves.size(); i++) {
            const SaveInfo& s = g_saves[i];
            bool in_use = s.file == current;
            ImGui::PushID(int(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            float top = ImGui::GetCursorPosY();
            ImGui::BeginDisabled(!s.valid);
            if (ImGui::Selectable("##row", in_use, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                  ImVec2(0, row_height)) &&
                !in_use)
                use(s.file);
            ImGui::EndDisabled();
            bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_AllowWhenOverlappedByItem);
            if (hovered) {
                if (s.valid) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                else ImGui::SetTooltip("%s", L("This file is not a save of this game", "Este arquivo não é um save deste jogo"));
            }
            if (hovered && s.valid) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(0.3f, 0.34f, 0.42f, 0.35f)));
            // The lightning box: lit on the save in use, faint on the others (a bit more on hover).
            center(top, bolt_size.y);
            float x = ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - bolt_size.x) / 2;
            ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x));
            if (SDL_Texture* texture = s.valid ? bolt(renderer) : nullptr) {
                float alpha = in_use ? 1.0f : hovered ? 0.55f : 0.22f;
                ImGui::ImageWithBg(ImTextureRef(ImTextureID(intptr_t(texture))), bolt_size, ImVec2(0, 0), ImVec2(1, 1),
                                   ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, alpha));
            } else {
                ImGui::Dummy(bolt_size);
            }

            ImGui::TableNextColumn();
            card(s, in_use, renderer, layout);
            ImGui::TableNextColumn();
            center(ImGui::GetCursorPosY(), ImGui::GetTextLineHeight());
            ImGui::TextDisabled("%s", s.modified.c_str());
            ImGui::PopID();
        }
        ImGui::PopStyleColor(3);
        if (g_saves.empty()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", L("No saves (.sav) in the saves folder.", "Nenhum save (.sav) na pasta saves."));
        }
        ImGui::EndTable();
    }
    new_save_popup();
}

}  // namespace launcher
