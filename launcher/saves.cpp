// The saves of "Jogar": the .sav files in saves/, what each one holds (the same summary as the
// game's slot card) and which one the game uses ([game] save in boej.ini, read by
// arm7_card_init in the runtime). "Novo jogo" chooses a new file, "Carregar jogo" lists the
// saves with a game in them as cards and "Importar save" copies one in. Every save is named
// "<name>-<n>.sav": the name the player typed in "Novo jogo", or the file's own name for one
// imported (chosen, dropped on the window or put in the folder by hand), in lowercase ASCII
// letters and digits (kana in romaji), and n the first number free for that name. [game] save
// follows a rename.
//
// What a save holds is read by ntr/savedata.h (shared with the game's achievements).
//
// The team's faces are the icons of the friends list: /data_iz/pic3d/ih.pkh/.pkb in boej.pak,
// keyed by unit number. That .pkh is "PackNum" with (u32 key, u32 size) entries from 0x30; the
// .pkb keeps each entry at index * (the largest size, u32 at 0x1C). An entry is LZ10 of a 3D PAC:
// a 32x32 4 bpp texture, 16 colours (colour 0 transparent) and the sizes.
//
// Each card is drawn like the game's slot card (pic2d/menu/MMSlot.SPF_: SFDN_BG00, its
// colours), and the save used last is marked with the card's lightning box (SFDN_I00 there: a
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

#include <ntr/achievements.h>
#include <ntr/nitro.h>
#include <ntr/savedata.h>

#include "launcher.h"

namespace fs = std::filesystem;

namespace launcher {

namespace {

constexpr size_t kSaveSize = ntr::kSaveDataSize;

struct SaveInfo {
    std::string file;  // the name in saves/
    std::string modified;
    bool valid = false;    // a save of this game
    bool empty = true;     // valid, but no game saved in it yet
    bool blank = false;    // all 0xFF, like a new cartridge (made by older launchers)
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
    ntr::SaveSummary s = ntr::read_save_data(data.data(), got);
    info.valid = s.valid;
    info.empty = s.empty;
    info.blank = s.blank;
    info.unknown = s.unknown;
    if (!s.valid || s.empty) return;
    info.play_frames = s.play_frames;
    info.chapter = s.chapter;
    info.level = s.level;
    info.members = s.members;
    info.user = from_sjis(s.user, sizeof s.user);
    info.team_units = s.team_units;
    std::stable_sort(info.team_units.begin(), info.team_units.end(), [&](uint16_t a, uint16_t b) { return s.shirt[a] < s.shirt[b]; });
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

// By name, the blank ones first.
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
        // Its achievements go with it.
        std::string from = ntr::Achievements::state_path(paths().saves() + s.file), to = ntr::Achievements::state_path(paths().saves() + file);
        if (fs::exists(ntr::fs_path(from), ec)) fs::rename(ntr::fs_path(from), ntr::fs_path(to), ec);
        if (ec) NTR_LOG("[launcher] cannot rename %s: %s\n", from.c_str(), ec.message().c_str());
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

// --- the card ------------------------------------------------------------------------------

// The slot card's colours (SFDN_BG00).
constexpr ImU32 kCardBody = IM_COL32(49, 115, 230, 255), kCardBodyLit = IM_COL32(66, 132, 246, 255),
                kCardBand = IM_COL32(49, 57, 172, 255), kCardDark = IM_COL32(16, 74, 172, 255),
                kCardPanel = IM_COL32(172, 180, 213, 255), kCardPanelLow = IM_COL32(131, 148, 189, 255),
                kCardText = IM_COL32(255, 255, 255, 255), kCardSoft = IM_COL32(197, 213, 255, 255),
                kCardFile = IM_COL32(156, 189, 246, 255);

// The saves "Carregar jogo" lists: a game saved in them (a format the launcher does not know
// is listed too, the game may read it).
bool loadable(const SaveInfo& s) { return s.valid && !s.blank && !s.empty; }

// A save as a compact slot card, a button: the player in the band (the lightning box before it
// on the save used last), the file and date at its right; under it the chapter, team level,
// members and play time, and the team's faces in the light panel. True when clicked.
bool card(const SaveInfo& s, bool in_use, SDL_Renderer* renderer) {
    const float scale = std::max(1.0f, std::round(ImGui::GetFontSize() / 18.0f));
    const float line = ImGui::GetTextLineHeight() + 9, panel_h = kIconH * scale + 10, pad = 10, rounding = 6;
    const float height = 3 + line + 2 + 6 + panel_h + 8;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 44);
    bool pressed = ImGui::InvisibleButton("##card", ImVec2(w, height), ImGuiButtonFlags_EnableNav);
    const bool lit = ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible);
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const bool data = !s.unknown;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + w, p.y + height);
    dl->AddRectFilled(p, q, lit ? kCardBodyLit : kCardBody, rounding);
    dl->AddRect(p, q, lit || in_use ? kCardText : kCardDark, rounding, 0, lit ? 3.0f : 2.0f);
    auto text_at = [&](float x, float y, ImU32 color, const std::string& text) { dl->AddText(ImVec2(x, y), color, text.c_str()); };
    auto width = [](const std::string& text) { return ImGui::CalcTextSize(text.c_str()).x; };

    // The band.
    const ImVec2 band0(p.x + 3, p.y + 3), band1(q.x - 3, p.y + 3 + line + 2);
    dl->AddRectFilled(band0, band1, kCardBand, rounding - 2, ImDrawFlags_RoundCornersTop);
    const float band_h = band1.y - band0.y;
    float x = band0.x + pad;
    if (SDL_Texture* texture = in_use ? bolt(renderer) : nullptr) {
        const float bh = band_h - 6, bw = bh * kBoltW / kBoltH;
        dl->AddImage(ImTextureRef(ImTextureID(intptr_t(texture))), ImVec2(x, band0.y + 3), ImVec2(x + bw, band0.y + 3 + bh));
        x += bw + 8;
    }
    std::string title = s.user.empty() ? L("(no name)", "(sem nome)") : s.user;
    text_at(x, band0.y + (band_h - ImGui::GetTextLineHeight()) / 2, kCardText, title);
    {
        const float small = ImGui::GetFontSize() * 0.8f;
        ImFont* font = ImGui::GetFont();
        std::string file = s.file + (s.modified.empty() ? "" : "  ·  " + s.modified);
        ImVec2 size = font->CalcTextSizeA(small, FLT_MAX, 0, file.c_str());
        dl->AddText(font, small, ImVec2(band1.x - pad - size.x, band0.y + (band_h - size.y) / 2), kCardFile, file.c_str());
    }

    const float y = band1.y + 6, x0 = p.x + pad + 4, x1 = q.x - pad - 4;
    if (!data) {
        text_at(x0, y + (panel_h - ImGui::GetTextLineHeight()) / 2, kCardSoft,
                L("A save of this game, in a format the launcher does not know.", "Save deste jogo, num formato que o launcher não conhece."));
        return pressed;
    }
    // Two rows at the left: the chapter and team level; the members and play time.
    char row1[64], row2[64];
    uint32_t minutes = s.play_frames / 60 / 60;
    std::snprintf(row1, sizeof row1, L("Ch%02d  ·  Team Lv. %d", "Ch%02d  ·  Nív. Equipe %d"), s.chapter, s.level);
    std::snprintf(row2, sizeof row2, L("%d members  ·  %uh%02u", "%d membros  ·  %uh%02u"), s.members, minutes / 60, minutes % 60);
    const float gap = (panel_h - 2 * ImGui::GetTextLineHeight()) / 3;
    text_at(x0, y + gap, kCardText, row1);
    text_at(x0, y + gap * 2 + ImGui::GetTextLineHeight(), kCardSoft, row2);
    // The team's faces at the right.
    // As wide as the longest the rows get, so the panels line up from card to card.
    char wide1[64], wide2[64];
    std::snprintf(wide1, sizeof wide1, L("Ch%02d  ·  Team Lv. %d", "Ch%02d  ·  Nív. Equipe %d"), 88, 88);
    std::snprintf(wide2, sizeof wide2, L("%d members  ·  %uh%02u", "%d membros  ·  %uh%02u"), 88, 888u, 88u);
    const float info_w = std::max({width(row1), width(row2), width(wide1), width(wide2)});
    const ImVec2 panel0(x0 + info_w + 18, y), panel1(x1 + 4, y + panel_h);
    if (panel1.x - panel0.x < kIconW * scale + 16) return pressed;
    dl->AddRectFilled(panel0, panel1, kCardPanelLow, 3);
    dl->AddRectFilled(panel0, ImVec2(panel1.x, panel0.y + panel_h * 0.45f), kCardPanel, 3, ImDrawFlags_RoundCornersTop);
    dl->AddRect(ImVec2(panel0.x - 1, panel0.y - 1), ImVec2(panel1.x + 1, panel1.y + 1), kCardDark, 3.0f, 0, 2.0f);
    float icon_x = panel0.x + 8;
    for (uint16_t unit : s.team_units) {
        SDL_Texture* texture = icon(renderer, unit);
        if (!texture) continue;
        if (icon_x + kIconW * scale > panel1.x - 4) break;  // no room for more
        dl->AddImage(ImTextureRef(ImTextureID(intptr_t(texture))), ImVec2(icon_x, panel0.y + 5),
                     ImVec2(icon_x + kIconW * scale, panel0.y + 5 + kIconH * scale));
        icon_x += (kIconW + 3) * scale;
    }
    return pressed;
}

// --- "Novo jogo" ---------------------------------------------------------------------------

bool g_open_new_game = false;

// A name for the new save; the game starts from scratch (no file is what it sees in a new
// cartridge) and writes it when the player saves. True when the player starts.
bool new_game_box() {
    if (!ImGui::BeginPopupModal(L("New game###new_game", "Novo jogo###new_game"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return false;
    ImGui::TextUnformatted(L("Save name:", "Nome do save:"));
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool enter = ImGui::InputText("##name", g_new_name, sizeof g_new_name, ImGuiInputTextFlags_EnterReturnsTrue);
    std::string base = file_name_for(g_new_name);
    std::string file = base.empty() ? "" : free_name(base);
    if (!base.empty()) ImGui::TextDisabled(L("File: %s", "Arquivo: %s"), file.c_str());
    else if (g_new_name[0]) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", L("Use letters or numbers in the name.", "Use letras ou números no nome."));
    else ImGui::TextDisabled("%s", L("File: <name>-1.sav (lowercase, letters and numbers only)",
                                     "Arquivo: <nome>-1.sav (minúsculas, só letras e números)"));
    ImGui::TextDisabled("%s", L("The game starts from scratch and writes this file when you save; the other saves do not change.",
                                "O jogo começa do zero e grava esse arquivo quando você salvar; os outros saves não mudam."));
    ImGui::Spacing();
    bool start = false;
    ImGui::BeginDisabled(base.empty());
    if (ImGui::Button(L("Start", "Começar")) || (enter && !base.empty())) {
        settings_set("game", "save", file);
        NTR_LOG("[launcher] new game in %s\n", file.c_str());
        start = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(L("Cancel", "Cancelar"))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return start;
}

}  // namespace

std::string current_save() {
    std::string name = settings().get("game", "save");
    return name.empty() ? "boej.sav" : name;
}

// A .sav dropped on the window or chosen in "Importar save": a copy in saves/, as
// "<its name>-<n>.sav". The original stays; a file that is not a save of this game is not kept.
void saves_drop(const std::string& path) {
    fs::path from = ntr::fs_path(path);
    std::error_code ec;
    fs::path folder = ntr::fs_path(paths().saves());
    fs::create_directories(folder, ec);
    if (fs::equivalent(from.parent_path(), folder, ec)) {
        set_status(ntr::utf8_string(from.filename()) + L(" is already in the saves folder.", " já está na pasta saves."));
        return;
    }
    SaveInfo info;
    read_save(from, info);
    if (!info.valid) {
        set_status(ntr::utf8_string(from.filename()) + L(" does not look like a save of this game.", " não parece um save deste jogo."), true);
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
    set_status(loadable(info) ? L("Imported as ", "Importado como ") + file + "."
                              : L("Imported as ", "Importado como ") + file +
                                    L(", but no game was saved in it yet.", ", mas ainda não há jogo salvo nele."));
    g_listing_time = -10;
}

void saves_update() {
    if (ImGui::GetTime() - g_listing_time > 1.5) {
        g_listing_time = ImGui::GetTime();
        scan();
    }
}

bool saves_loadable() { return std::any_of(g_saves.begin(), g_saves.end(), loadable); }

bool saves_load_tab(SDL_Renderer* renderer) {
    const std::string current = current_save();
    if (!g_status.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, g_status_error ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(0.6f, 0.85f, 0.6f, 1));
        ImGui::TextWrapped("%s", g_status.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::TextDisabled("%s", L("Click a save to play it. The game writes over it when you save.",
                                "Clique num save para jogar nele. O jogo grava por cima quando você salvar."));
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Open the saves folder", "Abrir pasta de saves"))) open_folder(paths().saves());
    ImGui::Spacing();
    if (!g_icons.loaded) load_icons();
    bool chosen = false;
    ImGui::BeginChild("load", ImVec2(0, 0));
    int shown = 0;
    for (size_t i = 0; i < g_saves.size(); i++) {
        const SaveInfo& s = g_saves[i];
        if (!loadable(s)) continue;
        ImGui::PushID(int(i));
        if (card(s, same_file(s.file, current), renderer) && !chosen) {
            settings_set("game", "save", s.file);
            NTR_LOG("[launcher] loading the save %s\n", s.file.c_str());
            chosen = true;
        }
        if (shown++ == 0 && ImGui::IsWindowAppearing()) ImGui::SetItemDefaultFocus();
        ImGui::PopID();
        ImGui::Spacing();
    }
    if (shown == 0) ImGui::TextDisabled("%s", L("No saved game in the saves folder.", "Nenhum jogo salvo na pasta saves."));
    ImGui::EndChild();
    if (chosen) g_status.clear();
    return chosen;
}

void new_game_open() { g_open_new_game = true; }

bool new_game_popup() {
    // Tests: IE3_LAUNCHER_NEW_SAVE=<name> opens the box with that name typed.
    static const char* test_name = std::getenv("IE3_LAUNCHER_NEW_SAVE");
    if (test_name) {
        g_open_new_game = true;
        std::snprintf(g_new_name, sizeof g_new_name, "%s", test_name);
        test_name = nullptr;
    } else if (g_open_new_game) {
        g_new_name[0] = 0;
    }
    if (g_open_new_game) {
        ImGui::OpenPopup("###new_game");
        g_open_new_game = false;
    }
    return new_game_box();
}

}  // namespace launcher
